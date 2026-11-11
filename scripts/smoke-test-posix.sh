#!/usr/bin/env bash
set -euo pipefail

app="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
mkdir -p "$2"
work="$(mktemp -d "$2/context-actions-smoke.XXXXXX")"
export CONTEXT_ACTIONS_DATA_DIR="$work/state"
export LC_ALL=C
video="$work/Café & 100%.mp4"
image="$work/Кадр & 100%.png"
document="$work/Notes & 100%.txt"

run() {
    local report="$work/$1.log"
    shift
    if ! "$app" --headless --report "$report" "$@"; then
        cat "$report" >&2
        return 1
    fi
    grep -qx 'success=1 failed=0 cancelled=0' "$report"
}

ffmpeg -hide_banner -loglevel error -nostdin -f lavfi -i 'color=s=96x64:r=12:d=1' \
    -c:v libx264 -pix_fmt yuv420p "$video"
ffmpeg -hide_banner -loglevel error -nostdin -i "$video" -frames:v 1 -threads 1 "$image"

run h264 --action h264 --width 64 --mute -- "$video"
run h265 --action h265 --mute -- "$video"
test "$(ffprobe -v error -select_streams v:0 -show_entries stream=codec_name,width \
    -of csv=p=0 "$work/Café & 100%_h264.mp4")" = h264,64
test "$(ffprobe -v error -select_streams v:0 -show_entries stream=codec_name \
    -of csv=p=0 "$work/Café & 100%_h265.mp4")" = hevc

run image --action image --format webp -- "$image"
test -s "$work/Кадр & 100%_image.webp"
run pdf --action pdf -- "$image"
pdf="$work/Кадр & 100%_pdf.pdf"
test "$(qpdf --show-npages "$pdf")" = 1
newline_pdf="$work/"$'Existing\nPage.pdf'
cp "$pdf" "$newline_pdf"
run merge --action pdf -- "$image" "$newline_pdf"
test "$(qpdf --show-npages "$work/Кадр & 100%_pdf (2).pdf")" = 2

printf '%s\n' 'Batch rename test' > "$document"
run rename --action rename --pattern '{name}_{n}' -- "$document"
test ! -e "$document"
test -s "$work/Notes & 100%_1.txt"

echo "Headless media, PDF assembly and rename checks passed. Reports are in $work"
