#!/usr/bin/env bash
set -euo pipefail

build_dir=$(cd -- "${1:-build/macos}" && pwd)
output_dir=${2:-dist/macos}
packaging_dir=$(cd -- "$(dirname -- "$0")" && pwd)
stage_dir=$(mktemp -d "$build_dir/package-stage.XXXXXXXX")
trap 'rm -rf -- "$stage_dir"' EXIT

mkdir -p "$output_dir"
cmake --install "$build_dir" --config Release --prefix "$stage_dir"
app="$stage_dir/ContextActions.app"
plist="$app/Contents/Info.plist"
version=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$plist")
identifier=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$plist")
lipo -verify_arch arm64 x86_64 "$app/Contents/MacOS/ContextActions"
codesign --force --sign - "$app"
codesign --verify --strict --all-architectures "$app"

name="ContextActions-$version-macos-universal"
ditto -c -k --keepParent "$app" "$output_dir/$name.zip"
pkgbuild --root "$stage_dir" --component-plist "$packaging_dir/component.plist" \
  --identifier "$identifier" --version "$version" --install-location /Applications \
  "$output_dir/$name.pkg"
(
  cd "$output_dir"
  shasum -a 256 "$name.zip" > "$name.zip.sha256"
  shasum -a 256 "$name.pkg" > "$name.pkg.sha256"
)
