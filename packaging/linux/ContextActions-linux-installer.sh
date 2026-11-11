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

package=$(zenity --file-selection --title='Select a Context Actions package' \
    --file-filter='Context Actions packages | *.deb *.rpm') || exit 0
package=$(realpath -- "$package") || fail 'Could not find the selected package.'
case "$package" in
    *.deb)
        command -v dpkg-deb >/dev/null || fail 'This system cannot install DEB packages.'
        manager=/usr/bin/apt-get
        [[ -x "$manager" ]] || fail 'This system cannot install DEB packages.'
        name=$(dpkg-deb --field "$package" Package) || fail 'Could not read the DEB package.'
        version=$(dpkg-deb --field "$package" Version) || fail 'Could not read the package version.'
        dpkg --validate-version "$version" >/dev/null 2>&1 || fail 'Could not read the package version.'
        format=deb
        ;;
    *.rpm)
        command -v rpm >/dev/null || fail 'This system cannot install RPM packages.'
        manager=/usr/bin/dnf
        [[ -x "$manager" ]] || fail 'This system cannot install RPM packages.'
        name=$(rpm -qp --queryformat '%{NAME}' "$package") || fail 'Could not read the RPM package.'
        version=$(rpm -qp --queryformat '%{EPOCHNUM}:%{VERSION}-%{RELEASE}' "$package") ||
            fail 'Could not read the package version.'
        [[ "$version" =~ ^[0-9]+:[[:alnum:]_.+~^]+-[[:alnum:]_.+~^]+$ ]] ||
            fail 'Could not read the package version.'
        format=rpm
        ;;
    *) fail 'Select a Context Actions DEB or RPM package.' ;;
esac
[[ "$name" == context-actions && -n "$version" ]] || fail 'Select a Context Actions package.'

installed_versions() {
    local entries status=0
    if [[ "$format" == deb ]]; then
        entries=$(dpkg-query -W -f='${db:Status-Status} ${Version}\n' context-actions) || status=$?
        [[ "$status" == 1 ]] && return 0
        [[ "$status" == 0 ]] || return "$status"
        printf '%s\n' "$entries" |
            sed -n '/^\(installed\|triggers-.*\|unpacked\|half-configured\|half-installed\) /s/^[^ ]* //p'
    else
        entries=$(LC_ALL=C rpm -q --queryformat '%{EPOCHNUM}:%{VERSION}-%{RELEASE}\n' context-actions) || status=$?
        [[ "$entries" == 'package context-actions is not installed' ]] && return 0
        [[ "$status" == 0 ]] || return "$status"
        printf '%s\n' "$entries"
    fi
}

compare_version() {
    if [[ "$format" == rpm ]]; then
        CONTEXT_ACTIONS_INCOMING_EVR="$version" CONTEXT_ACTIONS_INSTALLED_EVR="$1" \
            rpm --eval '%{lua:print(rpm.vercmp(os.getenv("CONTEXT_ACTIONS_INCOMING_EVR"), os.getenv("CONTEXT_ACTIONS_INSTALLED_EVR")))}'
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
        else
            action=reinstall
        fi
    elif [[ "$format" == deb ]]; then
        options+=(--allow-downgrades)
    fi
fi

log=$(mktemp)
trap 'rm -f -- "$log"' EXIT
if (
    status=0
    pkexec --disable-internal-agent /usr/bin/env "CONTEXT_ACTIONS_GUI_DOWNGRADE=$override" \
        DEBIAN_FRONTEND=noninteractive "$manager" "${options[@]}" "$action" "$package" >"$log" 2>&1 ||
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
