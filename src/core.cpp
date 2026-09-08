#include "core.h"
#include <shlobj.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>

namespace ca {
namespace {
[[noreturn]] void fail(const std::wstring& message) { throw std::runtime_error(utf8(message)); }
std::wstring lower(std::wstring s) { for (auto& c : s) c = towlower(c); return s; }
bool contains(const wchar_t* list, const std::wstring& extension) { return std::wstring(list).find(L"|" + extension + L"|") != std::wstring::npos; }
std::wstring guid() { GUID id; check(CoCreateGuid(&id)); wchar_t s[40]; StringFromGUID2(id, s, 40); return s; }
void stopped(const std::atomic_bool& cancel) { if (cancel) throw Cancelled(); }
void replaceAll(std::wstring& value, const std::wstring& from, const std::wstring& to) {
    if (from.empty()) return;
    size_t at = 0;
    while ((at = value.find(from, at)) != std::wstring::npos) { value.replace(at, from.size(), to); at += to.size(); }
}
struct Temporary {
    fs::path path;
    explicit Temporary(const fs::path& parent) : path(parent / (L".context-actions-" + guid())) { fs::create_directory(path); }
    ~Temporary() { std::error_code ec; fs::remove_all(path, ec); }
};
fs::path engine(const wchar_t* name) {
    const auto path = appDirectory() / L"engines" / name;
    if (!fs::is_regular_file(path)) fail(tr("engine") + path.wstring());
    return path;
}
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
std::wstring number(double value) { std::wostringstream out; out.imbue(std::locale::classic()); out << std::fixed << std::setprecision(3) << value; return out.str(); }
void engineSuccess(const ProcessResult& process) {
    if (process.code != 0) fail(tr("failed") + L": " + wide(process.output));
}
double probeDuration(const fs::path& input, std::atomic_bool& cancel) {
    auto result = runProcess(engine(L"ffprobe.exe"), {L"-v",L"error",L"-show_entries",L"format=duration",L"-of",L"default=noprint_wrappers=1:nokey=1",input.wstring()}, cancel, {}, 60000);
    engineSuccess(result);
    try { const double d = std::stod(result.output); return std::isfinite(d) && d > 0 ? d : 0; } catch (...) { return 0; }
}
void imageConversion(const Options& options, const fs::path& source, const fs::path& output, std::atomic_bool& cancel) {
    std::vector<std::wstring> args{L"-hide_banner",L"-loglevel",L"error",L"-nostdin",L"-n",L"-i",source.wstring()};
    auto filter = options.width ? L"scale=w='min(iw," + std::to_wstring(options.width) + L")':h=-1:flags=lanczos" : L"null";
    auto ext = lower(output.extension().wstring());
    if (ext == L".jpg" || ext == L".jpeg" || ext == L".bmp") {
        filter += L",format=rgba,split[fg][bg];[bg]lutrgb=r=255:g=255:b=255,format=rgb24[white];[white][fg]overlay=shortest=1:format=auto,format=rgb24";
        args.insert(args.end(), {L"-filter_complex",filter});
    } else args.insert(args.end(), {L"-vf",filter});
    args.insert(args.end(), {L"-frames:v",L"1",L"-map_metadata",L"-1",L"-update",L"1"});
    if (ext == L".webp") args.insert(args.end(), {L"-c:v",L"libwebp",L"-quality",std::to_wstring(100-options.quality)});
    if (ext == L".jpg" || ext == L".jpeg") args.insert(args.end(), {L"-q:v",std::to_wstring(2+options.quality/4)});
    args.push_back(output.wstring());
    engineSuccess(runProcess(engine(L"ffmpeg.exe"), args, cancel));
    engineSuccess(runProcess(engine(L"ffmpeg.exe"), {L"-v",L"error",L"-nostdin",L"-i",output.wstring(),L"-frames:v",L"1",L"-f",L"null",L"-"}, cancel, {}, 60000));
}
}

void check(HRESULT hr) { if (FAILED(hr)) fail(systemError(static_cast<DWORD>(hr))); }
std::wstring systemError(DWORD code) {
    wchar_t* buffer = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,code,0,reinterpret_cast<LPWSTR>(&buffer),0,nullptr);
    std::wstring result = buffer ? buffer : L"Windows error " + std::to_wstring(code);
    if (buffer) LocalFree(buffer);
    return result;
}
std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (!n) throw std::runtime_error("Invalid UTF-8");
    std::wstring out(n,L'\0'); MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n); return out;
}
std::string utf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);
    std::string out(n,'\0'); WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr); return out;
}
std::wstring quote(const std::wstring& arg) {
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (auto c : arg) {
        if (c == L'\\') { ++slashes; continue; }
        out.append(slashes * (c == L'"' ? 2 : 1), L'\\'); slashes = 0;
        if (c == L'"') out += L'\\';
        out += c;
    }
    out.append(slashes*2,L'\\'); return out + L'"';
}
fs::path appDirectory() {
    wchar_t path[32768];
    if (!GetModuleFileNameW(nullptr,path,32768)) fail(systemError(GetLastError()));
    return fs::path(path).parent_path();
}
Kind kind(const fs::path& path) {
    const auto ext = lower(path.extension().wstring());
    if (contains(L"|.mp4|.mkv|.mov|.avi|.webm|.m4v|.wmv|.mts|.m2ts|",ext)) return Kind::video;
    if (contains(L"|.jpg|.jpeg|.png|.webp|.bmp|.tif|.tiff|.gif|",ext)) return Kind::image;
    if (ext == L".pdf") return Kind::pdf;
    if (contains(L"|.docx|.doc|.odt|.rtf|.txt|",ext)) return Kind::document;
    if (contains(L"|.xlsx|.xls|.ods|.csv|",ext)) return Kind::sheet;
    return Kind::other;
}
const char* actionKey(Action a) {
    static const char* keys[]{"more","compress","h264","h265","telegram","image","pdf","rename","word","excel","documents","sheets"};
    const auto i = static_cast<size_t>(a);
    if (i >= std::size(keys)) fail(tr("invalid"));
    return keys[i];
}
std::wstring actionId(Action action) { return wide(actionKey(action)); }
Action parseAction(const std::wstring& value) {
    for (int i=0;i<=static_cast<int>(Action::sheets);++i) if (actionId(static_cast<Action>(i)) == value) return static_cast<Action>(i);
    fail(tr("invalid"));
}
std::vector<Action> catalog() { return {Action::compress,Action::h264,Action::h265,Action::telegram,Action::image,Action::pdf,Action::rename,Action::word,Action::excel,Action::documents,Action::sheets}; }
bool accepts(Action action, const std::vector<fs::path>& files) {
    if (action == Action::more) return true;
    if (files.empty()) return false;
    return std::all_of(files.begin(),files.end(),[action](const auto& file) {
        auto k = kind(file);
        switch(action) {
        case Action::compress: case Action::h264: case Action::h265: case Action::telegram: return k == Kind::video;
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
        if (officeCapability(action == Action::excel) == Capability::unavailable) return tr(action == Action::excel ? "needExcel" : "needWord");
    return {};
}
void validate(const Options& o) {
    const auto reason = unavailableReason(o.action,o.files);
    if (!reason.empty()) fail(reason);
    if (o.action == Action::more || o.files.size()>100000 || o.quality<0 || o.quality>51 || o.width<0 || o.width>16384 || o.width==1 || o.fps<0 || o.fps>240 || o.targetMB<0 || o.targetMB>1048576 || o.start<0 || o.start>1000000000) fail(tr("invalid"));
    if(o.action==Action::rename||o.action==Action::pdf)
        for (const auto& path : o.files) if (!fs::is_regular_file(path)) fail(tr("missing")+path.wstring());
    if (!o.output.empty() && fs::exists(o.output) && !fs::is_directory(o.output)) fail(tr("invalid"));
    if (o.action == Action::image && !contains(L"|png|jpg|webp|bmp|tiff|gif|",lower(o.format))) fail(tr("invalid"));
    if (o.action == Action::image || o.action == Action::pdf) {
        for (const auto& path : o.files)
            if (!o.firstFrame && contains(L"|.gif|.tif|.tiff|.webp|",lower(path.extension().wstring()))) fail(tr("frameRequired"));
    }
}
void saveOptions(const Options& o, const fs::path& path) {
    std::ofstream file(path,std::ios::binary|std::ios::trunc); file.write("CAJ1",4);
    for (const auto& s : {actionId(o.action),o.output.wstring(),o.format,o.pattern,o.prefix,o.suffix,o.find,o.replace}) writeString(file,s);
    const int values[]{o.quality,o.width,o.fps,o.targetMB,o.mute?1:0,o.firstFrame?1:0,o.start,static_cast<int>(o.files.size())};
    file.write(reinterpret_cast<const char*>(values),sizeof(values));
    for (const auto& p : o.files) writeString(file,fs::absolute(p).wstring());
    file.flush(); if (!file) fail(tr("failed"));
}
Options readJob(const fs::path& path) {
    if (fs::file_size(path)>128*1024*1024) fail(tr("invalid"));
    std::ifstream file(path,std::ios::binary); char magic[4]{}; file.read(magic,4);
    if (memcmp(magic,"CAJ1",4)) fail(tr("invalid"));
    Options o; o.action=parseAction(readString(file)); o.output=readString(file); o.format=readString(file); o.pattern=readString(file);
    o.prefix=readString(file); o.suffix=readString(file); o.find=readString(file); o.replace=readString(file);
    int v[8]{}; file.read(reinterpret_cast<char*>(v),sizeof(v));
    if (!file || v[7]<0 || v[7]>100000 || (v[4]!=0 && v[4]!=1) || (v[5]!=0 && v[5]!=1)) fail(tr("invalid"));
    o.quality=v[0];o.width=v[1];o.fps=v[2];o.targetMB=v[3];o.mute=v[4]!=0;o.firstFrame=v[5]!=0;o.start=v[6];
    for (int i=0;i<v[7];++i) o.files.emplace_back(readString(file));
    if (file.peek()!=std::char_traits<char>::eof()) fail(tr("invalid"));
    return o;
}
fs::path writeJob(const Options& o) {
    const auto directory = dataDirectory()/L"jobs"; fs::create_directories(directory);
    const auto path=directory/(guid()+L".caj"); saveOptions(o,path); return path;
}
void consumeJob(const fs::path& path) {
    // Only internally generated jobs may be consumed; an arbitrary --job file is never deleted.
    if (fs::weakly_canonical(path.parent_path())==fs::weakly_canonical(dataDirectory()/L"jobs") && path.extension()==L".caj") fs::remove(path);
}
void launch(const fs::path& executable, const std::vector<std::wstring>& args) {
    std::wstring line=quote(executable.wstring()); for (const auto& arg:args) line+=L" "+quote(arg);
    if (line.size()>=32767) fail(tr("invalid"));
    STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(),line.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,executable.parent_path().c_str(),&startup,&process)) fail(systemError(GetLastError()));
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
}
ProcessResult runProcess(const fs::path& exe, const std::vector<std::wstring>& args, std::atomic_bool& cancel,
    const std::function<void(const std::string&)>& onOutput, DWORD timeoutMs) {
    stopped(cancel);
    std::wstring line=quote(exe.wstring()); for (const auto& arg:args) line+=L" "+quote(arg);
    if (line.size()>=32767) fail(tr("invalid"));
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE}; HANDLE readRaw=nullptr,writeRaw=nullptr;
    if (!CreatePipe(&readRaw,&writeRaw,&security,0)) fail(systemError(GetLastError()));
    Handle reader(readRaw),writer(writeRaw),nullInput(CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr));
    SetHandleInformation(reader.value,HANDLE_FLAG_INHERIT,0);
    Handle job(CreateJobObjectW(nullptr,nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{}; limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job.value || !SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits))) fail(systemError(GetLastError()));
    STARTUPINFOW startup{sizeof(startup)}; startup.dwFlags=STARTF_USESTDHANDLES;startup.hStdInput=nullInput.value;startup.hStdOutput=writer.value;startup.hStdError=writer.value;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(exe.c_str(),line.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,exe.parent_path().c_str(),&startup,&process)) fail(systemError(GetLastError()));
    Handle child(process.hProcess),thread(process.hThread);
    if (!AssignProcessToJobObject(job.value,child.value)) { const auto error=GetLastError(); TerminateProcess(child.value,1); fail(systemError(error)); }
    ResumeThread(thread.value); CloseHandle(writer.value);writer.value=nullptr;
    std::string output; const auto start=GetTickCount64();
    for (;;) {
        DWORD available=0;
        while (PeekNamedPipe(reader.value,nullptr,0,nullptr,&available,nullptr) && available) {
            char data[4096];DWORD count=0;
            if (!ReadFile(reader.value,data,std::min<DWORD>(sizeof(data),available),&count,nullptr) || !count) break;
            std::string chunk(data,count); if(onOutput) onOutput(chunk);
            output+=chunk; if(output.size()>65536) output.erase(0,output.size()-65536);
        }
        if (WaitForSingleObject(child.value,20)==WAIT_OBJECT_0) {
            // Drain bytes written between the previous pipe read and process exit.
            DWORD availableAfter=0; if (PeekNamedPipe(reader.value,nullptr,0,nullptr,&availableAfter,nullptr) && availableAfter) continue;
            break;
        }
        if (cancel || (timeoutMs!=INFINITE && GetTickCount64()-start>timeoutMs)) {
            TerminateJobObject(job.value,ERROR_CANCELLED); WaitForSingleObject(child.value,5000);
            stopped(cancel); fail(L"Operation timed out.");
        }
    }
    stopped(cancel); DWORD code=1; GetExitCodeProcess(child.value,&code); return {code,output};
}
std::vector<std::wstring> videoArguments(const Options& o, const fs::path& input, const fs::path& output, double duration, int attempt) {
    bool hevc=o.action==Action::h265, telegram=o.action==Action::telegram;
    std::vector<std::wstring> args{L"-hide_banner",L"-loglevel",L"error",L"-nostdin",L"-n",L"-progress",L"pipe:1",L"-i",input.wstring(),L"-map",L"0:v:0"};
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
    args.insert(args.end(),{L"-movflags",L"+faststart",L"-map_metadata",L"-1",L"-sn",L"-dn",output.wstring()});
    return args;
}
fs::path uniqueOutput(const fs::path& directory, const std::wstring& stem, const std::wstring& extension) {
    auto candidate=directory/(stem+extension);
    for(int n=2;fs::exists(candidate);++n) candidate=directory/(stem+L" ("+std::to_wstring(n)+L")"+extension);
    return candidate;
}
void commitOutput(const fs::path& temp, const fs::path& destination) {
    if(!fs::is_regular_file(temp) || fs::file_size(temp)==0) fail(tr("failed"));
    if(!MoveFileExW(temp.c_str(),destination.c_str(),MOVEFILE_WRITE_THROUGH)) fail(tr("conflict")+destination.wstring()+L"\n"+systemError(GetLastError()));
}
std::vector<Rename> previewRename(const Options& o) {
    if(o.pattern.empty()) fail(tr("invalid"));
    std::vector<Rename> result; std::set<std::wstring> targets, sources;
    for(size_t i=0;i<o.files.size();++i) {
        auto source=fs::absolute(o.files[i]);
        auto name=source.stem().wstring(); replaceAll(name,o.find,o.replace);
        auto pattern=o.pattern; replaceAll(pattern,L"{name}",name); replaceAll(pattern,L"{n}",std::to_wstring(o.start+i));
        name=o.prefix+pattern+o.suffix+source.extension().wstring();
        auto base=lower(fs::path(name).stem().wstring()); auto dot=base.find(L'.'); if(dot!=std::wstring::npos) base.resize(dot);
        bool reserved=base==L"con"||base==L"prn"||base==L"aux"||base==L"nul"||base==L"clock$"||(base.size()==4 && (base.starts_with(L"com")||base.starts_with(L"lpt")) && base[3]>=L'1' && base[3]<=L'9');
        if(name.empty()||name.size()>255||name.back()==L'.'||name.back()==L' '||name.find_first_of(L"<>:\"/\\|?*{}")!=std::wstring::npos||std::any_of(name.begin(),name.end(),[](auto c){return c<32;})||reserved) fail(tr("conflict")+name);
        auto to=source.parent_path()/name;
        if(!targets.insert(lower(to.wstring())).second || !sources.insert(lower(source.wstring())).second) fail(tr("conflict")+name);
        if(lower(source.wstring())!=lower(to.wstring()) && fs::exists(to)) fail(tr("conflict")+to.wstring());
        result.push_back({source,to});
    }
    return result;
}
void applyRename(const std::vector<Rename>& changes, std::atomic_bool& cancel, bool saveUndo) {
    // ponytail: reject swaps and cycles; add staged renames only when that workflow is needed.
    for(const auto& change:changes) {
        if(!fs::is_regular_file(change.from)) fail(tr("missing")+change.from.wstring());
        if(lower(change.from.wstring())!=lower(change.to.wstring()) && fs::exists(change.to)) fail(tr("conflict")+change.to.wstring());
    }
    fs::create_directories(dataDirectory());
    const auto journal=dataDirectory()/L"rename-undo.txt";
    const auto pending=dataDirectory()/L"rename-pending.txt";
    if(fs::exists(pending)) fail(L"An interrupted rename needs recovery; see " + pending.wstring());
    if(saveUndo) {
        std::ofstream log(pending,std::ios::binary);
        for(const auto& c:changes) log<<std::quoted(utf8(c.from.wstring()))<<' '<<std::quoted(utf8(c.to.wstring()))<<' '<<fs::file_size(c.from)<<' '<<fs::last_write_time(c.from).time_since_epoch().count()<<'\n';
        log.flush(); if(!log) fail(tr("failed"));
    }
    size_t moved=0;
    try {
        for(const auto& c:changes) {
            stopped(cancel);
            if(c.from!=c.to && !MoveFileExW(c.from.c_str(),c.to.c_str(),MOVEFILE_WRITE_THROUGH)) fail(systemError(GetLastError()));
            ++moved;
        }
        if(saveUndo && !MoveFileExW(pending.c_str(),journal.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) fail(systemError(GetLastError()));
    } catch(...) {
        bool restored=true;
        while(moved) { const auto& c=changes[--moved]; if(c.from!=c.to && !MoveFileExW(c.to.c_str(),c.from.c_str(),MOVEFILE_WRITE_THROUGH)) restored=false; }
        if(saveUndo && restored) fs::remove(pending);
        if(!restored) fail(L"Rename rollback incomplete. Recovery journal: "+pending.wstring());
        throw;
    }
}
void undoRename(std::atomic_bool& cancel) {
    auto journal=dataDirectory()/L"rename-undo.txt"; std::ifstream file(journal,std::ios::binary);
    if(!file) fail(tr("invalid"));
    std::vector<Rename> reverse; std::string from,to; uintmax_t size; long long modified;
    while(file>>std::quoted(from)>>std::quoted(to)>>size>>modified) {
        fs::path source=wide(to);
        if(!fs::is_regular_file(source)||fs::file_size(source)!=size||fs::last_write_time(source).time_since_epoch().count()!=modified) fail(tr("conflict")+source.wstring());
        reverse.push_back({source,fs::path(wide(from))});
    }
    if(!file.eof()||reverse.empty()) fail(tr("invalid"));
    file.close();applyRename(reverse,cancel,false); fs::remove(journal);
}
Result execute(const Options& o, std::atomic_bool& cancel, const Progress& notify) {
    validate(o); Result result;
    if(o.action==Action::rename) {
        auto changes=previewRename(o); applyRename(changes,cancel);
        for(size_t i=0;i<changes.size();++i) {result.outputs.push_back(changes[i].to);if(notify)notify({i,1,tr("done"),changes[i].to});++result.succeeded;}
        return result;
    }
    const size_t count=o.action==Action::pdf?1:o.files.size();
    for(size_t i=0;i<count;++i) {
        try {
            stopped(cancel);
            const auto source=fs::absolute(o.files[i]);
            if(!fs::is_regular_file(source))fail(tr("missing")+source.wstring());
            const auto directory=o.output.empty()?source.parent_path():fs::absolute(o.output); fs::create_directories(directory);
            Temporary scratch(directory);
            auto extension=o.action==Action::image?L"."+o.format:(o.action==Action::pdf||o.action==Action::word||o.action==Action::excel?L".pdf":L".mp4");
            auto output=uniqueOutput(directory,source.stem().wstring()+L"_"+actionId(o.action),extension);
            auto temporary=scratch.path/(L"result"+extension);
            if(notify)notify({i,0,tr("working"),{}});
            if(o.action==Action::pdf) {
                std::vector<fs::path> pages;
                for(size_t p=0;p<o.files.size();++p) {
                    stopped(cancel); auto file=fs::absolute(o.files[p]);
                    if(kind(file)==Kind::image) {
                        auto jpg=scratch.path/(std::to_wstring(p)+L".jpg"); auto pdf=scratch.path/(std::to_wstring(p)+L".pdf");
                        Options imageOptions=o;imageOptions.width=0;imageOptions.quality=4;
                        imageConversion(imageOptions,file,jpg,cancel); createImagePdf(jpg,pdf); pages.push_back(pdf);
                    } else pages.push_back(file);
                    if(notify)notify({p,0.5,tr("working"),{}});
                }
                auto response=scratch.path/L"pages.txt"; std::ofstream args(response,std::ios::binary);
                args<<"--empty\n--pages\n"; for(const auto& p:pages)args<<utf8(p.wstring())<<"\n1-z\n";
                args<<"--\n"<<utf8(temporary.wstring())<<'\n'; args.close(); if(!args)fail(tr("failed"));
                engineSuccess(runProcess(engine(L"qpdf.exe"),{L"@"+response.wstring()},cancel));
            } else if(o.action==Action::image) imageConversion(o,source,temporary,cancel);
            else if(o.action==Action::word||o.action==Action::excel) exportOfficePdf(source,temporary,o.action==Action::excel,cancel);
            else {
                auto duration=probeDuration(source,cancel); std::string pending;
                for(int attempt=0;attempt<4;++attempt) {
                    if(fs::exists(temporary))fs::remove(temporary);
                    engineSuccess(runProcess(engine(L"ffmpeg.exe"),videoArguments(o,source,temporary,duration,attempt),cancel,[&](const std::string& chunk) {
                        pending+=chunk; size_t end;
                        while((end=pending.find('\n'))!=std::string::npos) {
                            const auto line=pending.substr(0,end);pending.erase(0,end+1);
                            if(line.starts_with("out_time_us=")&&duration>0&&notify) { try {notify({i,std::clamp(std::stod(line.substr(12))/1000000.0/duration,0.0,0.99),tr("working"),{}});}catch(const std::invalid_argument&){} }
                        }
                    }));
                    if(!o.targetMB||fs::file_size(temporary)<=static_cast<uintmax_t>(o.targetMB)*1048576)break;
                    if(attempt==3)fail(tr("sizeFailed"));
                }
                if(probeDuration(temporary,cancel)<=0) fail(tr("failed"));
            }
            stopped(cancel);
            if(extension==L".pdf") engineSuccess(runProcess(engine(L"qpdf.exe"),{L"--check",temporary.wstring()},cancel));
            commitOutput(temporary,output); ++result.succeeded;result.outputs.push_back(output);
            if(notify) {
                if(o.action==Action::pdf)for(size_t p=0;p<o.files.size();++p)notify({p,1,tr("done"),output});
                else notify({i,1,tr("done"),output});
            }
        } catch(const Cancelled&) {result.cancelled=true;if(notify)notify({i,0,tr("cancelled"),{}});break;}
        catch(const std::exception& e) {++result.failed;if(notify)notify({i,1,tr("failed")+L": "+wide(e.what()),{}});}
    }
    return result;
}
}
