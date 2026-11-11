#!/bin/bash
set -euo pipefail

incoming_version='@BUNDLE_VERSION@'
policy='@POLICY@'
app="${3%/}/Applications/ContextActions.app"
version_pattern='^[0-9]+(\.[0-9]+){0,2}$'

if [[ ! -e "$app" && ! -L "$app" ]]; then
  exit 0
fi

if ! installed_version=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$app/Contents/Info.plist") ||
   [[ ! "$installed_version" =~ $version_pattern ]]; then
  echo 'Cannot determine the installed Context Actions version.' >&2
  exit 1
fi

IFS=. read -r -a incoming_parts <<< "$incoming_version"
IFS=. read -r -a installed_parts <<< "$installed_version"
for i in 0 1 2; do
  incoming=$((10#${incoming_parts[i]:-0}))
  installed=$((10#${installed_parts[i]:-0}))
  if (( incoming > installed )); then
    exit 0
  fi
  if (( incoming < installed )); then
    break
  fi
done

if [[ "$policy" == allow-replacement && -z "${COMMAND_LINE_INSTALL:-}" ]]; then
  exit 0
fi
echo "Context Actions $installed_version is installed. Select Allow reinstall or downgrade in the macOS Installer to install $incoming_version." >&2
exit 1
