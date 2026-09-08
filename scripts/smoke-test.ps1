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
    $report = Join-Path $testDirectory "$Folder.txt"
    $arguments = @('--headless','--report',$report,'--action',$Action,'--output',$output) + $Options + @('--') + $Files
    # '&' waits for Win32 GUI applications when called from a script. No console or UI is displayed.
    & $application @arguments | Out-Null
    Check ($LASTEXITCODE -eq $ExpectedExit) "App $Action exit $LASTEXITCODE; report: $report"
    return @(Get-ChildItem -LiteralPath $output -File -ErrorAction SilentlyContinue)
}
try {
    $video = Join-Path $testDirectory 'Кино & 100% ! (test).mp4'
    Native $ffmpeg @('-v','error','-f','lavfi','-i','testsrc2=size=160x90:rate=15','-f','lavfi','-i','sine=frequency=440:sample_rate=44100','-t','1','-c:v','libx264','-c:a','aac',$video)
    $sourceHash = (Get-FileHash -LiteralPath $video).Hash
    foreach ($action in @('h264','h265','telegram')) {
        $outputs = @(App $action $action @($video))
        Check ($outputs.Count -eq 1) "$action result missing"
        Native $ffmpeg @('-v','error','-i',$outputs[0].FullName,'-f','null','-')
        $info = (& $ffprobe -v error -show_streams -of json $outputs[0].FullName | ConvertFrom-Json).streams
        $codec = if ($action -eq 'h265') { 'hevc' } else { 'h264' }
        Check (@($info | Where-Object { $_.codec_type -eq 'video' -and $_.codec_name -eq $codec }).Count -eq 1) "Wrong $action codec"
        if ($action -eq 'telegram') { Check (@($info | Where-Object codec_type -eq 'audio').Count -eq 0) 'Telegram has audio' }
    }
    $limited = @(App 'compress' 'limited' @($video) @('--target-mb','1'))
    Check ($limited.Count -eq 1 -and $limited[0].Length -le 1MB) 'Target size exceeded'
    $image = Join-Path $testDirectory 'Изображение.png'
    Native $ffmpeg @('-v','error','-f','lavfi','-i','color=c=red@0.5:s=64x48,format=rgba','-frames:v','1','-update','1',$image)
    foreach ($format in @('png','jpg','webp','bmp','tiff','gif')) {
        $converted = @(App 'image' "image-$format" @($image) @('--format',$format,'--width','32'))
        Check ($converted.Count -eq 1) "Missing $format image"
        $dimensions = (& $ffprobe -v error -show_entries stream=width,height -of json $converted[0].FullName | ConvertFrom-Json).streams[0]
        Check ($dimensions.width -eq 32 -and $dimensions.height -eq 24) 'Incorrect image resize'
    }
    $pdf = @(App 'pdf' 'pdf' @($image))
    Check ($pdf.Count -eq 1) 'Missing PDF'
    Native $qpdf @('--check',$pdf[0].FullName)
    $merged = @(App 'pdf' 'merged' @($pdf[0].FullName,$image))
    $pages = & $qpdf --show-npages $merged[0].FullName
    Check ($pages -eq '2') 'PDF page merge failed'
    $broken = Join-Path $testDirectory 'broken.mp4'; [IO.File]::WriteAllText($broken,'not a video')
    $batch = @(App 'h264' 'mixed-results' @($broken,(Join-Path $testDirectory 'missing.mp4'),$video) @() 1)
    Check ($batch.Count -eq 1) 'One failure should not stop the next file'
    $repeat = @(App 'h264' 'h264' @($video))
    Check ($repeat.Count -eq 2) 'Existing output was overwritten'
    Check ((Get-FileHash -LiteralPath $video).Hash -eq $sourceHash) 'Source was modified'
    Check (@(Get-ChildItem -LiteralPath $testDirectory -Directory -Recurse -Filter '.context-actions-*').Count -eq 0) 'Temporary result was not cleaned'
    'PASS: short H.264/H.265/Telegram and size-limited video, six image outputs, PDF create/merge, batch failure isolation, conflicts, Unicode paths, source preservation.' |
        Set-Content -LiteralPath (Join-Path $ReportDirectory 'application-smoke.txt') -Encoding utf8
    Get-Content -LiteralPath (Join-Path $ReportDirectory 'application-smoke.txt')
} finally { $env:CONTEXT_ACTIONS_DATA_DIR = $previousData }
