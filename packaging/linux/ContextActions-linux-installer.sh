#!/usr/bin/env bash
set -euo pipefail

title='Context Actions Installer'
if ! command -v zenity >/dev/null; then
    printf 'Context Actions Installer requires Zenity.\n' >&2
    exit 1
fi

fail() {
    zenity --error --title="$title" --text="$1"
    exit 1
}

if (($# != 0)); then
    fail 'Open the installer without arguments to select a package.'
fi
command -v pkexec >/dev/null || fail 'Context Actions Installer requires PolicyKit (pkexec).'
command -v curl >/dev/null || fail 'Context Actions Installer requires curl to verify the release package.'

package=$(zenity --file-selection --title='Select a Context Actions package' \
    --file-filter='Context Actions packages | *.deb *.rpm *.pkg.tar.zst') || exit 0
[[ -f "$package" && -r "$package" ]] || fail 'Could not read the selected package.'
package_bytes=$(stat -Lc %s -- "$package") || fail 'Could not determine the package size.'
((package_bytes > 0 && package_bytes <= 512 * 1024 * 1024)) ||
    fail 'The selected package must be nonempty and no larger than 512 MiB.'

# Use the asset names and package versions produced by scripts/prepare-release.sh.
asset=${package##*/}
release_pattern='([0-9]+\.[0-9]+\.[0-9]+)'
if [[ "$asset" =~ ^context-actions_${release_pattern}_(amd64|arm64)\.deb$ ]]; then
    release=${BASH_REMATCH[1]}
    version=$release
    format=deb
    manager=/usr/bin/apt-get
elif [[ "$asset" =~ ^context-actions-${release_pattern}-1\.(x86_64|aarch64)\.rpm$ ]]; then
    release=${BASH_REMATCH[1]}
    version="0:$release-1"
    format=rpm
    manager=/usr/bin/dnf
elif [[ "$asset" =~ ^context-actions-${release_pattern}-1-x86_64\.pkg\.tar\.zst$ ]]; then
    release=${BASH_REMATCH[1]}
    version="$release-1"
    format=arch
    manager=/usr/bin/pacman
else
    fail 'Select an official Context Actions release package with its original filename.'
fi

release_url="https://github.com/ShortKedr/context-actions/releases/download/v$release"
checksums=$(curl --disable --fail --silent --show-error --location --max-time 30 \
    --proto '=https' --proto-redir '=https' "$release_url/SHA256SUMS.txt") ||
    fail "Could not retrieve the official checksums for Context Actions $release."
expected=$(awk -v asset="$asset" '$2 == asset { print $1 }' <<< "$checksums")
[[ "$expected" =~ ^[0-9a-f]{64}$ ]] || fail 'The selected package is missing from the official release checksums.'
actual=$(head -c "$((package_bytes + 1))" < "$package" | sha256sum) ||
    fail 'Could not calculate the package checksum.'
[[ "${actual%% *}" == "$expected" ]] || fail 'The selected package does not match the official release checksum.'

[[ -x "$manager" ]] || fail "This system cannot install $format packages."
if [[ "$format" == arch ]]; then
    command -v vercmp >/dev/null || fail 'This system cannot compare Arch package versions.'
fi

installed_versions() {
    local entries status=0
    if [[ "$format" == deb ]]; then
        entries=$(dpkg-query -W -f='${db:Status-Status} ${Version}\n' context-actions) || status=$?
        [[ "$status" == 1 ]] && return 0
        [[ "$status" == 0 ]] || return "$status"
        printf '%s\n' "$entries" |
            sed -n '/^\(installed\|triggers-.*\|unpacked\|half-configured\|half-installed\) /s/^[^ ]* //p'
    elif [[ "$format" == rpm ]]; then
        entries=$(LC_ALL=C rpm -q --queryformat '%{EPOCHNUM}:%{VERSION}-%{RELEASE}\n' context-actions) || status=$?
        [[ "$entries" == 'package context-actions is not installed' ]] && return 0
        [[ "$status" == 0 ]] || return "$status"
        printf '%s\n' "$entries"
    else
        entries=$(LC_ALL=C "$manager" -Q -- context-actions 2>&1) || status=$?
        [[ "$status" == 1 && "$entries" == "error: package 'context-actions' was not found" ]] && return 0
        [[ "$status" == 0 && "$entries" == 'context-actions '* ]] || return 1
        printf '%s\n' "${entries#context-actions }"
    fi
}

compare_version() {
    if [[ "$format" == rpm ]]; then
        CONTEXT_ACTIONS_INCOMING_EVR="$version" CONTEXT_ACTIONS_INSTALLED_EVR="$1" \
            rpm --eval '%{lua:print(rpm.vercmp(os.getenv("CONTEXT_ACTIONS_INCOMING_EVR"), os.getenv("CONTEXT_ACTIONS_INSTALLED_EVR")))}'
    elif [[ "$format" == arch ]]; then
        vercmp "$version" "$1"
    elif dpkg --compare-versions "$version" gt "$1"; then
        printf '1\n'
    elif dpkg --compare-versions "$version" lt "$1"; then
        printf '%s\n' -1
    else
        printf '0\n'
    fi
}

older_than=
same_version=false
current_versions=$(installed_versions) || fail 'Could not check the installed version of Context Actions.'
while IFS= read -r installed_version; do
    [[ -n "$installed_version" ]] || continue
    order=$(compare_version "$installed_version") || fail 'Could not compare package versions.'
    case "$order" in
        0) same_version=true ;;
        -1) older_than="$installed_version" ;;
        1) ;;
        *) fail 'Could not compare package versions.' ;;
    esac
done <<< "$current_versions"

option='Allow reinstall or downgrade'
description="Install Context Actions $version?"
if [[ "$same_version" == true ]]; then
    description="Reinstall Context Actions $version?"
elif [[ -n "$older_than" ]]; then
    description="Replace Context Actions $older_than with the older version $version?"
fi
choice=$(zenity --list --checklist --title="$title" --ok-label=Install \
    --text="$description" --width=460 --height=220 \
    --print-column=2 --hide-header --column='' --column='Installation option' FALSE "$option") || exit 0

options=(-y)
action=install
override=
if [[ "$same_version" == true || -n "$older_than" ]]; then
    [[ "$choice" == "$option" ]] || fail 'Select Allow reinstall or downgrade to replace the installed version.'
    override="$format:$version"
    if [[ "$same_version" == true ]]; then
        if [[ "$format" == deb ]]; then
            options+=(--reinstall)
        elif [[ "$format" == rpm ]]; then
            action=reinstall
        fi
    elif [[ "$format" == deb ]]; then
        options+=(--allow-downgrades)
    fi
fi

if [[ "$format" == arch ]]; then
    command_line=("$manager" --noconfirm -U --)
else
    command_line=("$manager" "${options[@]}" "$action")
fi

# The invoking user opens the selected path. The privileged process only reads
# its stdin and installs from a root-owned copy after checking those exact bytes.
root_install=$(cat <<'ROOT_INSTALL'
set -euo pipefail
expected=$1
asset=$2
export CONTEXT_ACTIONS_GUI_DOWNGRADE=$3 DEBIAN_FRONTEND=noninteractive
package_bytes=$4
shift 4
stage=$(/usr/bin/mktemp -d /tmp/context-actions-install.XXXXXXXX)
trap '/usr/bin/rm -rf -- "$stage"' EXIT
staged="$stage/$asset"
/usr/bin/head -c "$((package_bytes + 1))" > "$staged"
actual=$(/usr/bin/sha256sum < "$staged")
if [[ "$(/usr/bin/stat -c %s -- "$staged")" != "$package_bytes" || "${actual%% *}" != "$expected" ]]; then
    printf '%s\n' 'The package changed before installation. Download it again from the official release.' >&2
    exit 1
fi
# APT can read the verified local file through its _apt sandbox user.
/usr/bin/chmod 0644 -- "$staged"
/usr/bin/chmod 0755 -- "$stage"
"$@" "$staged" < /dev/null
ROOT_INSTALL
)

log=$(mktemp)
trap 'rm -f -- "$log"' EXIT
if (
    status=0
    pkexec --disable-internal-agent /bin/bash -c "$root_install" context-actions-install \
        "$expected" "$asset" "$override" "$package_bytes" "${command_line[@]}" >"$log" 2>&1 < "$package" ||
        status=$?
    printf '100\n'
    exit "$status"
) | zenity --progress --title="$title" --text='Installing Context Actions…' --pulsate --no-cancel --auto-close; then
    installed_versions | grep -Fx -- "$version" >/dev/null ||
        fail "The package manager did not install Context Actions $version."
    zenity --info --title="$title" --text='Context Actions installation finished.'
else
    zenity --text-info --title='Context Actions installation failed' --filename="$log" --width=620 --height=380 || :
    exit 1
fi
