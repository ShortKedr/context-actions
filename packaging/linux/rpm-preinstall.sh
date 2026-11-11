# RPM expands spec macros when building this scriptlet. Double percent signs
# preserve the query and Lua macros for the installed system.
set -eu

incoming_version='%{?epoch:%{epoch}}%{!?epoch:0}:%{version}-%{release}'
if installed_versions=$(LC_ALL=C rpm -q --queryformat '%%{EPOCHNUM}:%%{VERSION}-%%{RELEASE}\n' context-actions); then
    while IFS= read -r installed_version; do
        order=$(CONTEXT_ACTIONS_INCOMING_EVR="$incoming_version" CONTEXT_ACTIONS_INSTALLED_EVR="$installed_version" \
            rpm --eval '%%{lua:print(rpm.vercmp(os.getenv("CONTEXT_ACTIONS_INCOMING_EVR"), os.getenv("CONTEXT_ACTIONS_INSTALLED_EVR")))}')
        if [ "$order" = 1 ]; then
            continue
        fi
        if { [ "$order" = -1 ] || [ "$order" = 0 ]; } &&
            [ "${CONTEXT_ACTIONS_GUI_DOWNGRADE:-}" = "rpm:$incoming_version" ]; then
            continue
        fi

        printf 'Context Actions %s is installed. Setup cannot replace it with %s. Confirm a reinstall or downgrade in the desktop installer.\n' \
            "$installed_version" "$incoming_version" >&2
        exit 1
    done <<EOF
$installed_versions
EOF
elif [ "$installed_versions" != "package context-actions is not installed" ]; then
    printf 'Could not check the installed version of Context Actions.\n' >&2
    exit 1
fi
