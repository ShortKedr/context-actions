import os
from pathlib import Path

from gi.repository import Gio, GLib, GObject, Nautilus


class ContextActionsMenuProvider(GObject.GObject, Nautilus.MenuProvider):
    def get_file_items(self, *args):
        # Nautilus versions before 43 also pass a window before the file list.
        files = args[-1]
        paths = []
        for file in files:
            if file.get_file_type() != Gio.FileType.REGULAR:
                return []
            location = file.get_location()
            if not location.has_uri_scheme("file"):
                return []
            path = location.get_path()
            if path is None:
                return []
            paths.append(path)
        if not paths:
            return []

        item = Nautilus.MenuItem(
            name="ContextActions::Open",
            label="Context Actions",
            tip="Open the selected files in Context Actions",
            icon="io.github.ShortKedr.ContextActions",
        )
        item.connect("activate", self.open_files, paths)
        return [item]

    def open_files(self, _item, paths):
        # The portable installer links this module from beside the executable.
        portable = Path(__file__).resolve().with_name("context-actions")
        executable = str(portable) if os.access(portable, os.X_OK) else "context-actions"
        GLib.spawn_async([executable, "--", *paths], flags=GLib.SpawnFlags.SEARCH_PATH)
