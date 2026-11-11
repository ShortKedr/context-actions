#include "platform.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <iomanip>
#include <random>
#include <set>
#include <sstream>

namespace ca {
namespace {
[[noreturn]] void fail(const std::wstring& message) { throw std::runtime_error(utf8(message)); }
std::wstring lower(std::wstring s) { for (auto& c : s) c = towlower(c); return s; }
bool contains(const wchar_t* list, const std::wstring& extension) { return std::wstring(list).find(L"|" + extension + L"|") != std::wstring::npos; }
std::wstring randomId() {
    std::random_device random;
    std::wostringstream id;
    id << std::hex << std::setfill(L'0');
    for (int i = 0; i < 4; ++i) id << std::setw(8) << static_cast<uint32_t>(random());
    return id.str();
}
void stopped(const std::atomic_bool& cancel) { if (cancel) throw Cancelled(); }
void replaceAll(std::wstring& value, const std::wstring& from, const std::wstring& to) {
    if (from.empty()) return;
    size_t at = 0;
    while ((at = value.find(from, at)) != std::wstring::npos) { value.replace(at, from.size(), to); at += to.size(); }
}
struct Temporary {
    fs::path path;
    explicit Temporary(const fs::path& parent) : path(parent / textPath(L".context-actions-" + randomId())) {
        createPrivateDirectory(path);
    }
    ~Temporary() { std::error_code ec; fs::remove_all(path, ec); }
};
void writeString(std::ostream& stream, const std::wstring& value) {
    const auto text = utf8(value);
    const auto size = static_cast<uint32_t>(text.size());
    stream.write(reinterpret_cast<const char*>(&size), sizeof(size)); stream.write(text.data(), size);
}
std::wstring readString(std::istream& stream) {
    uint32_t size = 0;
    stream.read(reinterpret_cast<char*>(&size), sizeof(size));
    if (!stream || size > 131072) fail(tr("invalid"));
    std::string value(size, '\0'); stream.read(value.data(), size);
    if (!stream || value.find('\0') != std::string::npos) fail(tr("invalid"));
    return wide(value);
}
void engineSuccess(const ProcessResult& process) {
    if (process.code != 0) fail(tr("failed") + L": " + wide(process.output));
}
double probeDuration(const fs::path& input, std::atomic_bool& cancel) {
    auto result = runProcess(engine("ffprobe"), {L"-v",L"error",L"-show_entries",L"format=duration",L"-of",L"default=noprint_wrappers=1:nokey=1",pathText(input)}, cancel, {}, 60000);
    engineSuccess(result);
    std::istringstream stream(result.output);
    stream.imbue(std::locale::classic());
    double duration = 0;
    return (stream >> duration) && std::isfinite(duration) && duration > 0 ? duration : 0;
}
void imageConversion(const Options& options, const fs::path& source, const fs::path& output, std::atomic_bool& cancel) {
    std::vector<std::wstring> args{L"-hide_banner",L"-loglevel",L"error",L"-nostdin",L"-n",L"-i",pathText(source)};
    if (options.action == Action::frame) args.insert(args.end(), {L"-map",L"0:V:0"});
    auto filter = options.action != Action::frame && options.width
        ? L"scale=w='min(iw," + std::to_wstring(options.width) + L")':h=-1:flags=lanczos" : L"null";
    auto ext = lower(pathText(output.extension()));
    if (ext == L".jpg" || ext == L".jpeg" || ext == L".bmp") {
        filter += L",format=rgba,split[fg][bg];[bg]lutrgb=r=255:g=255:b=255,format=rgb24[white];[white][fg]overlay=shortest=1:format=auto,format=rgb24";
        args.insert(args.end(), {L"-filter_complex",filter});
    } else args.insert(args.end(), {L"-vf",filter});
    args.insert(args.end(), {L"-frames:v",L"1",L"-map_metadata",L"-1",L"-update",L"1"});
    if (ext == L".webp") args.insert(args.end(), {L"-c:v",L"libwebp",L"-quality",std::to_wstring(100-options.quality)});
    if (ext == L".jpg" || ext == L".jpeg") args.insert(args.end(), {L"-q:v",std::to_wstring(2+options.quality/4)});
    args.push_back(pathText(output));
    engineSuccess(runProcess(engine("ffmpeg"), args, cancel));
    engineSuccess(runProcess(engine("ffmpeg"), {L"-v",L"error",L"-nostdin",L"-i",pathText(output),L"-frames:v",L"1",L"-f",L"null",L"-"}, cancel, {}, 60000));
}

std::vector<std::wstring> audioOutputArguments(const std::wstring& format) {
    if (format == L"mp3") return {L"-c:a",L"libmp3lame",L"-q:a",L"2"};
    if (format == L"m4a") return {L"-c:a",L"aac",L"-b:a",L"192k"};
    if (format == L"wav") return {L"-c:a",L"pcm_s16le"};
    if (format == L"flac") return {L"-c:a",L"flac"};
    return {L"-c:a",L"libopus",L"-b:a",L"128k"};
}

void mediaReport(const fs::path& source, const fs::path& output, std::atomic_bool& cancel) {
    std::ofstream report(output, std::ios::binary);
    report << utf8(tr("media-info") + L"\n" + pathText(source.filename()) + L"\n\n");
    if (!report) fail(tr("failed"));
    const auto probe = runProcess(engine("ffprobe"), {
        L"-v",L"error",L"-pretty",L"-show_entries",
        L"format=format_name,format_long_name,duration,size,bit_rate:"
        L"stream=index,codec_type,codec_name,profile,width,height,avg_frame_rate,pix_fmt,"
        L"color_space,color_transfer,color_primaries,sample_rate,channels,channel_layout:"
        L"stream_tags=language:chapter=start_time,end_time",
        pathText(source)
    }, cancel, [&](const std::string& chunk) { report << chunk; }, 60000);
    engineSuccess(probe);
    if (probe.output.empty()) fail(tr("failed"));
    report.close();
    if (!report) fail(tr("failed"));
}

std::wstring outputExtension(const Options& options) {
    switch (options.action) {
    case Action::image: case Action::audio: case Action::remux: return L"." + outputFormat(options);
    case Action::pdf: case Action::word: case Action::excel: return L".pdf";
    case Action::frame: return L".png";
    case Action::mediaInfo: return L".txt";
    default: return L".mp4";
    }
}
}

Kind kind(const fs::path& path) {
    const auto ext = lower(pathText(path.extension()));
    if (contains(L"|.mp4|.mkv|.mov|.avi|.webm|.m4v|.wmv|.mts|.m2ts|",ext)) return Kind::video;
    if (contains(L"|.mp3|.m4a|.aac|.wav|.flac|.ogg|.oga|.opus|.aif|.aiff|.wma|",ext)) return Kind::audio;
    if (contains(L"|.jpg|.jpeg|.png|.webp|.bmp|.tif|.tiff|.gif|",ext)) return Kind::image;
    if (ext == L".pdf") return Kind::pdf;
    if (contains(L"|.docx|.doc|.odt|.rtf|.txt|",ext)) return Kind::document;
    if (contains(L"|.xlsx|.xls|.ods|.csv|",ext)) return Kind::sheet;
    return Kind::other;
}
const char* actionKey(Action a) {
    static const char* keys[]{"more","compress","h264","h265","telegram","image","pdf","rename","word","excel","documents","sheets","audio","remux","frame","media-info"};
    const auto i = static_cast<size_t>(a);
    if (i >= std::size(keys)) fail(tr("invalid"));
    return keys[i];
}
std::wstring actionId(Action action) { return wide(actionKey(action)); }
std::wstring trAction(Action action) {
    if (action == Action::word || action == Action::excel) return tr(officeActionKey(action == Action::excel));
    return tr(actionKey(action));
}
Action parseAction(const std::wstring& value) {
    for (int i=0;i<=static_cast<int>(Action::mediaInfo);++i) if (actionId(static_cast<Action>(i)) == value) return static_cast<Action>(i);
    fail(tr("invalid"));
}
std::vector<Action> catalog() {
    return {Action::compress,Action::h264,Action::h265,Action::telegram,Action::image,Action::pdf,
        Action::audio,Action::remux,Action::frame,Action::mediaInfo,Action::rename,Action::word,Action::excel,Action::documents,Action::sheets};
}
std::vector<std::wstring> outputFormats(Action action) {
    switch (action) {
    case Action::image: return {L"png",L"jpg",L"webp",L"bmp",L"tiff",L"gif"};
    case Action::audio: return {L"mp3",L"m4a",L"wav",L"flac",L"opus"};
    case Action::remux: return {L"mkv",L"mp4",L"mov"};
    default: return {};
    }
}
std::wstring outputFormat(const Options& options) {
    if (!options.format.empty()) return lower(options.format);
    const auto formats = outputFormats(options.action);
    return formats.empty() ? std::wstring{} : formats.front();
}
bool accepts(Action action, const std::vector<fs::path>& files) {
    if (action == Action::more) return true;
    if (files.empty()) return false;
    return std::all_of(files.begin(),files.end(),[action](const auto& file) {
        auto k = kind(file);
        switch(action) {
        case Action::compress: case Action::h264: case Action::h265: case Action::telegram:
        case Action::remux: case Action::frame: return k == Kind::video;
        case Action::audio: return k == Kind::audio || k == Kind::video;
        case Action::mediaInfo: return k == Kind::audio || k == Kind::video || k == Kind::image;
        case Action::image: return k == Kind::image;
        case Action::pdf: return k == Kind::image || k == Kind::pdf;
        case Action::rename: return true;
        case Action::word: case Action::documents: return k == Kind::document;
        case Action::excel: case Action::sheets: return k == Kind::sheet;
        default: return false;
        }
    });
}
std::wstring unavailableReason(Action action, const std::vector<fs::path>& files) {
    if (action == Action::documents || action == Action::sheets) return tr("next");
    if (files.empty()) return tr("select");
    if (!accepts(action,files)) return tr("unsupported");
    if (action == Action::word || action == Action::excel)
        if (officeCapability(action == Action::excel) == Capability::unavailable) return officeUnavailableReason(action == Action::excel);
    return {};
}
void validate(const Options& o) {
    const auto reason = unavailableReason(o.action,o.files);
    if (!reason.empty()) fail(reason);
    if (o.action == Action::more || o.files.size()>100000) fail(tr("invalid"));
    const bool video=o.action==Action::compress||o.action==Action::h264||o.action==Action::h265||o.action==Action::telegram;
    if ((video||o.action==Action::image) && (o.quality<0 || o.quality>51 || o.width<0 || o.width>16384 || o.width==1)) fail(tr("invalid"));
    if (video && (o.fps<0 || o.fps>240 || o.targetMB<0 || o.targetMB>1048576)) fail(tr("invalid"));
    if (o.action==Action::rename && (o.start<0 || o.start>1000000000)) fail(tr("invalid"));
    if(o.action==Action::rename||o.action==Action::pdf)
        for (const auto& path : o.files) if (!fs::is_regular_file(path)) fail(tr("missing")+pathText(path));
    if (o.action!=Action::rename && !o.output.empty() && fs::exists(o.output) && !fs::is_directory(o.output)) fail(tr("invalid"));
    const auto formats = outputFormats(o.action);
    if (!formats.empty() && std::find(formats.begin(),formats.end(),outputFormat(o)) == formats.end()) fail(tr("invalid"));
    if (o.action == Action::image || o.action == Action::pdf) {
        for (const auto& path : o.files)
            if (!o.firstFrame && contains(L"|.gif|.tif|.tiff|.webp|",lower(pathText(path.extension())))) fail(tr("frameRequired"));
    }
}
void saveOptions(const Options& o, const fs::path& path) {
    std::ofstream file(path,std::ios::binary|std::ios::trunc); file.write("CAJ1",4);
    for (const auto& s : {actionId(o.action),pathText(o.output),o.format,o.pattern,o.prefix,o.suffix,o.find,o.replace}) writeString(file,s);
    const int values[]{o.quality,o.width,o.fps,o.targetMB,o.mute?1:0,o.firstFrame?1:0,o.start,static_cast<int>(o.files.size())};
    file.write(reinterpret_cast<const char*>(values),sizeof(values));
    for (const auto& p : o.files) writeString(file,pathText(fs::absolute(p)));
    file.flush(); if (!file) fail(tr("failed"));
}
Options readJob(const fs::path& path) {
    if (fs::file_size(path)>128*1024*1024) fail(tr("invalid"));
    std::ifstream file(path,std::ios::binary); char magic[4]{}; file.read(magic,4);
    if (std::memcmp(magic,"CAJ1",4)) fail(tr("invalid"));
    Options o; o.action=parseAction(readString(file)); o.output=textPath(readString(file)); o.format=readString(file); o.pattern=readString(file);
    o.prefix=readString(file); o.suffix=readString(file); o.find=readString(file); o.replace=readString(file);
    int v[8]{}; file.read(reinterpret_cast<char*>(v),sizeof(v));
    if (!file || v[7]<0 || v[7]>100000 || (v[4]!=0 && v[4]!=1) || (v[5]!=0 && v[5]!=1)) fail(tr("invalid"));
    o.quality=v[0];o.width=v[1];o.fps=v[2];o.targetMB=v[3];o.mute=v[4]!=0;o.firstFrame=v[5]!=0;o.start=v[6];
    for (int i=0;i<v[7];++i) o.files.push_back(textPath(readString(file)));
    if (file.peek()!=std::char_traits<char>::eof()) fail(tr("invalid"));
    return o;
}
fs::path writeJob(const Options& o) {
    const auto state = dataDirectory();
    ensurePrivateDirectory(state);
    const auto directory = state / "jobs";
    ensurePrivateDirectory(directory);
    const auto path=directory/textPath(randomId()+L".caj"); saveOptions(o,path); return path;
}
void consumeJob(const fs::path& path) {
    // Only internally generated jobs may be consumed; an arbitrary --job file is never deleted.
    if (fs::weakly_canonical(path.parent_path())==fs::weakly_canonical(dataDirectory()/"jobs") && path.extension()==".caj") fs::remove(path);
}
std::vector<std::wstring> videoArguments(const Options& o, const fs::path& input, const fs::path& output, double duration, int attempt) {
    bool hevc=o.action==Action::h265, telegram=o.action==Action::telegram;
    // FFmpeg detects the input stream's codec and selects its default decoder.
    std::vector<std::wstring> args{L"-hide_banner",L"-loglevel",L"error",L"-nostdin",L"-n",L"-progress",L"pipe:1",L"-i",pathText(input),L"-map",L"0:v:0"};
    if (o.mute||telegram) args.push_back(L"-an"); else args.insert(args.end(),{L"-map",L"0:a:0?",L"-c:a",L"aac",L"-b:a",L"128k"});
    args.insert(args.end(),{L"-c:v",hevc?L"libx265":L"libx264",L"-preset",L"medium",L"-pix_fmt",L"yuv420p"});
    if(hevc) args.insert(args.end(),{L"-tag:v",L"hvc1"});
    const int width=o.width?o.width:(telegram?720:0),fps=o.fps?o.fps:(telegram?30:0);
    auto filter = width ? L"scale=w='min(iw,"+std::to_wstring(width)+L")':h=-2:force_divisible_by=2" : std::wstring(L"scale=trunc(iw/2)*2:trunc(ih/2)*2");
    if(width) filter+=L",scale=trunc(iw/2)*2:trunc(ih/2)*2";
    if(fps) filter+=L",fps="+std::to_wstring(fps);
    args.insert(args.end(),{L"-vf",filter});
    if(o.targetMB) {
        if(duration<=0 || !std::isfinite(duration)) fail(tr("sizeFailed"));
        double bitrate=(o.targetMB*1048576.0*8/duration*0.94-(o.mute||telegram?0:128000))*std::pow(0.8,attempt);
        if(bitrate<64000) fail(tr("sizeFailed"));
        auto rate=std::to_wstring(static_cast<long long>(bitrate));
        args.insert(args.end(),{L"-b:v",rate,L"-maxrate",rate,L"-bufsize",std::to_wstring(static_cast<long long>(bitrate*2))});
    } else args.insert(args.end(),{L"-crf",std::to_wstring(o.quality)});
    args.insert(args.end(),{L"-movflags",L"+faststart",L"-map_metadata",L"-1",L"-sn",L"-dn",pathText(output)});
    return args;
}
fs::path uniqueOutput(const fs::path& directory, const std::wstring& stem, const std::wstring& extension) {
    auto candidate=directory/textPath(stem+extension);
    for(int n=2;fs::exists(candidate);++n) candidate=directory/textPath(stem+L" ("+std::to_wstring(n)+L")"+extension);
    return candidate;
}
void commitOutput(const fs::path& temp, const fs::path& destination) {
    if(!fs::is_regular_file(temp) || fs::file_size(temp)==0) fail(tr("failed"));
    try { moveFile(temp, destination); }
    catch (const std::exception& error) { fail(tr("conflict") + pathText(destination) + L"\n" + wide(error.what())); }
}
std::vector<Rename> previewRename(const Options& o) {
    if(o.pattern.empty()) fail(tr("invalid"));
    std::vector<Rename> result; std::set<std::wstring> targets, sources;
    for(size_t i=0;i<o.files.size();++i) {
        auto source=fs::absolute(o.files[i]);
        auto name=pathText(source.stem()); replaceAll(name,o.find,o.replace);
        auto pattern=o.pattern; replaceAll(pattern,L"{name}",name); replaceAll(pattern,L"{n}",std::to_wstring(o.start+i));
        name=o.prefix+pattern+o.suffix+pathText(source.extension());
        auto base=lower(pathText(textPath(name).stem())); auto dot=base.find(L'.'); if(dot!=std::wstring::npos) base.resize(dot);
        bool reserved=base==L"con"||base==L"prn"||base==L"aux"||base==L"nul"||base==L"clock$"||(base.size()==4 && (base.starts_with(L"com")||base.starts_with(L"lpt")) && base[3]>=L'1' && base[3]<=L'9');
        if(name.empty()||name.size()>255||name.back()==L'.'||name.back()==L' '||name.find_first_of(L"<>:\"/\\|?*{}")!=std::wstring::npos||std::any_of(name.begin(),name.end(),[](auto c){return c<32;})||reserved) fail(tr("conflict")+name);
        auto to=source.parent_path()/textPath(name);
        if(!targets.insert(pathKey(to)).second || !sources.insert(pathKey(source)).second) fail(tr("conflict")+name);
        if(pathKey(source)!=pathKey(to) && fs::exists(to)) fail(tr("conflict")+pathText(to));
        result.push_back({source,to});
    }
    return result;
}
namespace {
long long fileStamp(const fs::path& path) {
    return static_cast<long long>(fs::last_write_time(path).time_since_epoch().count());
}

void applyRenameLocked(const std::vector<Rename>& changes, std::atomic_bool& cancel, bool saveUndo,
                       const fs::path& state) {
    // ponytail: reject swaps and cycles; add staged renames only when that workflow is needed.
    for(const auto& change:changes) {
        if(!fs::is_regular_file(change.from)) fail(tr("missing")+pathText(change.from));
        if(pathKey(change.from)!=pathKey(change.to) && fs::exists(change.to)) fail(tr("conflict")+pathText(change.to));
    }
    const auto journal=state/"rename-undo.txt";
    const auto pending=state/"rename-pending.txt";
    if(fs::exists(pending)) fail(L"An interrupted rename needs recovery; see " + pathText(pending));
    if(saveUndo) {
        Temporary scratch(state);
        const auto prepared = scratch.path / "journal";
        auto log = openNewPrivateFile(prepared);
        for (const auto& change : changes) {
            std::ostringstream entry;
            entry << std::quoted(utf8(pathText(change.from))) << ' ' << std::quoted(utf8(pathText(change.to)))
                  << ' ' << fs::file_size(change.from) << ' ' << fileStamp(change.from) << '\n';
            const auto line = entry.str();
            if (!entry || std::fwrite(line.data(), 1, line.size(), log.get()) != line.size()) fail(tr("failed"));
        }
        if (std::fclose(log.release()) != 0) fail(tr("failed"));
        moveFile(prepared,pending);
    }
    size_t moved=0;
    try {
        for(const auto& c:changes) {
            stopped(cancel);
            if(c.from!=c.to) moveFile(c.from,c.to);
            ++moved;
        }
        if(saveUndo) moveFile(pending,journal,true);
    } catch(...) {
        bool restored=true;
        while(moved) {
            const auto& c=changes[--moved];
            try { if(c.from!=c.to) moveFile(c.to,c.from); }
            catch(const std::exception&) { restored=false; }
        }
        if(saveUndo && restored) fs::remove(pending);
        if(!restored) fail(L"Rename rollback incomplete. Recovery journal: "+pathText(pending));
        throw;
    }
}
}

void applyRename(const std::vector<Rename>& changes, std::atomic_bool& cancel, bool saveUndo) {
    const auto state = dataDirectory();
    ensurePrivateDirectory(state);
    withRenameLock(state / "rename.lock", [&] { applyRenameLocked(changes, cancel, saveUndo, state); });
}

void undoRename(std::atomic_bool& cancel) {
    const auto state = dataDirectory();
    if (!fs::is_directory(state)) fail(tr("invalid"));
    ensurePrivateDirectory(state);
    withRenameLock(state / "rename.lock", [&] {
        const auto journal=state/"rename-undo.txt";
        if (!fs::is_regular_file(fs::symlink_status(journal)) || fs::hard_link_count(journal) != 1)
            fail(tr("invalid"));
        std::ifstream file(journal,std::ios::binary);
        if(!file) fail(tr("invalid"));
        std::vector<Rename> reverse;
        std::string from,to;
        uintmax_t size;
        long long modified;
        while (file >> std::ws && file.peek() != std::char_traits<char>::eof()) {
            if (!(file>>std::quoted(from)>>std::quoted(to)>>size>>modified)) fail(tr("invalid"));
            const auto source=textPath(wide(to));
            if(!fs::is_regular_file(source)||fs::file_size(source)!=size||fileStamp(source)!=modified) fail(tr("conflict")+pathText(source));
            reverse.push_back({source,textPath(wide(from))});
        }
        if(!file.eof()||reverse.empty()) fail(tr("invalid"));
        file.close();
        applyRenameLocked(reverse,cancel,false,state);
        fs::remove(journal);
    });
}
Result execute(const Options& o, std::atomic_bool& cancel, const Progress& notify) {
    validate(o); Result result;
    if(o.action==Action::rename) {
        auto changes=previewRename(o);
        try { applyRename(changes,cancel); }
        catch (const Cancelled&) {
            result.cancelled = true;
            if (notify) notify({0,0,tr("cancelled"),{}});
            return result;
        }
        for(size_t i=0;i<changes.size();++i) {result.outputs.push_back(changes[i].to);if(notify)notify({i,1,tr("done"),changes[i].to});++result.succeeded;}
        return result;
    }
    const auto extension = outputExtension(o);
    const size_t count=o.action==Action::pdf?1:o.files.size();
    for(size_t i=0;i<count;++i) {
        try {
            stopped(cancel);
            const auto source=fs::absolute(o.files[i]);
            if(!fs::is_regular_file(source))fail(tr("missing")+pathText(source));
            const auto directory=o.output.empty()?source.parent_path():fs::absolute(o.output); fs::create_directories(directory);
            Temporary scratch(directory);
            auto output=uniqueOutput(directory,pathText(source.stem())+L"_"+actionId(o.action),extension);
            auto temporary=scratch.path/textPath(L"result"+extension);
            if(notify)notify({i,0,tr("working"),{}});
            if(o.action==Action::pdf) {
                std::vector<fs::path> pages;
                for(size_t p=0;p<o.files.size();++p) {
                    stopped(cancel); auto file=fs::absolute(o.files[p]);
                    if(kind(file)==Kind::image) {
                        auto jpg=scratch.path/(std::to_string(p)+".jpg"); auto pdf=scratch.path/(std::to_string(p)+".pdf");
                        Options imageOptions=o;imageOptions.width=0;imageOptions.quality=4;
                        imageConversion(imageOptions,file,jpg,cancel);
                        const auto dimensions = runProcess(engine("ffprobe"), {
                            L"-v", L"error", L"-select_streams", L"v:0", L"-show_entries",
                            L"stream=width,height", L"-of", L"csv=p=0", pathText(jpg)
                        }, cancel, {}, 60000);
                        engineSuccess(dimensions);
                        int width = 0, height = 0;
                        char separator = 0;
                        std::istringstream size(dimensions.output);
                        if (!(size >> width >> separator >> height) || separator != ',') fail(tr("failed"));
                        createImagePdf(jpg,pdf,width,height);
                        pages.push_back(pdf);
                    } else pages.push_back(file);
                    if(notify)notify({p,0.5,tr("working"),{}});
                }
                std::vector<std::wstring> arguments{L"--empty", L"--pages"};
                for (const auto& page : pages) arguments.insert(arguments.end(), {pathText(page), L"1-z"});
                arguments.insert(arguments.end(), {L"--", pathText(temporary)});
                // A response file supports large batches. A path with a newline needs a literal argument.
                if (std::none_of(arguments.begin(), arguments.end(), [](const auto& arg) {
                    return arg.find_first_of(L"\r\n") != std::wstring::npos;
                })) {
                    const auto response = scratch.path / "pages.txt";
                    std::ofstream file(response, std::ios::binary);
                    for (const auto& arg : arguments) file << utf8(arg) << '\n';
                    file.close();
                    if (!file) fail(tr("failed"));
                    arguments = {L"@" + pathText(response)};
                }
                engineSuccess(runProcess(engine("qpdf"), arguments, cancel));
            } else if(o.action==Action::image||o.action==Action::frame) imageConversion(o,source,temporary,cancel);
            else if(o.action==Action::mediaInfo) mediaReport(source,temporary,cancel);
            else if(o.action==Action::word||o.action==Action::excel) exportOfficePdf(source,temporary,o.action==Action::excel,cancel);
            else {
                const auto duration=probeDuration(source,cancel);
                const auto convert = [&](const std::vector<std::wstring>& arguments) {
                    std::string pending;
                    engineSuccess(runProcess(engine("ffmpeg"),arguments,cancel,[&](const std::string& chunk) {
                        pending+=chunk; size_t end;
                        while((end=pending.find('\n'))!=std::string::npos) {
                            const auto line=pending.substr(0,end);pending.erase(0,end+1);
                            if(line.starts_with("out_time_us=")&&duration>0&&notify) { try {notify({i,std::clamp(std::stod(line.substr(12))/1000000.0/duration,0.0,0.99),tr("working"),{}});}catch(const std::invalid_argument&){} }
                        }
                    }));
                };
                if (o.action == Action::audio || o.action == Action::remux) {
                    std::vector<std::wstring> arguments{L"-hide_banner",L"-loglevel",L"error",L"-nostdin",L"-n",
                        L"-xerror",L"-progress",L"pipe:1",L"-i",pathText(source)};
                    if (o.action == Action::audio) {
                        arguments.insert(arguments.end(), {L"-map",L"0:a:0"});
                        const auto encoding = audioOutputArguments(outputFormat(o));
                        arguments.insert(arguments.end(), encoding.begin(), encoding.end());
                    } else {
                        arguments.insert(arguments.end(), {L"-map",L"0",L"-c",L"copy"});
                    }
                    arguments.push_back(pathText(temporary));
                    convert(arguments);
                } else {
                    for(int attempt=0;attempt<4;++attempt) {
                        if(fs::exists(temporary))fs::remove(temporary);
                        convert(videoArguments(o,source,temporary,duration,attempt));
                        if(!o.targetMB||fs::file_size(temporary)<=static_cast<uintmax_t>(o.targetMB)*1048576)break;
                        if(attempt==3)fail(tr("sizeFailed"));
                    }
                }
                if(probeDuration(temporary,cancel)<=0) fail(tr("failed"));
            }
            stopped(cancel);
            if(extension==L".pdf") engineSuccess(runProcess(engine("qpdf"),{L"--check",pathText(temporary)},cancel));
            commitOutput(temporary,output); ++result.succeeded;result.outputs.push_back(output);
            if(notify) {
                if(o.action==Action::pdf)for(size_t p=0;p<o.files.size();++p)notify({p,1,tr("done"),output});
                else notify({i,1,tr("done"),output});
            }
        } catch(const Cancelled&) {result.cancelled=true;if(notify)notify({i,0,tr("cancelled"),{}});break;}
        catch(const std::exception& e) {++result.failed;if(notify)notify({i,1,tr("failed")+L": "+wide(e.what()),{},true});}
    }
    return result;
}
}
