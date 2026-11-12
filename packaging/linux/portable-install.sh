#!/usr/bin/env bash
set -euo pipefail

usage() {
    printf '%s\n' 'Run install.sh install [--allow-reinstall-or-downgrade] to install for your user.' \
        'Run install.sh uninstall to remove the per-user app and desktop integration.'
}

fail() {
    printf '%s\n' "$1" >&2
    exit 1
}

operation=${1:---help}
allow_replace=false
case "$operation" in
    install)
        if [[ $# == 2 && $2 == --allow-reinstall-or-downgrade ]]; then allow_replace=true
        elif [[ $# != 1 ]]; then usage >&2; exit 2; fi
        ;;
    uninstall) if [[ $# != 1 ]]; then usage >&2; exit 2; fi ;;
    -h|--help) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
esac

((EUID != 0)) || fail 'Run the per-user installer without sudo.'
command -v flock >/dev/null || fail 'The per-user installer requires flock from util-linux.'

data_home=${XDG_DATA_HOME:-"$HOME/.local/share"}
[[ $data_home == /* && $data_home != *'='* && ! $data_home =~ [[:cntrl:]] ]] ||
    fail 'XDG_DATA_HOME must be an absolute path without equals signs or control characters.'
if [[ $operation == uninstall && ! -d $data_home ]]; then
    printf '%s\n' 'Context Actions is not installed for this user.'
    exit 0
fi
mkdir -p -- "$data_home"
data_home=$(realpath -- "$data_home")
[[ $data_home != *'='* && ! $data_home =~ [[:cntrl:]] ]] ||
    fail 'The resolved data directory contains an unsupported character.'
exec 9<"$data_home"
flock -n 9 || fail 'Another per-user installation is in progress.'

app_id=io.github.ShortKedr.ContextActions
app_dir="$data_home/context-actions-app"
launcher="$data_home/applications/$app_id.desktop"
icon="$data_home/icons/hicolor/256x256/apps/$app_id.png"
nautilus_menu="$data_home/nautilus-python/extensions/context_actions.py"
dolphin_menu="$data_home/kio/servicemenus/$app_id.servicemenu.desktop"
desktop_links=("$launcher" "$icon" "$nautilus_menu" "$dolphin_menu")

valid_version() {
    [[ $1 =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]]
}

owned_link() {
    [[ -L $1 && $(readlink -- "$1") == "$2" ]]
}

refresh_desktop() {
    if command -v update-desktop-database >/dev/null && [[ -d $data_home/applications ]]; then
        update-desktop-database "$data_home/applications" ||
            printf '%s\n' 'Could not refresh the desktop application cache.' >&2
    fi
}

current=
current_version=
if [[ -L $app_dir ]]; then
    current=$(readlink -- "$app_dir")
    [[ $current == "$data_home"/.context-actions-app.* && ${current#"$data_home"/} != */* &&
       -d $current && ! -L $current && -f $current/VERSION ]] ||
        fail "The installer does not own $app_dir."
    current_version=$(<"$current/VERSION")
    valid_version "$current_version" || fail "Invalid installed version in $current/VERSION."
elif [[ -e $app_dir ]]; then
    fail "The installer does not own $app_dir."
fi

if [[ $operation == uninstall ]]; then
    if [[ -z $current ]]; then
        printf '%s\n' 'Context Actions is not installed for this user.'
        exit 0
    fi
    for link in "${desktop_links[@]}"; do
        if owned_link "$link" "$app_dir/${link##*/}"; then rm -- "$link"; fi
    done
    rm -- "$app_dir"
    rm -rf -- "$current"
    refresh_desktop
    printf '%s\n' 'Removed the per-user Context Actions app. Settings and files were kept.'
    exit 0
fi

source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
archive_licenses="$source_dir/usr/share/doc/context-actions/licenses/archive"
[[ -f $source_dir/VERSION && -x $source_dir/usr/bin/context-actions &&
   -x $source_dir/usr/bin/ContextActionsArchive &&
   -f $source_dir/usr/share/applications/$app_id.desktop &&
   -f $source_dir/usr/share/nautilus-python/extensions/context_actions.py &&
   -f $source_dir/usr/share/kio/servicemenus/$app_id.servicemenu.desktop &&
   -f $source_dir/usr/share/icons/hicolor/256x256/apps/$app_id.png &&
   -f $archive_licenses/libarchive.txt && -f $archive_licenses/zlib.txt &&
   -f $archive_licenses/liblzma.txt &&
   -f $source_dir/usr/share/doc/context-actions/LICENSE ]] ||
    fail 'Run the installer from an extracted Context Actions Linux app archive.'
version=$(<"$source_dir/VERSION")
valid_version "$version" || fail 'The archive contains an invalid Context Actions version.'

if [[ -n $current_version ]] && ! $allow_replace; then
    newest=$(printf '%s\n' "$current_version" "$version" | LC_ALL=C sort -V | tail -n 1)
    [[ $version == "$newest" && $version != "$current_version" ]] ||
        fail "Context Actions $current_version is installed. Use --allow-reinstall-or-downgrade to replace it with $version."
fi
for link in "${desktop_links[@]}"; do
    if [[ -e $link || -L $link ]]; then
        owned_link "$link" "$app_dir/${link##*/}" || fail "The installer does not own $link."
    fi
done

if ! LD_BIND_NOW=1 "$source_dir/usr/bin/context-actions" --help >/dev/null; then
    fail 'This archive needs a matching Linux architecture and compatible glibc and GTK 4 libraries.'
fi

for link in "${desktop_links[@]}"; do
    mkdir -p -- "${link%/*}"
done
stage=$(mktemp -d -- "$data_home/.context-actions-app.XXXXXXXX")
created_links=()
cleanup() {
    if ! owned_link "$app_dir" "$stage"; then
        for link in "${created_links[@]}"; do rm -f -- "$link"; done
        rm -rf -- "$stage"
    fi
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM

install -m 0755 -- "$source_dir/usr/bin/context-actions" "$stage/context-actions"
install -m 0755 -- "$source_dir/usr/bin/ContextActionsArchive" "$stage/ContextActionsArchive"
install -m 0755 -- "$source_dir/install.sh" "$stage/install.sh"
install -m 0644 -- "$source_dir/VERSION" "$stage/VERSION"
install -m 0644 -- "$source_dir/usr/share/doc/context-actions/LICENSE" "$stage/LICENSE"
install -d -- "$stage/licenses/archive"
install -m 0644 -- "$archive_licenses/libarchive.txt" "$archive_licenses/zlib.txt" \
    "$archive_licenses/liblzma.txt" "$stage/licenses/archive/"
install -m 0644 -- "$source_dir/usr/share/icons/hicolor/256x256/apps/$app_id.png" "$stage/$app_id.png"
install -m 0644 -- "$source_dir/usr/share/nautilus-python/extensions/context_actions.py" \
    "$stage/context_actions.py"

# Quote the path for Exec, then escape backslashes for the desktop file's string layer.
executable="$app_dir/context-actions"
executable=${executable//\\/\\\\}
executable=${executable//\"/\\\"}
executable=${executable//\$/\\\$}
executable=${executable//\`/\\\`}
executable=${executable//\\/\\\\}
executable=${executable//%/%%}
write_desktop_file() {
    local line
    while IFS= read -r line; do
        if [[ $line == 'Exec=context-actions '* ]]; then
            printf 'Exec=/usr/bin/env -- "%s" %s\n' "$executable" "${line#Exec=context-actions }"
        else printf '%s\n' "$line"; fi
    done < "$1" > "$2"
}
write_desktop_file "$source_dir/usr/share/applications/$app_id.desktop" "$stage/$app_id.desktop"
write_desktop_file "$source_dir/usr/share/kio/servicemenus/$app_id.servicemenu.desktop" \
    "$stage/$app_id.servicemenu.desktop"
chmod +x -- "$stage/$app_id.servicemenu.desktop"

for link in "${desktop_links[@]}"; do
    if [[ ! -L $link ]]; then
        ln -sT -- "$app_dir/${link##*/}" "$link"
        created_links+=("$link")
    fi
done
ln -s -- "$stage" "$stage/active"
mv -Tf -- "$stage/active" "$app_dir"
if [[ -n $current ]]; then rm -rf -- "$current"; fi
refresh_desktop
printf 'Installed Context Actions %s for this user.\nExecutable %s\n' "$version" "$app_dir/context-actions"
if command -v nautilus >/dev/null; then
    printf '%s\n' 'GNOME Files needs the Nautilus Python extension loader (python3-nautilus or nautilus-python).' \
        'Restart GNOME Files to load the Context Actions menu.'
fi

for tool in ffmpeg ffprobe qpdf; do
    if ! command -v "$tool" >/dev/null; then
        printf 'Install %s with your distribution package manager to use its conversion features.\n' "$tool" >&2
    fi
done
