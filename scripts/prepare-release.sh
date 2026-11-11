#!/usr/bin/env bash
set -euo pipefail

version=$1
downloads=$2
destination=$3
mkdir -p "$destination"

collect() {
    local directory="$downloads/$1"
    local filename=$2
    local expected recorded actual
    test -s "$directory/$filename"
    read -r expected recorded < "$directory/$filename.sha256"
    recorded=${recorded%$'\r'}
    actual=$(sha256sum < "$directory/$filename")
    actual=${actual%% *}
    if [[ $recorded != "$filename" || $actual != "${expected,,}" ]]; then
        echo "Checksum mismatch for $filename" >&2
        return 1
    fi
    install -m 0644 -- "$directory/$filename" "$destination/$filename"
    printf '%s  %s\n' "$actual" "$filename" >> "$destination/SHA256SUMS.txt"
}

: > "$destination/SHA256SUMS.txt"
collect ContextActions-win-x64-app ContextActions-win-x64.zip
collect ContextActions-win-x64 "ContextActions-$version-win-x64-setup.exe"
collect ContextActions-macos-universal-app "ContextActions-$version-macos-universal.zip"
collect ContextActions-macos-universal-installer "ContextActions-$version-macos-universal.pkg"
collect ContextActions-linux-amd64-installers ContextActions-linux-installer.sh
for deb_arch in amd64 arm64; do
    case $deb_arch in
        amd64) machine=x86_64 ;;
        arm64) machine=aarch64 ;;
    esac
    collect "ContextActions-linux-$deb_arch-app" "context-actions-$version-linux-$machine.tar.gz"
    collect "ContextActions-linux-$deb_arch-installers" "context-actions_${version}_$deb_arch.deb"
    collect "ContextActions-linux-$deb_arch-installers" "context-actions-$version-1.$machine.rpm"
done
