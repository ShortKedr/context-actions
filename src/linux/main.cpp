#include "core.h"

#include <gtk/gtk.h>

#include <algorithm>
#include <iostream>
#include <map>
#include <mutex>
#include <numeric>
#include <optional>
#include <thread>
#include <utility>

namespace {
using ca::Action;
constexpr auto applicationId = "io.github.ShortKedr.ContextActions";

std::string label(const char* key) {
    return ca::utf8(ca::tr(key));
}

std::string displayPath(const ca::fs::path& path) {
    auto* name = g_filename_display_name(path.c_str());
    std::string result = name;
    g_free(name);
    return result;
}

ca::fs::path localPath(GFile* file) {
    auto* name = g_file_get_path(file);
    if (!name) return {};
    ca::fs::path result(name);
    g_free(name);
    return result;
}

void setText(GtkWidget* widget, const std::string& text) {
    gtk_label_set_text(GTK_LABEL(widget), text.c_str());
}

GtkWidget* leftLabel(const std::string& text = {}) {
    auto* widget = gtk_label_new(text.c_str());
    gtk_label_set_xalign(GTK_LABEL(widget), 0);
    return widget;
}

void margins(GtkWidget* widget, int value) {
    gtk_widget_set_margin_top(widget, value);
    gtk_widget_set_margin_bottom(widget, value);
    gtk_widget_set_margin_start(widget, value);
    gtk_widget_set_margin_end(widget, value);
}

GtkWidget* scroller(GtkWidget* child) {
    auto* widget = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(widget), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(widget), child);
    gtk_widget_set_vexpand(widget, true);
    return widget;
}

void field(GtkWidget* box, const char* key, GtkWidget* widget) {
    auto* caption = leftLabel(label(key));
    gtk_label_set_wrap(GTK_LABEL(caption), true);
    gtk_label_set_mnemonic_widget(GTK_LABEL(caption), widget);
    gtk_box_append(GTK_BOX(box), caption);
    gtk_box_append(GTK_BOX(box), widget);
}

GtkWidget* spin(GtkWidget* box, const char* key, int value, int maximum) {
    auto* widget = gtk_spin_button_new_with_range(0, maximum, 1);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(widget), true);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(widget), value);
    field(box, key, widget);
    return widget;
}

GtkWidget* entry(GtkWidget* box, const char* key, const std::string& value) {
    auto* widget = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(widget), value.c_str());
    field(box, key, widget);
    return widget;
}

bool isVideo(Action action) {
    return action == Action::compress || action == Action::h264 ||
        action == Action::h265 || action == Action::telegram;
}

class Workspace {
public:
    explicit Workspace(ca::CommandLine command)
        : files_(std::move(command.options.files)), initial_(std::move(command.options)),
          immediate_(command.immediate), actions_(ca::catalog()) {}

    ~Workspace() {
        cancelled_ = true;
        if (updateSource_) g_source_remove(updateSource_);
        if (worker_.joinable()) worker_.join();
        dismissChooser();
    }

    void activate(GtkApplication* application) {
        if (!window_) build(application);
        gtk_window_present(window_);
        if (std::exchange(immediate_, false)) start();
    }

    void reportError(const std::exception& error) {
        if (status_) setText(status_, error.what());
        else std::cerr << error.what() << '\n';
    }

private:
    template<void (Workspace::*handler)()>
    GtkWidget* button(GtkWidget* box, const char* key) {
        auto* widget = gtk_button_new_with_label(label(key).c_str());
        g_signal_connect(widget, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            auto& self = *static_cast<Workspace*>(data);
            try { (self.*handler)(); }
            catch (const std::exception& error) { self.reportError(error); }
        }), this);
        gtk_box_append(GTK_BOX(box), widget);
        return widget;
    }

    void build(GtkApplication* application) {
        window_ = GTK_WINDOW(gtk_application_window_new(application));
        gtk_window_set_title(window_, "Context Actions");
        gtk_window_set_icon_name(window_, applicationId);
        gtk_window_set_default_size(window_, 900, 640);

        auto* header = gtk_header_bar_new();
        auto* addBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        add_ = button<&Workspace::chooseFiles>(addBox, "add");
        gtk_header_bar_pack_start(GTK_HEADER_BAR(header), addBox);
        gtk_window_set_titlebar(window_, header);

        auto* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
        margins(root, 16);
        gtk_window_set_child(window_, root);

        auto* panes = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_paned_set_position(GTK_PANED(panes), 500);
        gtk_paned_set_wide_handle(GTK_PANED(panes), true);
        gtk_widget_set_vexpand(panes, true);
        gtk_box_append(GTK_BOX(root), panes);
        buildFiles(panes);
        buildOptions(panes);
        buildFooter(root);

        auto* drop = gtk_drop_target_new(GDK_TYPE_FILE_LIST, GDK_ACTION_COPY);
        g_signal_connect(drop, "drop", G_CALLBACK(+[](GtkDropTarget*, const GValue* value,
                double, double, gpointer data) -> gboolean {
            auto& self = *static_cast<Workspace*>(data);
            if (self.busy_) return false;
            try {
                std::vector<ca::fs::path> paths;
                auto* files = static_cast<GdkFileList*>(g_value_get_boxed(value));
                if (!files) return false;
                for (auto* item = gdk_file_list_get_files(files); item; item = item->next)
                    paths.push_back(localPath(G_FILE(item->data)));
                return self.appendFiles(paths);
            } catch (const std::exception& error) {
                self.reportError(error);
                return false;
            }
        }), this);
        gtk_widget_add_controller(GTK_WIDGET(window_), GTK_EVENT_CONTROLLER(drop));
        g_signal_connect(window_, "close-request", G_CALLBACK(+[](GtkWindow*, gpointer data) -> gboolean {
            auto& self = *static_cast<Workspace*>(data);
            if (self.busy_) {
                self.closeAfterJob_ = true;
                self.cancelJob();
                return true;
            }
            self.dismissChooser();
            self.window_ = nullptr;
            return false;
        }), this);

        rebuildFiles();
        refreshControls();
    }

    void buildFiles(GtkWidget* panes) {
        auto* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        gtk_widget_set_size_request(box, 260, -1);
        gtk_paned_set_start_child(GTK_PANED(panes), box);
        gtk_box_append(GTK_BOX(box), leftLabel(label("file")));

        fileList_ = gtk_list_box_new();
        gtk_list_box_set_selection_mode(GTK_LIST_BOX(fileList_), GTK_SELECTION_MULTIPLE);
        auto* placeholder = leftLabel(label("dropTitle"));
        margins(placeholder, 12);
        gtk_list_box_set_placeholder(GTK_LIST_BOX(fileList_), placeholder);
        auto* scroll = scroller(fileList_);
        gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(scroll), true);
        gtk_box_append(GTK_BOX(box), scroll);

        auto* buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        remove_ = button<&Workspace::removeFiles>(buttons, "remove");
        up_ = button<&Workspace::moveUp>(buttons, "up");
        down_ = button<&Workspace::moveDown>(buttons, "down");
        gtk_box_append(GTK_BOX(box), buttons);
        g_signal_connect(fileList_, "selected-rows-changed", G_CALLBACK(+[](GtkListBox*, gpointer data) {
            static_cast<Workspace*>(data)->refreshSelection();
        }), this);
    }

    void buildOptions(GtkWidget* panes) {
        controls_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        gtk_widget_set_size_request(controls_, 300, -1);
        gtk_widget_set_margin_end(controls_, 8);
        gtk_paned_set_end_child(GTK_PANED(panes), scroller(controls_));

        auto* names = gtk_string_list_new(nullptr);
        for (auto action : actions_) gtk_string_list_append(names, ca::utf8(ca::trAction(action)).c_str());
        action_ = gtk_drop_down_new(G_LIST_MODEL(names), nullptr);
        field(controls_, "action", action_);
        selectAction(initial_.action == Action::more ? suggestedAction() : initial_.action);

        availability_ = leftLabel();
        gtk_label_set_wrap(GTK_LABEL(availability_), true);
        gtk_widget_add_css_class(availability_, "dim-label");
        gtk_box_append(GTK_BOX(controls_), availability_);

        media_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        gtk_box_append(GTK_BOX(controls_), media_);
        format_ = gtk_drop_down_new(nullptr, nullptr);
        if (!initial_.format.empty()) {
            ca::Options formatOptions = initial_;
            formatOptions.action = selectedAction();
            formatSelections_[formatOptions.action] = ca::outputFormat(formatOptions);
        }
        formatField_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        field(formatField_, "format", format_);
        gtk_box_append(GTK_BOX(media_), formatField_);
        dimensions_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        gtk_box_append(GTK_BOX(media_), dimensions_);
        quality_ = spin(dimensions_, "quality", initial_.quality, 51);
        width_ = spin(dimensions_, "width", initial_.width, 16384);

        video_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        gtk_box_append(GTK_BOX(controls_), video_);
        fps_ = spin(video_, "fps", initial_.fps, 240);
        target_ = spin(video_, "target", initial_.targetMB, 1048576);
        mute_ = gtk_check_button_new_with_label(label("mute").c_str());
        gtk_check_button_set_active(GTK_CHECK_BUTTON(mute_), initial_.mute);
        gtk_box_append(GTK_BOX(video_), mute_);

        frame_ = gtk_check_button_new_with_label(label("firstFrame").c_str());
        gtk_check_button_set_active(GTK_CHECK_BUTTON(frame_), initial_.firstFrame);
        gtk_box_append(GTK_BOX(controls_), frame_);

        rename_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        gtk_box_append(GTK_BOX(controls_), rename_);
        pattern_ = entry(rename_, "pattern", ca::utf8(initial_.pattern));
        start_ = spin(rename_, "start", initial_.start, 1000000000);
        auto* advanced = gtk_expander_new(label("advanced").c_str());
        auto* advancedBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        prefix_ = entry(advancedBox, "prefix", ca::utf8(initial_.prefix));
        suffix_ = entry(advancedBox, "suffix", ca::utf8(initial_.suffix));
        find_ = entry(advancedBox, "find", ca::utf8(initial_.find));
        replace_ = entry(advancedBox, "replace", ca::utf8(initial_.replace));
        gtk_expander_set_child(GTK_EXPANDER(advanced), advancedBox);
        gtk_box_append(GTK_BOX(rename_), advanced);
        previewButton_ = button<&Workspace::previewNames>(rename_, "preview");
        button<&Workspace::undo>(rename_, "undo");

        destination_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        gtk_box_append(GTK_BOX(controls_), destination_);
        chosenOutput_ = initial_.output;
        output_ = entry(destination_, "saveTo", chosenOutput_.empty() ? "" : displayPath(chosenOutput_));
        gtk_entry_set_placeholder_text(GTK_ENTRY(output_), label("sameFolder").c_str());
        button<&Workspace::chooseFolder>(destination_, "browse");
        g_signal_connect(output_, "changed", G_CALLBACK(+[](GtkEditable*, gpointer data) {
            static_cast<Workspace*>(data)->chosenOutput_.clear();
        }), this);

        g_signal_connect(action_, "notify::selected", G_CALLBACK(+[](GObject*, GParamSpec*, gpointer data) {
            auto& self = *static_cast<Workspace*>(data);
            self.invalidatePreview();
            self.refreshControls();
        }), this);
        const auto invalidate = +[](GtkWidget*, gpointer data) {
            static_cast<Workspace*>(data)->invalidatePreview();
        };
        for (auto* widget : {pattern_, prefix_, suffix_, find_, replace_})
            g_signal_connect(widget, "changed", G_CALLBACK(invalidate), this);
        g_signal_connect(start_, "changed", G_CALLBACK(invalidate), this);
    }

    void buildFooter(GtkWidget* root) {
        progress_ = gtk_progress_bar_new();
        gtk_widget_set_visible(progress_, false);
        gtk_box_append(GTK_BOX(root), progress_);

        status_ = leftLabel(label("ready"));
        gtk_label_set_wrap(GTK_LABEL(status_), true);
        gtk_label_set_selectable(GTK_LABEL(status_), true);
        gtk_box_append(GTK_BOX(root), status_);

        auto* footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_append(GTK_BOX(root), footer);
        open_ = button<&Workspace::openResult>(footer, "open");
        reveal_ = button<&Workspace::revealResult>(footer, "reveal");
        auto* space = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_set_hexpand(space, true);
        gtk_box_append(GTK_BOX(footer), space);
        cancel_ = button<&Workspace::cancelJob>(footer, "cancel");
        run_ = button<&Workspace::start>(footer, "run");
        gtk_widget_add_css_class(run_, "suggested-action");
    }

    Action selectedAction() const {
        const auto index = gtk_drop_down_get_selected(GTK_DROP_DOWN(action_));
        return index < actions_.size() ? actions_[index] : Action::more;
    }

    Action suggestedAction() const {
        for (auto action : {Action::compress, Action::audio, Action::image, Action::pdf, Action::word,
                Action::excel, Action::mediaInfo})
            if (ca::accepts(action, files_)) return action;
        return files_.empty() ? Action::compress : Action::rename;
    }

    void selectAction(Action action) {
        const auto it = std::find(actions_.begin(), actions_.end(), action);
        if (it != actions_.end())
            gtk_drop_down_set_selected(GTK_DROP_DOWN(action_), static_cast<guint>(it - actions_.begin()));
    }

    void refreshControls() {
        const auto action = selectedAction();
        const bool video = isVideo(action);
        const bool image = action == Action::image;
        const bool rename = action == Action::rename;
        if (action != formatAction_) {
            const auto previous = gtk_drop_down_get_selected(GTK_DROP_DOWN(format_));
            if (previous < formats_.size()) formatSelections_[formatAction_] = formats_[previous];
            formatAction_ = action;
            formats_ = ca::outputFormats(action);
            auto* names = gtk_string_list_new(nullptr);
            for (const auto& format : formats_) gtk_string_list_append(names, ca::utf8(format).c_str());
            gtk_drop_down_set_model(GTK_DROP_DOWN(format_), G_LIST_MODEL(names));
            g_object_unref(names);
            const auto preferred = formatSelections_.find(action);
            guint selected = 0;
            if (preferred != formatSelections_.end()) {
                const auto found = std::find(formats_.begin(), formats_.end(), preferred->second);
                if (found != formats_.end())
                    selected = static_cast<guint>(found - formats_.begin());
            }
            if (!formats_.empty()) gtk_drop_down_set_selected(GTK_DROP_DOWN(format_), selected);
        }
        const auto reason = ca::unavailableReason(action, files_);
        setText(availability_, ca::utf8(reason));
        gtk_widget_set_visible(availability_, !reason.empty());
        gtk_widget_set_visible(media_, video || image || !formats_.empty());
        gtk_widget_set_visible(dimensions_, video || image);
        gtk_widget_set_visible(video_, video);
        gtk_widget_set_visible(formatField_, !formats_.empty());
        gtk_widget_set_visible(frame_, image || action == Action::pdf);
        gtk_widget_set_visible(rename_, rename);
        gtk_widget_set_visible(destination_, !rename);
        gtk_widget_set_sensitive(add_, !busy_);
        gtk_widget_set_sensitive(controls_, !busy_);
        gtk_widget_set_sensitive(run_, !busy_ && reason.empty());
        gtk_widget_set_sensitive(previewButton_, reason.empty());
        gtk_widget_set_sensitive(cancel_, busy_ && !cancelled_);
        refreshSelection();
    }

    std::vector<int> selectedRows() const {
        std::vector<int> indices;
        auto* rows = gtk_list_box_get_selected_rows(GTK_LIST_BOX(fileList_));
        for (auto* node = rows; node; node = node->next)
            indices.push_back(gtk_list_box_row_get_index(GTK_LIST_BOX_ROW(node->data)));
        g_list_free(rows);
        return indices;
    }

    void refreshSelection() {
        if (!run_) return;
        const auto indices = selectedRows();
        gtk_widget_set_sensitive(remove_, !busy_ && !indices.empty());
        const bool one = !busy_ && indices.size() == 1;
        gtk_widget_set_sensitive(up_, one && indices.front() > 0);
        gtk_widget_set_sensitive(down_, one && static_cast<size_t>(indices.front() + 1) < files_.size());
        const bool hasResult = !busy_ && std::any_of(outputs_.begin(), outputs_.end(),
            [](const auto& path) { return !path.empty(); });
        gtk_widget_set_visible(open_, hasResult);
        gtk_widget_set_visible(reveal_, hasResult);
    }

    void rebuildFiles() {
        while (auto* row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(fileList_), 0))
            gtk_list_box_remove(GTK_LIST_BOX(fileList_), GTK_WIDGET(row));
        rowStatus_.clear();
        for (const auto& path : files_) {
            auto* row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
            margins(row, 8);
            auto* name = leftLabel(displayPath(path.filename()));
            gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_MIDDLE);
            gtk_widget_set_tooltip_text(name, displayPath(path).c_str());
            gtk_box_append(GTK_BOX(row), name);
            auto* state = leftLabel(label("ready"));
            gtk_label_set_ellipsize(GTK_LABEL(state), PANGO_ELLIPSIZE_END);
            gtk_widget_add_css_class(state, "dim-label");
            gtk_box_append(GTK_BOX(row), state);
            rowStatus_.push_back(state);
            gtk_list_box_append(GTK_LIST_BOX(fileList_), row);
        }
    }

    void setRowStatus(size_t index, const std::string& text) {
        setText(rowStatus_[index], text);
        gtk_widget_set_tooltip_text(rowStatus_[index], text.c_str());
    }

    void filesChanged() {
        preview_.clear();
        outputs_.clear();
        gtk_widget_set_visible(progress_, false);
        setText(status_, label("ready"));
        rebuildFiles();
        refreshControls();
    }

    bool appendFiles(const std::vector<ca::fs::path>& paths) {
        const auto previousCount = files_.size();
        for (const auto& path : paths) {
            std::error_code error;
            if (!path.empty() && ca::fs::is_regular_file(path, error) &&
                    std::find(files_.begin(), files_.end(), path) == files_.end())
                files_.push_back(path);
        }
        if (files_.size() == previousCount) return false;
        if (!previousCount && !ca::accepts(selectedAction(), files_)) selectAction(suggestedAction());
        filesChanged();
        return true;
    }

    void removeFiles() {
        auto indices = selectedRows();
        std::sort(indices.rbegin(), indices.rend());
        for (int index : indices) files_.erase(files_.begin() + index);
        filesChanged();
    }

    void moveFile(int direction) {
        auto indices = selectedRows();
        if (indices.size() != 1) return;
        const int other = indices.front() + direction;
        if (other < 0 || static_cast<size_t>(other) >= files_.size()) return;
        std::swap(files_[indices.front()], files_[other]);
        filesChanged();
        gtk_list_box_select_row(GTK_LIST_BOX(fileList_), gtk_list_box_get_row_at_index(GTK_LIST_BOX(fileList_), other));
    }

    void moveUp() { moveFile(-1); }
    void moveDown() { moveFile(1); }

    void chooseFiles() { choose(false); }
    void chooseFolder() { choose(true); }

    void choose(bool folder) {
        if (busy_ || chooser_) return;
        chooser_ = gtk_file_chooser_native_new(label(folder ? "saveTo" : "add").c_str(), window_,
            folder ? GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER : GTK_FILE_CHOOSER_ACTION_OPEN,
            label(folder ? "saveTo" : "open").c_str(), label("cancel").c_str());
        gtk_native_dialog_set_modal(GTK_NATIVE_DIALOG(chooser_), true);
        gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(chooser_), !folder);
        g_signal_connect(chooser_, "response", G_CALLBACK(+[](GtkNativeDialog* dialog, int response, gpointer data) {
            auto& self = *static_cast<Workspace*>(data);
            if (response == GTK_RESPONSE_ACCEPT) {
                try { self.acceptChoice(GTK_FILE_CHOOSER(dialog)); }
                catch (const std::exception& error) { self.reportError(error); }
            }
            self.dismissChooser();
        }), this);
        gtk_native_dialog_show(GTK_NATIVE_DIALOG(chooser_));
    }

    void acceptChoice(GtkFileChooser* chooser) {
        if (gtk_file_chooser_get_action(chooser) == GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER) {
            auto* file = gtk_file_chooser_get_file(chooser);
            if (!file) return;
            auto path = localPath(file);
            g_object_unref(file);
            if (!path.empty()) {
                gtk_editable_set_text(GTK_EDITABLE(output_), displayPath(path).c_str());
                chosenOutput_ = std::move(path);
            }
            return;
        }
        auto* list = gtk_file_chooser_get_files(chooser);
        std::vector<ca::fs::path> paths;
        const auto count = g_list_model_get_n_items(list);
        for (guint index = 0; index < count; ++index) {
            auto* file = G_FILE(g_list_model_get_item(list, index));
            paths.push_back(localPath(file));
            g_object_unref(file);
        }
        g_object_unref(list);
        if (!appendFiles(paths) && files_.empty()) setText(status_, label("select"));
    }

    void dismissChooser() {
        if (auto* dialog = std::exchange(chooser_, nullptr)) {
            gtk_native_dialog_destroy(GTK_NATIVE_DIALOG(dialog));
            g_object_unref(dialog);
        }
    }

    ca::Options options() const {
        auto text = [](GtkWidget* widget) { return ca::wide(gtk_editable_get_text(GTK_EDITABLE(widget))); };
        auto number = [](GtkWidget* widget) {
            gtk_spin_button_update(GTK_SPIN_BUTTON(widget));
            return gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
        };
        auto checked = [](GtkWidget* widget) { return gtk_check_button_get_active(GTK_CHECK_BUTTON(widget)); };
        ca::Options result;
        result.files = files_;
        result.action = selectedAction();
        result.output = chosenOutput_.empty() ? ca::textPath(text(output_)) : chosenOutput_;
        result.quality = number(quality_);
        result.width = number(width_);
        result.fps = number(fps_);
        result.targetMB = number(target_);
        result.mute = checked(mute_);
        result.firstFrame = checked(frame_);
        const auto formatIndex = gtk_drop_down_get_selected(GTK_DROP_DOWN(format_));
        if (formatIndex < formats_.size()) result.format = formats_[formatIndex];
        result.pattern = text(pattern_);
        result.start = number(start_);
        result.prefix = text(prefix_);
        result.suffix = text(suffix_);
        result.find = text(find_);
        result.replace = text(replace_);
        return result;
    }

    void invalidatePreview() {
        if (preview_.empty()) return;
        preview_.clear();
        for (size_t index = 0; index < rowStatus_.size(); ++index) setRowStatus(index, label("ready"));
    }

    void previewNames() {
        const auto job = options();
        ca::validate(job);
        preview_ = ca::previewRename(job);
        for (size_t index = 0; index < preview_.size(); ++index)
            setRowStatus(index, "→ " + displayPath(preview_[index].to.filename()));
        setText(status_, label("ready"));
    }

    void start() {
        if (busy_) return;
        auto job = options();
        ca::validate(job);
        if (job.action == Action::rename) {
            auto names = ca::previewRename(job);
            const auto same = [](const ca::Rename& a, const ca::Rename& b) { return a.from == b.from && a.to == b.to; };
            if (!std::equal(names.begin(), names.end(), preview_.begin(), preview_.end(), same))
                throw std::runtime_error(label("previewRequired"));
        }
        startJob(std::move(job), false);
    }

    void undo() {
        if (!busy_) startJob({}, true);
    }

    void startJob(ca::Options job, bool undo) {
        cancelled_ = false;
        pending_.clear();
        fractions_.assign(files_.size(), 0);
        outputs_.assign(files_.size(), {});
        result_.reset();
        workerError_.clear();
        for (size_t index = 0; index < rowStatus_.size(); ++index) setRowStatus(index, label("ready"));
        preview_.clear();

        worker_ = std::thread([this, job = std::move(job), undo] {
            ca::Result result;
            std::string error;
            try {
                if (undo) ca::undoRename(cancelled_);
                else result = ca::execute(job, cancelled_, [this](const ca::Update& update) {
                    std::lock_guard lock(mutex_);
                    pending_[update.index] = update;
                });
            } catch (const ca::Cancelled&) {
                result.cancelled = true;
            } catch (const std::exception& exception) {
                result.failed = 1;
                error = exception.what();
            }
            std::lock_guard lock(mutex_);
            workerError_ = std::move(error);
            result_ = std::move(result);
        });
        undoing_ = undo;
        busy_ = true;
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_), 0);
        gtk_widget_set_visible(progress_, true);
        setText(status_, label("working"));
        refreshControls();
        updateSource_ = g_timeout_add(50, +[](gpointer data) -> gboolean {
            return static_cast<Workspace*>(data)->consumeUpdates();
        }, this);
    }

    gboolean consumeUpdates() {
        std::optional<ca::Result> result;
        std::string error;
        std::map<size_t, ca::Update> updates;
        {
            std::lock_guard lock(mutex_);
            updates.swap(pending_);
            result = std::exchange(result_, std::nullopt);
            error = std::move(workerError_);
        }
        for (const auto& [index, update] : updates) {
            if (index >= rowStatus_.size()) continue;
            setRowStatus(index, ca::utf8(update.status));
            fractions_[index] = update.progress;
            if (!update.output.empty()) outputs_[index] = update.output;
        }
        if (undoing_) gtk_progress_bar_pulse(GTK_PROGRESS_BAR(progress_));
        else if (!fractions_.empty()) {
            const auto total = std::accumulate(fractions_.begin(), fractions_.end(), 0.0) /
                static_cast<double>(fractions_.size());
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_), std::clamp(total, 0.0, 1.0));
        }
        if (!result) return G_SOURCE_CONTINUE;

        updateSource_ = 0;
        worker_.join();
        busy_ = false;
        if (closeAfterJob_) {
            gtk_window_close(window_);
            return G_SOURCE_REMOVE;
        }
        if (!result->failed && !result->cancelled) {
            if (undoing_) {
                files_.clear();
                filesChanged();
            } else if (selectedAction() == Action::rename) {
                files_ = result->outputs;
                rebuildFiles();
                for (size_t index = 0; index < rowStatus_.size(); ++index) setRowStatus(index, label("done"));
            }
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_), 1);
        }
        auto summary = ca::tr(result->cancelled ? "cancelled" : "done");
        if (!undoing_ && error.empty())
            summary += L" · " + std::to_wstring(result->succeeded) + L" " + ca::tr("succeeded") +
                L" · " + std::to_wstring(result->failed) + L" " + ca::tr("failed");
        setText(status_, error.empty() ? ca::utf8(summary) : error);
        refreshControls();
        return G_SOURCE_REMOVE;
    }

    void cancelJob() {
        cancelled_ = true;
        gtk_widget_set_sensitive(cancel_, false);
        setText(status_, label("cancelling"));
    }

    void openResult() { launchResult(false); }
    void revealResult() { launchResult(true); }

    void launchResult(bool folder) {
        ca::fs::path chosen;
        for (int index : selectedRows()) {
            if (static_cast<size_t>(index) < outputs_.size() && !outputs_[index].empty()) {
                chosen = outputs_[index];
                break;
            }
        }
        if (chosen.empty()) {
            const auto it = std::find_if(outputs_.begin(), outputs_.end(), [](const auto& path) { return !path.empty(); });
            if (it == outputs_.end()) return;
            chosen = *it;
        }
        if (folder) chosen = chosen.parent_path();
        auto* file = g_file_new_for_path(chosen.c_str());
        auto* uri = g_file_get_uri(file);
        GError* error = nullptr;
        g_app_info_launch_default_for_uri(uri, nullptr, &error);
        g_free(uri);
        g_object_unref(file);
        if (error) {
            std::string message = error->message;
            g_error_free(error);
            throw std::runtime_error(message);
        }
    }

    std::vector<ca::fs::path> files_;
    ca::Options initial_;
    ca::fs::path chosenOutput_;
    bool immediate_;
    std::vector<Action> actions_;
    Action formatAction_ = Action::more;
    std::vector<std::wstring> formats_;
    std::map<Action, std::wstring> formatSelections_;
    GtkWindow* window_ = nullptr;
    GtkFileChooserNative* chooser_ = nullptr;
    GtkWidget *fileList_ = nullptr, *add_ = nullptr, *remove_ = nullptr, *up_ = nullptr, *down_ = nullptr;
    GtkWidget *controls_ = nullptr, *action_ = nullptr, *availability_ = nullptr, *media_ = nullptr;
    GtkWidget *formatField_ = nullptr, *format_ = nullptr, *dimensions_ = nullptr, *quality_ = nullptr, *width_ = nullptr;
    GtkWidget *video_ = nullptr, *fps_ = nullptr, *target_ = nullptr, *mute_ = nullptr, *frame_ = nullptr;
    GtkWidget *rename_ = nullptr, *pattern_ = nullptr, *start_ = nullptr, *prefix_ = nullptr;
    GtkWidget *suffix_ = nullptr, *find_ = nullptr, *replace_ = nullptr, *previewButton_ = nullptr;
    GtkWidget *destination_ = nullptr, *output_ = nullptr, *progress_ = nullptr, *status_ = nullptr;
    GtkWidget *open_ = nullptr, *reveal_ = nullptr, *cancel_ = nullptr, *run_ = nullptr;
    std::vector<GtkWidget*> rowStatus_;
    std::vector<ca::Rename> preview_;
    std::vector<ca::fs::path> outputs_;
    std::vector<double> fractions_;
    bool busy_ = false, undoing_ = false, closeAfterJob_ = false;
    std::atomic_bool cancelled_ = false;
    std::thread worker_;
    guint updateSource_ = 0;
    std::mutex mutex_;
    std::map<size_t, ca::Update> pending_;
    std::optional<ca::Result> result_;
    std::string workerError_;
};
}

int main(int argc, char** argv) {
    ca::CommandLine command{};
    try {
        std::vector<std::wstring> args;
        for (int index = 1; index < argc; ++index) args.push_back(ca::wide(argv[index]));
        ca::parseArguments(args, command);
        if (auto status = ca::runCommandLine(command)) return *status;
    } catch (const std::exception& error) {
        ca::reportCommandError(command, error);
        return 1;
    }

    Workspace workspace(std::move(command));
    auto* app = gtk_application_new(applicationId, G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(+[](GtkApplication* application, gpointer data) {
        auto& workspace = *static_cast<Workspace*>(data);
        try { workspace.activate(application); }
        catch (const std::exception& error) { workspace.reportError(error); }
    }), &workspace);
    const int status = g_application_run(G_APPLICATION(app), 1, argv);
    g_object_unref(app);
    return status;
}
