#!/usr/bin/env bash
set -euo pipefail

build_dir=$(cd -- "${1:-build/macos}" && pwd)
output_dir=${2:-dist/macos}
packaging_dir=$(cd -- "$(dirname -- "$0")" && pwd)
stage_dir=$(mktemp -d "$build_dir/package-stage.XXXXXXXX")
trap 'rm -rf -- "$stage_dir"' EXIT

mkdir -p "$output_dir"
payload_dir="$stage_dir/payload"
package_dir="$stage_dir/packages"
cmake --install "$build_dir" --config Release --prefix "$payload_dir"
app="$payload_dir/ContextActions.app"
plist="$app/Contents/Info.plist"
version=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$plist")
bundle_version=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$plist")
identifier=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$plist")
lipo "$app/Contents/MacOS/ContextActions" -verify_arch arm64 x86_64
archive_engine="$app/Contents/MacOS/ContextActionsArchive"
lipo "$archive_engine" -verify_arch arm64 x86_64
codesign --force --sign - "$archive_engine"
codesign --force --sign - "$app"
codesign --verify --strict --all-architectures "$app"

name="ContextActions-$version-macos-universal"
ditto -c -k --keepParent "$app" "$output_dir/$name.zip"
mkdir -p "$package_dir"
pkgbuild --root "$payload_dir" --component-plist "$packaging_dir/component.plist" \
  --identifier "$identifier" --version "$bundle_version" --install-location /Applications \
  "$package_dir/app.pkg"

# The selected version check runs before the installer commits the app payload.
for policy in upgrade-only allow-replacement; do
  script_dir="$stage_dir/$policy"
  mkdir -p "$script_dir"
  sed -e "s/@BUNDLE_VERSION@/$bundle_version/g" -e "s/@POLICY@/$policy/g" \
    "$packaging_dir/preinstall.sh" > "$script_dir/preinstall"
  chmod +x "$script_dir/preinstall"
  pkgbuild --nopayload --scripts "$script_dir" --identifier "$identifier.$policy" \
    --version "$bundle_version" "$package_dir/$policy.pkg"
done

sed "s/@IDENTIFIER@/$identifier/g" "$packaging_dir/distribution.xml.in" > "$stage_dir/distribution.xml"
productbuild --distribution "$stage_dir/distribution.xml" --package-path "$package_dir" \
  "$output_dir/$name.pkg"
(
  cd "$output_dir"
  shasum -a 256 "$name.zip" > "$name.zip.sha256"
  shasum -a 256 "$name.pkg" > "$name.pkg.sha256"
)
