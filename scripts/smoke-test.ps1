[CmdletBinding()]
param([Parameter(Mandatory)][string]$AppDirectory, [Parameter(Mandatory)][string]$ReportDirectory)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$testDirectory = Join-Path $root ('build\smoke-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $testDirectory,$ReportDirectory | Out-Null
$previousData = $env:CONTEXT_ACTIONS_DATA_DIR
$env:CONTEXT_ACTIONS_DATA_DIR = Join-Path $testDirectory 'state'
$ffmpeg = Join-Path $AppDirectory 'engines\ffmpeg.exe'
$ffprobe = Join-Path $AppDirectory 'engines\ffprobe.exe'
$qpdf = Join-Path $AppDirectory 'engines\qpdf.exe'
$application = Join-Path $AppDirectory 'ContextActions.exe'
function Check([bool]$Value,[string]$Message) { if (-not $Value) { throw $Message } }
function Native([string]$File,[string[]]$Arguments) { & $File @Arguments; Check ($LASTEXITCODE -eq 0) "$File failed with $LASTEXITCODE" }
function App([string]$Action,[string]$Folder,[string[]]$Files,[string[]]$Options = @(),[int]$ExpectedExit = 0) {
    $output = Join-Path $testDirectory $Folder
    $report = Join-Path $testDirectory "$Folder-$([guid]::NewGuid().ToString('N')).txt"
    $arguments = @('--headless','--report',$report,'--action',$Action,'--output',$output) + $Options + @('--') + $Files
    # '&' waits for Win32 GUI applications when called from a script. No console or UI is displayed.
    & $application @arguments | Out-Null
    Check ($LASTEXITCODE -eq $ExpectedExit) "App $Action exit $LASTEXITCODE; report: $report"
    return @(Get-ChildItem -LiteralPath $output -File -ErrorAction SilentlyContinue)
}
try {
    $tar = Join-Path $env:SystemRoot 'System32\tar.exe'
    $archivePayload = Join-Path $testDirectory 'archive-payload.txt'
    $archiveSource = Join-Path $testDirectory 'repack-source.tar'
    Set-Content -LiteralPath $archivePayload -Value 'Context Actions archive smoke' -Encoding ascii
    Native $tar @('-cf',$archiveSource,'-C',$testDirectory,'archive-payload.txt')
    $archiveHash = (Get-FileHash -LiteralPath $archiveSource).Hash
    $repacked = @(App 'repack' 'repack' @($archiveSource) @('--format','tar.gz'))
    Check ($repacked.Count -eq 1 -and $repacked[0].Name -eq 'repack-source.tar.gz') 'Archive repack output is missing'
    $repackedContents = & $tar -xOf $repacked[0].FullName 'archive-payload.txt'
    Check ($LASTEXITCODE -eq 0 -and $repackedContents -eq 'Context Actions archive smoke') 'Repacked archive contents differ'
    Check ((Get-FileHash -LiteralPath $archiveSource).Hash -eq $archiveHash) 'Archive repack changed the source'

    $decoders = & $ffmpeg -hide_banner -decoders
    Check ($LASTEXITCODE -eq 0) 'Cannot list bundled FFmpeg decoders'
    Check ([bool]($decoders -match '^\s*V\S*\s+libdav1d\s')) 'Bundled FFmpeg requires libdav1d to decode NVIDIA AV1 recordings'
    $video = Join-Path $testDirectory 'Кино & 100% ! (test).mp4'
    Native $ffmpeg @('-v','error','-f','lavfi','-i','testsrc2=size=160x90:rate=15','-f','lavfi','-i','sine=frequency=440:sample_rate=44100','-t','1','-c:v','libx264','-c:a','aac',$video)
    $sourceHash = (Get-FileHash -LiteralPath $video).Hash
    $videoFixtures = [ordered]@{
        'mp4-av1.mp4' = @('-c:v','libaom-av1','-cpu-used','8','-crf','34','-c:a','copy')
        'matroska.mkv' = @('-c:v','libx265','-preset','ultrafast','-x265-params','log-level=error','-c:a','copy')
        'quicktime.mov' = @('-c','copy')
        'video.avi' = @('-c:v','mpeg4','-c:a','libmp3lame')
        'video-webm.webm' = @('-c:v','libvpx-vp9','-deadline','realtime','-c:a','libopus')
        'itunes.m4v' = @('-c','copy','-f','ipod')
        'windows.wmv' = @('-c:v','wmv2','-c:a','wmav2')
        'avchd-mts.mts' = @('-c:v','copy','-c:a','ac3','-mpegts_m2ts_mode','1','-f','mpegts')
        'avchd-m2ts.m2ts' = @('-c:v','copy','-c:a','ac3','-mpegts_m2ts_mode','1','-f','mpegts')
    }
    $videoInputs = @($video)
    foreach ($fixture in $videoFixtures.GetEnumerator()) {
        $path = Join-Path $testDirectory $fixture.Key
        Native $ffmpeg (@('-v','error','-i',$video) + $fixture.Value + @($path))
        $videoInputs += $path
    }
    $videoActions = [ordered]@{ compress='h264'; h264='h264'; h265='hevc'; telegram='h264' }
    foreach ($action in $videoActions.Keys) {
        $outputs = @(App $action $action $videoInputs)
        Check ($outputs.Count -eq $videoInputs.Count) "$action output count does not match input count"
        foreach ($file in $outputs) {
            Native $ffmpeg @('-v','error','-xerror','-i',$file.FullName,'-f','null','-')
            $info = & $ffprobe -v error -show_streams -of json $file.FullName | ConvertFrom-Json
            Check ($LASTEXITCODE -eq 0) "Cannot probe $($file.Name)"
            $videoStreams = @($info.streams | Where-Object codec_type -eq 'video')
            $audioStreams = @($info.streams | Where-Object codec_type -eq 'audio')
            Check ($file.Extension -eq '.mp4' -and $videoStreams.Count -eq 1 -and $videoStreams[0].codec_name -eq $videoActions[$action]) "Wrong video format: $($file.Name)"
            if ($action -eq 'telegram') {
                Check ($audioStreams.Count -eq 0) "Expected silent output: $($file.Name)"
            } else {
                Check ($audioStreams.Count -eq 1 -and $audioStreams[0].codec_name -eq 'aac') "Expected AAC audio: $($file.Name)"
            }
        }
    }
    $limited = @(App 'compress' 'limited' @($video) @('--target-mb','1'))
    Check ($limited.Count -eq 1 -and $limited[0].Length -le 1MB) 'Target size exceeded'
    $image = Join-Path $testDirectory 'Изображение.png'
    Native $ffmpeg @('-v','error','-f','lavfi','-i','color=c=red@0.5:s=64x48,format=rgba','-frames:v','1','-update','1',$image)
    $imageInputs = @($image)
    foreach ($extension in @('jpg','jpeg','webp','bmp','tif','tiff','gif')) {
        $path = Join-Path $testDirectory "image-$extension.$extension"
        Native $ffmpeg @('-v','error','-i',$image,'-frames:v','1','-update','1',$path)
        $imageInputs += $path
    }
    $imageFormats = [ordered]@{ png='png'; jpg='mjpeg'; webp='webp'; bmp='bmp'; tiff='tiff'; gif='gif' }
    foreach ($format in $imageFormats.Keys) {
        $converted = @(App 'image' "image-$format" $imageInputs @('--format',$format,'--width','32','--first-frame'))
        Check ($converted.Count -eq $imageInputs.Count) "$format output count does not match input count"
        foreach ($file in $converted) {
            $info = & $ffprobe -v error -select_streams v:0 -show_entries stream=codec_name,width,height -of json $file.FullName | ConvertFrom-Json
            Check ($LASTEXITCODE -eq 0) "Cannot probe $($file.Name)"
            $frame = $info.streams[0]
            Check ($file.Extension -eq ".$format" -and $frame.codec_name -eq $imageFormats[$format]) "Wrong image format: $($file.Name)"
            Check ($frame.width -eq 32 -and $frame.height -eq 24) "Incorrect image resize: $($file.Name)"
        }
    }
    $pdf = @(App 'pdf' 'pdf' @($image))
    Check ($pdf.Count -eq 1) 'Missing PDF'
    Native $qpdf @('--check',$pdf[0].FullName)
    $merged = @(App 'pdf' 'merged' (@($pdf[0].FullName) + $imageInputs) @('--first-frame'))
    Check ($merged.Count -eq 1) 'Missing merged PDF'
    Native $qpdf @('--check',$merged[0].FullName)
    $pages = & $qpdf --show-npages $merged[0].FullName
    Check ($LASTEXITCODE -eq 0 -and $pages -eq [string](1 + $imageInputs.Count)) 'PDF page merge failed'

    $audioSource = Join-Path $testDirectory 'Звук.wav'
    Native $ffmpeg @('-v','error','-i',$video,'-vn','-c:a','pcm_s16le',$audioSource)
    $audioFixtures = [ordered]@{
        'sound-mp3.mp3' = @('-c:a','libmp3lame')
        'sound-m4a.m4a' = @('-c:a','aac')
        'sound-aac.aac' = @('-c:a','aac','-f','adts')
        'sound-flac.flac' = @('-c:a','flac')
        'sound-ogg.ogg' = @('-c:a','libvorbis')
        'sound-oga.oga' = @('-c:a','libvorbis','-f','ogg')
        'sound-opus.opus' = @('-c:a','libopus')
        'sound-aif.aif' = @('-c:a','pcm_s16be','-f','aiff')
        'sound-aiff.aiff' = @('-c:a','pcm_s16be')
        'sound-wma.wma' = @('-c:a','wmav2')
    }
    $audioInputs = @($audioSource,$video)
    foreach ($fixture in $audioFixtures.GetEnumerator()) {
        $path = Join-Path $testDirectory $fixture.Key
        Native $ffmpeg (@('-v','error','-i',$audioSource) + $fixture.Value + @($path))
        $audioInputs += $path
    }
    $audioFormats = [ordered]@{ mp3='mp3'; m4a='aac'; wav='pcm_s16le'; flac='flac'; opus='opus' }
    foreach ($format in $audioFormats.Keys) {
        $options = @()
        if ($format -ne 'mp3') { $options = @('--format',$format) }
        $outputs = @(App 'audio' "audio-$format" $audioInputs $options)
        Check ($outputs.Count -eq $audioInputs.Count) "Missing $format audio output"
        foreach ($file in $outputs) {
            $streams = (& $ffprobe -v error -show_streams -of json $file.FullName | ConvertFrom-Json).streams
            Check ($LASTEXITCODE -eq 0 -and @($streams).Count -eq 1 -and $streams[0].codec_type -eq 'audio' -and
                $streams[0].codec_name -eq $audioFormats[$format] -and $file.Extension -eq ".$format") "Wrong audio format: $($file.Name)"
            Native $ffmpeg @('-v','error','-xerror','-i',$file.FullName,'-f','null','-')
        }
    }
    $silent = Join-Path $testDirectory 'silent.mp4'
    Native $ffmpeg @('-v','error','-i',$video,'-an','-c:v','copy',$silent)
    Check (@(App 'audio' 'audio-silent' @($silent) @() 1).Count -eq 0) 'Silent input produced an audio result'

    $twoTracks = Join-Path $testDirectory 'two-tracks.mp4'
    Native $ffmpeg @('-v','error','-i',$video,'-map','0:v:0','-map','0:a:0','-map','0:a:0','-c','copy',$twoTracks)
    $streamHashes = @('-v','error','-xerror','-map','0','-c','copy','-f','streamhash','-')
    $expected = & $ffmpeg -i $twoTracks @streamHashes
    Check ($LASTEXITCODE -eq 0 -and @($expected).Count -eq 3) 'Invalid stream-copy fixture'
    foreach ($format in @('mkv','mp4','mov')) {
        $options = @()
        if ($format -ne 'mkv') { $options = @('--format',$format) }
        $outputs = @(App 'remux' "remux-$format" @($twoTracks) $options)
        Check ($outputs.Count -eq 1 -and $outputs[0].Extension -eq ".$format") "Missing $format stream-copy result"
        $actual = & $ffmpeg -i $outputs[0].FullName @streamHashes
        Check ($LASTEXITCODE -eq 0 -and ($actual -join "`n") -eq ($expected -join "`n")) "Remux changed or dropped a stream in $format"
    }
    $subtitle = Join-Path $testDirectory 'track.srt'
    [IO.File]::WriteAllText($subtitle,"1`n00:00:00,000 --> 00:00:00,800`nTest subtitle`n")
    $subtitled = Join-Path $testDirectory 'subtitled.mkv'
    Native $ffmpeg @('-v','error','-i',$video,'-i',$subtitle,'-map','0','-map','1:s:0','-c','copy',$subtitled)
    Check (@(App 'remux' 'remux-incompatible' @($subtitled) @('--format','mp4') 1).Count -eq 0) 'Incompatible subtitles should prevent remux'

    $frames = @(App 'frame' 'video-frame' $videoInputs)
    Check ($frames.Count -eq $videoInputs.Count) 'Missing video frame'
    foreach ($file in $frames) {
        $stream = (& $ffprobe -v error -select_streams v:0 -show_entries stream=codec_name,width,height -of json $file.FullName | ConvertFrom-Json).streams[0]
        Check ($LASTEXITCODE -eq 0 -and $file.Extension -eq '.png' -and $stream.codec_name -eq 'png' -and
            $stream.width -eq 160 -and $stream.height -eq 90) "Wrong video frame: $($file.Name)"
    }
    $coverOnly = Join-Path $testDirectory 'cover-only.mp4'
    Native $ffmpeg @('-v','error','-i',$video,'-i',$image,'-map','0:a:0','-map','1:v:0','-c','copy','-disposition:v','attached_pic',$coverOnly)
    Check (@(App 'frame' 'frame-cover-only' @($coverOnly) @() 1).Count -eq 0) 'Embedded cover art should not count as a video frame'

    $mediaReports = @(App 'media-info' 'media-info' @($video,$audioSource,$image))
    Check ($mediaReports.Count -eq 3) 'Missing media information report'
    foreach ($file in $mediaReports) {
        $report = Get-Content -LiteralPath $file.FullName -Raw
        Check ($file.Extension -eq '.txt' -and $report -match '(?m)^format_name=\S+') "Missing container details: $($file.Name)"
    }
    $reportCodecs = $mediaReports | ForEach-Object { Get-Content -LiteralPath $_.FullName }
    foreach ($codec in @('h264','pcm_s16le','png')) {
        Check ($reportCodecs -contains "codec_name=$codec") "Missing media report codec: $codec"
    }

    $broken = Join-Path $testDirectory 'broken.mp4'; [IO.File]::WriteAllText($broken,'not a video')
    $batch = @(App 'h264' 'mixed-results' @($broken,(Join-Path $testDirectory 'missing.mp4'),$video) @() 1)
    Check ($batch.Count -eq 1) 'One failure should not stop the next file'
    $repeat = @(App 'h264' 'mixed-results' @($video))
    Check ($repeat.Count -eq 2) 'Existing output was overwritten'
    Check ((Get-FileHash -LiteralPath $video).Hash -eq $sourceHash) 'Source was modified'
    Check (@(Get-ChildItem -LiteralPath $testDirectory -Directory -Recurse -Filter '.context-actions-*').Count -eq 0) 'Temporary result was not cleaned'
    "PASS: $($videoInputs.Count * $videoActions.Count) video, $($imageInputs.Count * $imageFormats.Count) image and $($audioInputs.Count * $audioFormats.Count) audio conversions, lossless remux, video frames, media reports, size limit, PDF merge with all image formats, batch failure isolation, conflicts, Unicode paths, source preservation." |
        Set-Content -LiteralPath (Join-Path $ReportDirectory 'application-smoke.txt') -Encoding utf8
    Get-Content -LiteralPath (Join-Path $ReportDirectory 'application-smoke.txt')
} finally { $env:CONTEXT_ACTIONS_DATA_DIR = $previousData }
