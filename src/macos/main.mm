#import <AppKit/AppKit.h>
#include <dispatch/dispatch.h>

#include "core.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <thread>

namespace {
NSString* text(const std::wstring& value) {
    const auto bytes = ca::utf8(value);
    return [[NSString alloc] initWithBytes:bytes.data() length:bytes.size() encoding:NSUTF8StringEncoding];
}

NSString* label(const char* key) { return text(ca::tr(key)); }

std::wstring value(NSString* string) {
    auto bytes = [string dataUsingEncoding:NSUTF8StringEncoding];
    if (bytes.length == 0) return {};
    return ca::wide(std::string(static_cast<const char*>(bytes.bytes), bytes.length));
}

NSButton* button(const char* key, id target, SEL action) {
    return [NSButton buttonWithTitle:label(key) target:target action:action];
}

NSButton* symbolButton(NSString* symbol, const char* key, id target, SEL action) {
    NSString* title = label(key);
    NSImage* image = [NSImage imageWithSystemSymbolName:symbol accessibilityDescription:title];
    NSButton* control = [NSButton buttonWithImage:image target:target action:action];
    control.toolTip = title;
    control.accessibilityLabel = title;
    return control;
}

NSStackView* row(NSArray<NSView*>* views) {
    NSStackView* stack = [NSStackView stackViewWithViews:views];
    stack.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    stack.alignment = NSLayoutAttributeCenterY;
    return stack;
}

NSStackView* column(NSArray<NSView*>* views) {
    NSStackView* stack = [NSStackView stackViewWithViews:views];
    stack.orientation = NSUserInterfaceLayoutOrientationVertical;
    stack.alignment = NSLayoutAttributeLeading;
    return stack;
}

void fillWidth(NSView* view, NSView* container) {
    [view.widthAnchor constraintEqualToAnchor:container.widthAnchor].active = YES;
}

void pin(NSView* view, NSView* container, CGFloat margin) {
    view.translatesAutoresizingMaskIntoConstraints = NO;
    [container addSubview:view];
    [NSLayoutConstraint activateConstraints:@[
        [view.leadingAnchor constraintEqualToAnchor:container.leadingAnchor constant:margin],
        [view.trailingAnchor constraintEqualToAnchor:container.trailingAnchor constant:-margin],
        [view.topAnchor constraintEqualToAnchor:container.topAnchor constant:margin],
        [view.bottomAnchor constraintEqualToAnchor:container.bottomAnchor constant:-margin],
    ]];
}

bool isVideo(ca::Action action) {
    return action == ca::Action::compress || action == ca::Action::h264 ||
           action == ca::Action::h265 || action == ca::Action::telegram;
}

int integer(NSTextField* field, const char* key) {
    const auto input = value(field.stringValue);
    size_t length = 0;
    try {
        const int result = std::stoi(input, &length);
        if (length == input.size()) return result;
    } catch (const std::logic_error&) {}
    throw std::runtime_error(ca::utf8(ca::tr("invalid") + L" " + ca::tr(key)));
}

NSArray<NSURL*>* pastedFiles(NSPasteboard* pasteboard) {
    return (NSArray<NSURL*>*)[pasteboard readObjectsForClasses:@[[NSURL class]]
        options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}];
}

std::vector<ca::fs::path> filePaths(NSArray<NSURL*>* urls) {
    std::vector<ca::fs::path> paths;
    paths.reserve(urls.count);
    for (NSURL* url in urls) {
        if (!url.isFileURL) continue;
        if (const char* path = url.fileSystemRepresentation) paths.emplace_back(path);
    }
    return paths;
}

void placeAtPointer(NSWindow* window) {
    const NSPoint pointer = NSEvent.mouseLocation;
    NSScreen* screen = NSScreen.mainScreen;
    for (NSScreen* candidate in NSScreen.screens) {
        if (NSPointInRect(pointer, candidate.frame)) {
            screen = candidate;
            break;
        }
    }
    if (!screen) {
        [window center];
        return;
    }
    const NSRect bounds = screen.visibleFrame;
    const NSSize size = window.frame.size;
    const CGFloat left = NSMinX(bounds);
    const CGFloat bottom = NSMinY(bounds);
    [window setFrameOrigin:NSMakePoint(
        std::clamp(pointer.x, left, std::max(left, NSMaxX(bounds) - size.width)),
        std::clamp(pointer.y - size.height, bottom, std::max(bottom, NSMaxY(bounds) - size.height)))];
}

void showError(NSWindow* window, NSString* message) {
    NSAlert* alert = [[NSAlert alloc] init];
    alert.alertStyle = NSAlertStyleWarning;
    alert.messageText = label("failed");
    alert.informativeText = message;
    if (window) [alert beginSheetModalForWindow:window completionHandler:nil];
    else [alert runModal];
}
}

@interface Workspace : NSWindowController <NSWindowDelegate, NSTableViewDataSource,
    NSTableViewDelegate, NSTextFieldDelegate>
- (instancetype)initWithOptions:(const ca::Options&)options;
- (void)addURLs:(NSArray<NSURL*>*)urls;
- (void)addFiles:(id)sender;
- (void)run:(id)sender;
- (BOOL)cancelForTermination;
@end

@implementation Workspace {
    ca::Options _options;
    std::vector<ca::Action> _actions;
    ca::Action _formatAction;
    std::vector<std::wstring> _formats;
    std::map<ca::Action, std::wstring> _formatSelections;
    std::vector<ca::Rename> _preview;
    std::vector<ca::fs::path> _pendingFiles, _outputs;
    std::vector<std::wstring> _statuses;
    std::vector<double> _fractions;
    std::atomic_bool _cancel;
    double _totalFraction;
    BOOL _busy, _closeAfter;

    NSTableView* _files;
    NSPopUpButton* _action;
    NSPopUpButton* _format;
    NSTextField* _formatLabel;
    NSTextField *_videoQuality, *_imageQuality, *_videoWidth, *_imageWidth, *_fps, *_target;
    NSTextField *_pattern, *_start, *_prefix, *_suffix, *_find, *_replacement, *_output;
    NSButton *_mute, *_firstFrame;
    NSButton *_add, *_remove, *_up, *_down, *_previewButton, *_undo, *_browse;
    NSButton *_run, *_stop, *_open, *_reveal;
    NSGridView* _form;
    NSIndexSet *_videoRows, *_imageRows, *_renameRows;
    NSInteger _formatRow, _firstFrameRow, _outputRow;
    NSTextField *_availability, *_summary;
    NSProgressIndicator* _progress;
}

- (instancetype)initWithOptions:(const ca::Options&)options {
    NSWindow* window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 1040, 620)
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                  NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
        backing:NSBackingStoreBuffered defer:NO];
    self = [super initWithWindow:window];
    if (!self) return nil;

    _options = options;
    _actions = ca::catalog();
    _formatAction = ca::Action::more;
    if (!options.format.empty()) {
        const auto action = options.action == ca::Action::more ? ca::Action::image : options.action;
        _formatSelections[action] = ca::outputFormat(options);
    }
    window.delegate = self;
    window.title = @"Context Actions";
    window.contentMinSize = NSMakeSize(880, 540);
    window.releasedWhenClosed = NO;
    [self buildContent];
    [self resetRows];
    [window center];
    [window setFrameAutosaveName:@"ContextActionsWorkspace"];
    return self;
}

- (NSTextField*)field:(const std::wstring&)initial {
    NSTextField* field = [NSTextField textFieldWithString:text(initial)];
    field.delegate = self;
    return field;
}

- (void)buildContent {
    _files = [[NSTableView alloc] initWithFrame:NSZeroRect];
    _files.style = NSTableViewStyleInset;
    _files.usesAlternatingRowBackgroundColors = YES;
    _files.allowsMultipleSelection = YES;
    _files.columnAutoresizingStyle = NSTableViewUniformColumnAutoresizingStyle;
    _files.autoresizingMask = NSViewWidthSizable;
    _files.delegate = self;
    _files.dataSource = self;
    [_files registerForDraggedTypes:@[NSPasteboardTypeFileURL]];

    NSTableColumn* file = [[NSTableColumn alloc] initWithIdentifier:@"file"];
    file.title = label("file");
    file.width = 230;
    file.minWidth = 125;
    [_files addTableColumn:file];
    NSTableColumn* status = [[NSTableColumn alloc] initWithIdentifier:@"status"];
    status.title = label("status");
    status.width = 150;
    status.minWidth = 100;
    [_files addTableColumn:status];

    NSScrollView* scroller = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    scroller.hasVerticalScroller = YES;
    scroller.borderType = NSBezelBorder;
    scroller.documentView = _files;

    _add = button("add", self, @selector(addFiles:));
    _remove = symbolButton(@"trash", "remove", self, @selector(removeFiles:));
    _up = symbolButton(@"arrow.up", "up", self, @selector(moveFile:));
    _down = symbolButton(@"arrow.down", "down", self, @selector(moveFile:));
    NSStackView* inputs = column(@[row(@[_add, _remove, _up, _down]), scroller]);
    fillWidth(scroller, inputs);

    _action = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    _action.menu.autoenablesItems = NO;
    [_action addItemWithTitle:label("more")];
    _action.lastItem.enabled = NO;
    for (auto action : _actions) [_action addItemWithTitle:text(ca::trAction(action))];
    _action.target = self;
    _action.action = @selector(actionChanged:);
    auto selected = std::find(_actions.begin(), _actions.end(), _options.action);
    [_action selectItemAtIndex:selected == _actions.end() ? 0 : (selected - _actions.begin()) + 1];

    _videoQuality = [self field:std::to_wstring(_options.quality)];
    _videoWidth = [self field:std::to_wstring(_options.width)];
    _fps = [self field:std::to_wstring(_options.fps)];
    _target = [self field:std::to_wstring(_options.targetMB)];
    _mute = [NSButton checkboxWithTitle:label("mute") target:nil action:NULL];
    _mute.state = _options.mute ? NSControlStateValueOn : NSControlStateValueOff;
    _imageQuality = [self field:std::to_wstring(_options.quality)];
    _imageWidth = [self field:std::to_wstring(_options.width)];
    _format = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    _firstFrame = [NSButton checkboxWithTitle:label("firstFrame") target:nil action:NULL];
    _firstFrame.state = _options.firstFrame ? NSControlStateValueOn : NSControlStateValueOff;

    _pattern = [self field:_options.pattern];
    _start = [self field:std::to_wstring(_options.start)];
    _prefix = [self field:_options.prefix];
    _suffix = [self field:_options.suffix];
    _find = [self field:_options.find];
    _replacement = [self field:_options.replace];
    _previewButton = button("preview", self, @selector(previewNames:));
    _undo = button("undo", self, @selector(undoRename:));

    _output = [self field:ca::pathText(_options.output)];
    _output.placeholderString = label("sameFolder");
    _browse = button("browse", self, @selector(browseOutput:));
    [_output setContentHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];

    NSMutableArray<NSArray<NSView*>*>* rows = [NSMutableArray array];
    auto addRow = [&](const char* key, NSView* field) {
        NSTextField* caption = [NSTextField wrappingLabelWithString:label(key)];
        caption.preferredMaxLayoutWidth = 195;
        [rows addObject:@[caption, field]];
        [field setAccessibilityLabel:label(key)];
        return caption;
    };
    auto spanningRow = [&](NSView* view) {
        [rows addObject:@[view, NSGridCell.emptyContentView]];
    };

    addRow("action", _action);
    auto begin = rows.count;
    addRow("quality", _videoQuality);
    addRow("width", _videoWidth);
    addRow("fps", _fps);
    addRow("target", _target);
    const auto muteRow = rows.count;
    spanningRow(_mute);
    _videoRows = [NSIndexSet indexSetWithIndexesInRange:NSMakeRange(begin, rows.count - begin)];

    _formatRow = rows.count;
    _formatLabel = addRow("format", _format);
    begin = rows.count;
    addRow("width", _imageWidth);
    addRow("imageQuality", _imageQuality);
    _imageRows = [NSIndexSet indexSetWithIndexesInRange:NSMakeRange(begin, rows.count - begin)];
    _firstFrameRow = rows.count;
    spanningRow(_firstFrame);

    begin = rows.count;
    addRow("pattern", _pattern);
    addRow("start", _start);
    addRow("prefix", _prefix);
    addRow("suffix", _suffix);
    addRow("find", _find);
    addRow("replace", _replacement);
    const auto renameButtonsRow = rows.count;
    spanningRow(row(@[_previewButton, _undo]));
    _renameRows = [NSIndexSet indexSetWithIndexesInRange:NSMakeRange(begin, rows.count - begin)];

    _outputRow = rows.count;
    addRow("saveTo", row(@[_output, _browse]));

    _form = [NSGridView gridViewWithViews:rows];
    _form.rowSpacing = 8;
    _form.columnSpacing = 12;
    [_form columnAtIndex:0].width = 195;
    [_form columnAtIndex:0].xPlacement = NSGridCellPlacementLeading;
    [_form columnAtIndex:1].xPlacement = NSGridCellPlacementFill;
    for (NSUInteger index : {muteRow, static_cast<NSUInteger>(_firstFrameRow), renameButtonsRow}) {
        [_form mergeCellsInHorizontalRange:NSMakeRange(0, 2) verticalRange:NSMakeRange(index, 1)];
    }
    _availability = [NSTextField wrappingLabelWithString:@""];
    _availability.textColor = NSColor.secondaryLabelColor;
    NSStackView* options = column(@[_form, _availability]);
    options.spacing = 12;
    fillWidth(_form, options);
    fillWidth(_availability, options);

    NSBox* divider = [[NSBox alloc] initWithFrame:NSZeroRect];
    divider.boxType = NSBoxSeparator;
    NSStackView* body = row(@[inputs, divider, options]);
    body.alignment = NSLayoutAttributeTop;
    body.spacing = 16;
    [inputs.widthAnchor constraintEqualToAnchor:body.widthAnchor multiplier:0.47].active = YES;
    [inputs.heightAnchor constraintEqualToAnchor:body.heightAnchor].active = YES;
    [divider.widthAnchor constraintEqualToConstant:1].active = YES;
    [divider.heightAnchor constraintEqualToAnchor:body.heightAnchor].active = YES;

    _progress = [[NSProgressIndicator alloc] initWithFrame:NSZeroRect];
    _progress.indeterminate = NO;
    _progress.maxValue = 100;
    _progress.style = NSProgressIndicatorStyleBar;
    _summary = [NSTextField wrappingLabelWithString:label("ready")];
    _open = button("open", self, @selector(openResult:));
    _reveal = button("reveal", self, @selector(openResult:));
    _stop = button("cancel", self, @selector(cancel:));
    _run = button("run", self, @selector(run:));
    _run.keyEquivalent = @"\r";
    NSStackView* footer = row(@[_open, _reveal, _summary, _stop, _run]);
    [_summary setContentHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
    NSStackView* layout = column(@[body, _progress, footer]);
    layout.spacing = 12;
    for (NSView* view in @[(NSView*)body, _progress, footer]) fillWidth(view, layout);
    pin(layout, self.window.contentView, 16);
}

- (void)clearPreview {
    if (_preview.empty()) return;
    _preview.clear();
    _statuses.assign(_options.files.size(), ca::tr("ready"));
    [_files reloadData];
}

- (void)resetRows {
    if (_options.action == ca::Action::more && !_options.files.empty() &&
        ca::accepts(ca::Action::repack, _options.files)) {
        const auto selected = std::find(_actions.begin(), _actions.end(), ca::Action::repack);
        if (selected != _actions.end()) {
            _options.action = *selected;
            [_action selectItemAtIndex:(selected - _actions.begin()) + 1];
        }
    }
    _preview.clear();
    _statuses.assign(_options.files.size(), ca::tr("ready"));
    _outputs.assign(_options.files.size(), {});
    _summary.stringValue = label("ready");
    _progress.doubleValue = 0;
    [_files reloadData];
    [self refresh];
}

- (void)refresh {
    const auto action = _options.action;
    const bool video = isVideo(action);
    const bool image = action == ca::Action::image;
    const bool rename = action == ca::Action::rename;
    const bool chosen = action != ca::Action::more;
    auto formats = ca::outputFormats(action, _options.files);
    if (action != _formatAction || formats != _formats) {
        if (NSString* previous = _format.titleOfSelectedItem) _formatSelections[_formatAction] = value(previous);
        _formatAction = action;
        _formats = std::move(formats);
        NSString* caption = label(action == ca::Action::repack ? "archiveFormat" : "format");
        _formatLabel.stringValue = caption;
        [_format setAccessibilityLabel:caption];
        [_format removeAllItems];
        for (const auto& format : _formats) [_format addItemWithTitle:text(format)];
        if (_format.numberOfItems > 0) {
            [_format selectItemAtIndex:0];
            const auto preferred = _formatSelections.find(action);
            if (preferred != _formatSelections.end()) {
                if (NSMenuItem* item = [_format itemWithTitle:text(preferred->second)]) [_format selectItem:item];
            }
        }
    }
    [_videoRows enumerateIndexesUsingBlock:^(NSUInteger index, BOOL*) { [self->_form rowAtIndex:index].hidden = !video; }];
    [_imageRows enumerateIndexesUsingBlock:^(NSUInteger index, BOOL*) { [self->_form rowAtIndex:index].hidden = !image; }];
    [_renameRows enumerateIndexesUsingBlock:^(NSUInteger index, BOOL*) { [self->_form rowAtIndex:index].hidden = !rename; }];
    [_form rowAtIndex:_formatRow].hidden = _format.numberOfItems == 0;
    [_form rowAtIndex:_firstFrameRow].hidden = !image && action != ca::Action::pdf;
    [_form rowAtIndex:_outputRow].hidden = !chosen || rename;

    const auto unavailable = chosen ? ca::unavailableReason(action, _options.files) : ca::tr("select");
    _availability.stringValue = text(unavailable);
    _run.enabled = !_busy && unavailable.empty();
    _previewButton.enabled = !_busy && unavailable.empty();
    std::error_code error;
    _undo.enabled = !_busy && rename && ca::fs::is_regular_file(ca::dataDirectory() / "rename-undo.txt", error);
    _stop.enabled = _busy && !_cancel;
    _add.enabled = !_busy;
    _action.enabled = !_busy;
    for (NSControl* control in @[_videoQuality, _videoWidth, _fps, _target, _mute,
             _imageQuality, _imageWidth, _format, _firstFrame, _pattern, _start,
             _prefix, _suffix, _find, _replacement, _output, _browse]) {
        control.enabled = !_busy;
    }
    [self refreshSelection];
}

- (void)refreshSelection {
    const NSInteger index = _files.selectedRow;
    const bool single = _files.numberOfSelectedRows == 1;
    _remove.enabled = !_busy && index >= 0;
    _up.enabled = !_busy && single && index > 0;
    _down.enabled = !_busy && single && index >= 0 && static_cast<size_t>(index + 1) < _options.files.size();
    const bool output = !_busy && std::any_of(_outputs.begin(), _outputs.end(), [](const auto& path) { return !path.empty(); });
    _open.enabled = output;
    _reveal.enabled = output;
}

- (void)actionChanged:(id)sender {
    const NSInteger index = _action.indexOfSelectedItem;
    if (index <= 0) return;
    _options.action = _actions[static_cast<size_t>(index - 1)];
    [self clearPreview];
    [self refresh];
}

- (void)controlTextDidChange:(NSNotification*)notification {
    if (_options.action == ca::Action::rename) [self clearPreview];
}

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView {
    return static_cast<NSInteger>(_options.files.size());
}

- (NSView*)tableView:(NSTableView*)tableView viewForTableColumn:(NSTableColumn*)tableColumn row:(NSInteger)index {
    const size_t item = static_cast<size_t>(index);
    const bool filename = [tableColumn.identifier isEqualToString:@"file"];
    NSTableCellView* cell = (NSTableCellView*)[tableView makeViewWithIdentifier:tableColumn.identifier owner:self];
    if (!cell) {
        cell = [[NSTableCellView alloc] initWithFrame:NSZeroRect];
        cell.identifier = tableColumn.identifier;
        NSTextField* field = [NSTextField labelWithString:@""];
        field.lineBreakMode = NSLineBreakByTruncatingTail;
        field.translatesAutoresizingMaskIntoConstraints = NO;
        [cell addSubview:field];
        cell.textField = field;
        [NSLayoutConstraint activateConstraints:@[
            [field.leadingAnchor constraintEqualToAnchor:cell.leadingAnchor constant:4],
            [field.trailingAnchor constraintEqualToAnchor:cell.trailingAnchor constant:-4],
            [field.centerYAnchor constraintEqualToAnchor:cell.centerYAnchor],
        ]];
    }
    NSTextField* field = cell.textField;
    field.stringValue = text(filename ? ca::pathText(_options.files[item].filename()) : _statuses[item]);
    cell.toolTip = filename ? text(ca::pathText(_options.files[item])) : field.stringValue;
    return cell;
}

- (void)tableViewSelectionDidChange:(NSNotification*)notification { [self refreshSelection]; }

- (NSDragOperation)tableView:(NSTableView*)tableView validateDrop:(id<NSDraggingInfo>)info
    proposedRow:(NSInteger)index proposedDropOperation:(NSTableViewDropOperation)operation {
    if (_busy || pastedFiles(info.draggingPasteboard).count == 0) return NSDragOperationNone;
    [tableView setDropRow:-1 dropOperation:NSTableViewDropOn];
    return NSDragOperationCopy;
}

- (BOOL)tableView:(NSTableView*)tableView acceptDrop:(id<NSDraggingInfo>)info
    row:(NSInteger)index dropOperation:(NSTableViewDropOperation)operation {
    if (_busy) return NO;
    [self addURLs:pastedFiles(info.draggingPasteboard)];
    return YES;
}

- (void)addPaths:(const std::vector<ca::fs::path>&)paths {
    auto& destination = _busy ? _pendingFiles : _options.files;
    const size_t previous = destination.size();
    for (const auto& path : paths) {
        std::error_code error;
        if (ca::fs::is_regular_file(path, error) &&
            std::find(destination.begin(), destination.end(), path) == destination.end())
            destination.push_back(path);
    }
    if (!_busy && destination.size() != previous) [self resetRows];
}

- (void)addURLs:(NSArray<NSURL*>*)urls {
    [self addPaths:filePaths(urls)];
}

- (void)addFiles:(id)sender {
    if (_busy || self.window.attachedSheet) return;
    NSOpenPanel* picker = [NSOpenPanel openPanel];
    picker.allowsMultipleSelection = YES;
    picker.canChooseFiles = YES;
    picker.canChooseDirectories = NO;
    [picker beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse response) {
        if (response == NSModalResponseOK) [self addURLs:picker.URLs];
    }];
}

- (void)removeFiles:(id)sender {
    if (_busy) return;
    NSIndexSet* selected = _files.selectedRowIndexes;
    for (NSUInteger index = selected.lastIndex; index != NSNotFound; index = [selected indexLessThanIndex:index])
        _options.files.erase(_options.files.begin() + index);
    [self resetRows];
}

- (void)moveFile:(id)sender {
    if (_busy || _files.numberOfSelectedRows != 1) return;
    const NSInteger index = _files.selectedRow;
    const NSInteger next = index + (sender == _up ? -1 : 1);
    if (next < 0 || static_cast<size_t>(next) >= _options.files.size()) return;
    std::swap(_options.files[static_cast<size_t>(index)], _options.files[static_cast<size_t>(next)]);
    [self resetRows];
    [_files selectRowIndexes:[NSIndexSet indexSetWithIndex:next] byExtendingSelection:NO];
    [_files scrollRowToVisible:next];
}

- (void)browseOutput:(id)sender {
    if (_busy || self.window.attachedSheet) return;
    NSOpenPanel* picker = [NSOpenPanel openPanel];
    picker.canChooseFiles = NO;
    picker.canChooseDirectories = YES;
    picker.canCreateDirectories = YES;
    [picker beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse response) {
        if (response == NSModalResponseOK) self->_output.stringValue = picker.URL.path;
    }];
}

- (ca::Options)readOptions {
    [self.window makeFirstResponder:_files];
    auto job = _options;
    if (job.action == ca::Action::rename) {
        job.pattern = value(_pattern.stringValue);
        job.start = integer(_start, "start");
        job.prefix = value(_prefix.stringValue);
        job.suffix = value(_suffix.stringValue);
        job.find = value(_find.stringValue);
        job.replace = value(_replacement.stringValue);
    } else {
        job.output = ca::textPath(value(_output.stringValue));
    }
    if (isVideo(job.action)) {
        job.quality = integer(_videoQuality, "quality");
        job.width = integer(_videoWidth, "width");
        job.fps = integer(_fps, "fps");
        job.targetMB = integer(_target, "target");
        job.mute = _mute.state == NSControlStateValueOn;
    }
    if (_format.numberOfItems > 0) job.format = value(_format.titleOfSelectedItem);
    if (job.action == ca::Action::image) {
        job.quality = integer(_imageQuality, "imageQuality");
        job.width = integer(_imageWidth, "width");
    }
    job.firstFrame = _firstFrame.state == NSControlStateValueOn;
    ca::validate(job);
    return job;
}

- (void)previewNames:(id)sender {
    if (_busy) return;
    try {
        _preview = ca::previewRename([self readOptions]);
        for (size_t index = 0; index < _preview.size(); ++index)
            _statuses[index] = L"\u2192 " + ca::pathText(_preview[index].to.filename());
        [_files reloadData];
    } catch (const std::exception& error) {
        [self clearPreview];
        showError(self.window, text(ca::wide(error.what())));
    }
}

- (void)undoRename:(id)sender {
    if (!_busy) [self beginJob:_options undo:YES];
}

- (void)run:(id)sender {
    if (_busy || self.window.attachedSheet) return;
    try {
        auto job = [self readOptions];
        if (job.action == ca::Action::rename && _preview.empty())
            throw std::runtime_error(ca::utf8(ca::tr("previewRequired")));
        [self beginJob:job undo:NO];
    } catch (const std::exception& error) {
        showError(self.window, text(ca::wide(error.what())));
    }
}

- (void)beginJob:(const ca::Options&)job undo:(BOOL)undo {
    [self resetRows];
    _busy = YES;
    _cancel = false;
    _fractions.assign(_options.files.size(), 0);
    _totalFraction = 0;
    _summary.stringValue = label("working");
    [self refresh];
    try {
        std::thread([workspace = self, options = job, undo] {
            @autoreleasepool {
                ca::Result result;
                std::wstring error;
                try {
                    if (undo) {
                        ca::undoRename(workspace->_cancel);
                        result.succeeded = 1;
                    } else {
                        result = ca::execute(options, workspace->_cancel, [workspace](const ca::Update& update) {
                            const auto snapshot = update;
                            dispatch_async(dispatch_get_main_queue(), ^{
                                [workspace updateProgress:snapshot];
                            });
                        });
                    }
                } catch (const ca::Cancelled&) {
                    result.cancelled = true;
                } catch (const std::exception& failure) {
                    result.failed = 1;
                    error = ca::wide(failure.what());
                }
                dispatch_async(dispatch_get_main_queue(), ^{
                    [workspace finishJob:result undo:undo error:error];
                });
            }
        }).detach();
    } catch (const std::exception& failure) {
        ca::Result result;
        result.failed = 1;
        [self finishJob:result undo:undo error:ca::wide(failure.what())];
    }
}

- (void)updateProgress:(const ca::Update&)update {
    const auto index = update.index;
    if (index >= _statuses.size()) return;
    _statuses[index] = update.status;
    if (!update.output.empty()) _outputs[index] = update.output;
    _totalFraction += update.progress - _fractions[index];
    _fractions[index] = update.progress;
    _progress.doubleValue = 100 * _totalFraction / _fractions.size();
    [_files reloadDataForRowIndexes:[NSIndexSet indexSetWithIndex:index] columnIndexes:[NSIndexSet indexSetWithIndex:1]];
}

- (void)finishJob:(const ca::Result&)result undo:(BOOL)undo error:(const std::wstring&)error {
    _busy = NO;
    if (!result.failed && !result.cancelled) {
        if (undo) {
            _options.files.clear();
            [self resetRows];
        } else if (_options.action == ca::Action::rename) {
            _options.files = result.outputs;
            [_files reloadData];
        }
        _progress.doubleValue = 100;
    }
    if (!_closeAfter) {
        auto pending = std::move(_pendingFiles);
        _pendingFiles.clear();
        [self addPaths:pending];
    }
    _summary.stringValue = text(ca::tr(result.cancelled ? "cancelled" : result.failed ? "failed" : "done")
        + L" \u00b7 " + std::to_wstring(result.succeeded) + L" " + ca::tr("succeeded")
        + L" \u00b7 " + std::to_wstring(result.failed) + L" " + ca::tr("failed"));
    [self refresh];
    if (_closeAfter) [self.window close];
    else if (!error.empty()) showError(self.window, text(error));
}

- (void)cancel:(id)sender {
    if (!_busy) return;
    _cancel = true;
    _stop.enabled = NO;
    _summary.stringValue = label("cancelling");
}

- (BOOL)windowShouldClose:(NSWindow*)sender {
    if (!_busy) return YES;
    _closeAfter = YES;
    [self cancel:nil];
    return NO;
}

- (BOOL)cancelForTermination {
    if (!_busy) return NO;
    _closeAfter = YES;
    [self cancel:nil];
    return YES;
}

- (void)openResult:(id)sender {
    auto result = _outputs.end();
    const NSInteger selected = _files.selectedRow;
    if (selected >= 0 && static_cast<size_t>(selected) < _outputs.size() && !_outputs[selected].empty())
        result = _outputs.begin() + selected;
    else
        result = std::find_if(_outputs.begin(), _outputs.end(), [](const auto& path) { return !path.empty(); });
    if (result == _outputs.end()) return;
    NSURL* url = [NSURL fileURLWithPath:text(ca::pathText(*result))];
    if (sender == _reveal) [NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:@[url]];
    else if (![NSWorkspace.sharedWorkspace openURL:url]) showError(self.window, text(ca::pathText(*result)));
}
@end

@interface ApplicationDelegate : NSObject <NSApplicationDelegate>
- (instancetype)initWithCommand:(const ca::CommandLine&)command;
- (void)chooseFileAction:(NSPasteboard*)pasteboard userData:(NSString*)userData error:(NSString**)error;
@end

@implementation ApplicationDelegate {
    ca::CommandLine _command;
    NSMutableArray<Workspace*>* _workspaces;
    NSMutableSet<NSWindow*>* _closingForTermination;
    NSMutableArray<NSURL*>* _pendingURLs;
    BOOL _finishedLaunching;
    NSPanel* _chooser;
    ca::Options _chooserOptions;
    std::vector<ca::ActionChoice> _choices;
}

- (instancetype)initWithCommand:(const ca::CommandLine&)command {
    self = [super init];
    if (self) {
        _command = command;
        _workspaces = [NSMutableArray array];
        _closingForTermination = [NSMutableSet set];
        _pendingURLs = [NSMutableArray array];
    }
    return self;
}

- (Workspace*)openWorkspace:(const ca::Options&)options adding:(NSArray<NSURL*>*)urls {
    Workspace* workspace = [[Workspace alloc] initWithOptions:options];
    [_workspaces addObject:workspace];
    [workspace addURLs:urls];
    [workspace showWindow:nil];
    [NSApp activateIgnoringOtherApps:YES];
    return workspace;
}

- (Workspace*)frontWorkspace {
    for (Workspace* workspace in _workspaces) {
        if (workspace.window == NSApp.keyWindow) return workspace;
    }
    return _workspaces.lastObject;
}

- (void)openFiles:(id)sender {
    Workspace* workspace = [self frontWorkspace];
    if (!workspace) workspace = [self openWorkspace:ca::Options{} adding:nil];
    [workspace addFiles:sender];
}

- (void)cancelChooser:(id)sender {
    [_chooser close];
}

- (void)openChosenAction:(NSButton*)sender {
    if (sender.window != _chooser || sender.tag < 0 || static_cast<size_t>(sender.tag) >= _choices.size()) return;
    const auto& choice = _choices[static_cast<size_t>(sender.tag)];
    if (!choice.unavailable.empty()) return;

    try {
        auto options = _chooserOptions;
        options.action = choice.action;
        [self openWorkspace:options adding:nil];
        [_chooser close];
    } catch (const std::exception& failure) {
        showError(_chooser, text(ca::wide(failure.what())));
    }
}

- (void)presentChooser:(const ca::Options&)options {
    auto choices = ca::actionChoices(options.files);
    _chooserOptions = options;
    _choices = std::move(choices);
    if (!_chooser) {
        _chooser = [[NSPanel alloc] initWithContentRect:NSZeroRect
            styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskUtilityWindow
            backing:NSBackingStoreBuffered defer:NO];
        _chooser.title = label("chooseAction");
        _chooser.floatingPanel = YES;
        _chooser.releasedWhenClosed = NO;
        _chooser.collectionBehavior = NSWindowCollectionBehaviorMoveToActiveSpace;
    }

    NSMutableArray<NSArray<NSView*>*>* rows = [NSMutableArray array];
    NSMutableArray<NSButton*>* focusable = [NSMutableArray array];
    NSMutableArray<NSView*>* pair = [NSMutableArray arrayWithCapacity:2];
    for (size_t index = 0; index < _choices.size(); ++index) {
        const auto& choice = _choices[index];
        NSButton* control = [NSButton buttonWithTitle:text(choice.label) target:self action:@selector(openChosenAction:)];
        control.tag = static_cast<NSInteger>(index);
        control.enabled = choice.unavailable.empty();
        if (control.enabled) [focusable addObject:control];
        else {
            control.toolTip = text(choice.unavailable);
            control.accessibilityHelp = control.toolTip;
        }
        [control.widthAnchor constraintGreaterThanOrEqualToConstant:154].active = YES;
        [control.heightAnchor constraintGreaterThanOrEqualToConstant:36].active = YES;
        [pair addObject:control];
        if (pair.count == 2) {
            [rows addObject:[pair copy]];
            [pair removeAllObjects];
        }
    }
    if (pair.count == 1) [rows addObject:@[pair.firstObject, NSGridCell.emptyContentView]];

    NSView* body = [NSTextField labelWithString:label("noApplicableActions")];
    if (rows.count) {
        NSGridView* grid = [NSGridView gridViewWithViews:rows];
        grid.rowSpacing = 8;
        grid.columnSpacing = 8;
        grid.xPlacement = NSGridCellPlacementFill;
        body = grid;
    }
    NSButton* cancel = button("cancel", self, @selector(cancelChooser:));
    cancel.keyEquivalent = @"\x1b";
    cancel.keyEquivalentModifierMask = 0;
    [focusable addObject:cancel];
    for (NSUInteger index = 0; index < focusable.count; ++index)
        focusable[index].nextKeyView = focusable[(index + 1) % focusable.count];

    NSStackView* layout = column(@[body, cancel]);
    layout.spacing = 12;
    const CGFloat margin = 16;
    const NSSize size = layout.fittingSize;
    _chooser.contentView = [[NSView alloc] initWithFrame:NSZeroRect];
    [_chooser setContentSize:NSMakeSize(size.width + 2 * margin, size.height + 2 * margin)];
    pin(layout, _chooser.contentView, margin);
    placeAtPointer(_chooser);
    NSButton* first = focusable.firstObject;
    _chooser.initialFirstResponder = first;
    [_chooser makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    if (first.acceptsFirstResponder) [_chooser makeFirstResponder:first];
}

- (void)chooseFileAction:(NSPasteboard*)pasteboard userData:(NSString*)userData error:(NSString**)error {
    NSArray<NSURL*>* urls = pastedFiles(pasteboard);
    auto paths = filePaths(urls);
    if (paths.empty() || paths.size() != urls.count) {
        if (error) *error = label("select");
        return;
    }
    ca::Options options;
    options.files = std::move(paths);
    dispatch_async(dispatch_get_main_queue(), ^{
        if (self->_closingForTermination.count) return;
        try { [self presentChooser:options]; }
        catch (const std::exception& failure) { showError(nil, text(ca::wide(failure.what()))); }
    });
}

- (void)windowWillClose:(NSNotification*)notification {
    NSWindow* window = notification.object;
    if (window == _chooser) {
        _chooser = nil;
        _chooserOptions = {};
        _choices.clear();
        if (!_workspaces.count) dispatch_async(dispatch_get_main_queue(), ^{
            if (!self->_chooser && !self->_workspaces.count) [NSApp terminate:nil];
        });
        return;
    }
    const auto index = [_workspaces indexOfObjectPassingTest:^BOOL(Workspace* workspace, NSUInteger, BOOL*) {
        return workspace.window == window;
    }];
    if (index != NSNotFound) [_workspaces removeObjectAtIndex:index];
    if ([_closingForTermination containsObject:window]) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self->_closingForTermination removeObject:window];
            if (!self->_closingForTermination.count) [NSApp replyToApplicationShouldTerminate:YES];
        });
    }
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    NSMenu* menuBar = [[NSMenu alloc] init];
    NSMenuItem* application = [menuBar addItemWithTitle:@"" action:NULL keyEquivalent:@""];
    NSMenu* appMenu = [[NSMenu alloc] initWithTitle:@"Context Actions"];
    [appMenu addItemWithTitle:@"About Context Actions" action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:@"Hide Context Actions" action:@selector(hide:) keyEquivalent:@"h"];
    [appMenu addItemWithTitle:@"Quit Context Actions" action:@selector(terminate:) keyEquivalent:@"q"];
    application.submenu = appMenu;

    NSMenuItem* file = [menuBar addItemWithTitle:@"File" action:NULL keyEquivalent:@""];
    NSMenu* fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
    NSMenuItem* open = [fileMenu addItemWithTitle:label("add") action:@selector(openFiles:) keyEquivalent:@"o"];
    open.target = self;
    [fileMenu addItemWithTitle:@"Close Window" action:@selector(performClose:) keyEquivalent:@"w"];
    file.submenu = fileMenu;

    NSMenuItem* edit = [menuBar addItemWithTitle:@"Edit" action:NULL keyEquivalent:@""];
    NSMenu* editMenu = [[NSMenu alloc] initWithTitle:@"Edit"];
    [editMenu addItemWithTitle:@"Undo" action:@selector(undo:) keyEquivalent:@"z"];
    [editMenu addItem:[NSMenuItem separatorItem]];
    [editMenu addItemWithTitle:@"Cut" action:@selector(cut:) keyEquivalent:@"x"];
    [editMenu addItemWithTitle:@"Copy" action:@selector(copy:) keyEquivalent:@"c"];
    [editMenu addItemWithTitle:@"Paste" action:@selector(paste:) keyEquivalent:@"v"];
    [editMenu addItemWithTitle:@"Select All" action:@selector(selectAll:) keyEquivalent:@"a"];
    edit.submenu = editMenu;
    NSApp.mainMenu = menuBar;

    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(windowWillClose:)
        name:NSWindowWillCloseNotification object:nil];
    _finishedLaunching = YES;
    // A cold Finder Services invocation supplies its selection after app launch.
    NSNumber* defaultLaunch = notification.userInfo[NSApplicationLaunchIsDefaultLaunchKey];
    if (_command.chooseAction) [self presentChooser:_command.options];
    else if (!defaultLaunch || defaultLaunch.boolValue || _pendingURLs.count ||
        !_command.options.files.empty() || _command.options.action != ca::Action::more) {
        Workspace* workspace = [self openWorkspace:_command.options adding:_pendingURLs];
        if (_command.immediate) dispatch_async(dispatch_get_main_queue(), ^{ [workspace run:nil]; });
    }
    [_pendingURLs removeAllObjects];
    NSApp.servicesProvider = self;
}

- (void)application:(NSApplication*)application openURLs:(NSArray<NSURL*>*)urls {
    if (!_finishedLaunching) {
        [_pendingURLs addObjectsFromArray:urls];
    } else if (Workspace* workspace = [self frontWorkspace]) {
        [workspace addURLs:urls];
        [workspace showWindow:nil];
    } else {
        [self openWorkspace:_command.options adding:urls];
    }
}

- (BOOL)applicationShouldHandleReopen:(NSApplication*)application hasVisibleWindows:(BOOL)visible {
    if (!visible) {
        if (Workspace* workspace = [self frontWorkspace]) [workspace showWindow:nil];
        else if (!_chooser) [self openWorkspace:ca::Options{} adding:nil];
    }
    return YES;
}

- (void)applicationDidResignActive:(NSNotification*)notification {
    [_chooser close];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)application {
    return !_chooser && !_closingForTermination.count;
}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)application {
    for (Workspace* workspace in _workspaces) {
        if ([workspace cancelForTermination]) [_closingForTermination addObject:workspace.window];
    }
    return _closingForTermination.count ? NSTerminateLater : NSTerminateNow;
}
@end

int main(int argc, char* argv[]) {
    @autoreleasepool {
        ca::CommandLine command{};
        try {
            std::vector<std::wstring> arguments;
            for (int index = 1; index < argc; ++index) {
                const std::string argument = argv[index];
                if (!argument.starts_with("-psn_")) arguments.push_back(ca::wide(argument));
            }
            ca::parseArguments(arguments, command);
            if (const auto result = ca::runCommandLine(command)) return *result;
            NSApplication* application = NSApplication.sharedApplication;
            [application setActivationPolicy:NSApplicationActivationPolicyRegular];
            __attribute__((objc_precise_lifetime)) ApplicationDelegate* delegate =
                [[ApplicationDelegate alloc] initWithCommand:command];
            application.delegate = delegate;
            [application run];
            return 0;
        } catch (const std::exception& error) {
            ca::reportCommandError(command, error);
            if (command.headless || command.help) std::fprintf(stderr, "%s\n", error.what());
            else {
                [NSApplication sharedApplication];
                showError(nil, text(ca::wide(error.what())));
            }
            return 1;
        }
    }
}
