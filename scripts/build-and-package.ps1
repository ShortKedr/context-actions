[CmdletBinding()]
param([switch]$CompileOnly, [switch]$Offline, [switch]$SkipTests, [switch]$Release)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$config = Get-Content -LiteralPath (Join-Path $root 'dependencies.json') -Raw | ConvertFrom-Json
$cache = Join-Path $root '.build-tools'
$stage = Join-Path $root 'dist\ContextActions'
$reports = Join-Path $root 'dist\reports'
$installer = Join-Path $root "dist\ContextActions-$($config.version)-win-x64-setup.exe"
[xml]$uiPackageConfig = Get-Content -LiteralPath (Join-Path $root 'src\ui\packages.config') -Raw
$uiPackages = @($uiPackageConfig.packages.package | ForEach-Object { [pscustomobject]@{ id=$_.id; version=$_.version } })
function Reset-OutputDirectory([string]$Path) {
    $resolved = [IO.Path]::GetFullPath($Path)
    if (-not $resolved.StartsWith(($root + '\'),[StringComparison]::OrdinalIgnoreCase) -or
        ($resolved -ne $stage -and $resolved -ne $reports -and $resolved -ne (Join-Path $root 'build\identity'))) { throw "Refusing to reset $resolved" }
    if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
    New-Item -ItemType Directory -Path $resolved -Force | Out-Null
}
function Invoke-Tool([string]$File, [string[]]$Arguments) {
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$File exited with code $LASTEXITCODE" }
}
function Assert-PublicBinary([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    $ascii = [Text.Encoding]::GetEncoding(28591).GetString($bytes)
    $strings = @($ascii, [Text.Encoding]::Unicode.GetString($bytes))
    if ($bytes.Length -gt 1) {
        $strings += [Text.Encoding]::Unicode.GetString($bytes,1,$bytes.Length-1)
    }
    foreach ($directory in @($root,$env:USERPROFILE) | Where-Object { $_ }) {
        foreach ($value in $strings) {
            if ($value.Replace('/','\').IndexOf($directory,[StringComparison]::OrdinalIgnoreCase) -ge 0) {
                throw "Build path found in $([IO.Path]::GetFileName($Path))."
            }
        }
    }
    if ($ascii -match '(?is)RSDS.{20}(?:[A-Z]:[\\/]|\\\\)[^\x00]{1,1024}\.pdb\x00') {
        throw "Absolute PDB path found in $([IO.Path]::GetFileName($Path))."
    }
}
function Get-Dependency([string]$Name) {
    $dependency = $config.$Name
    $archive = if ($Name -eq 'inno') { 'inno.zip' } else { "$($dependency.root).zip" }
    $zip = Join-Path $cache "downloads\$archive"
    $directory = Join-Path $cache $Name
    if (-not (Test-Path -LiteralPath $zip)) {
        if ($Offline) { throw "Offline dependency missing: $zip" }
        Invoke-WebRequest -Uri $dependency.url -OutFile $zip -UseBasicParsing
    }
    if ((Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash -ne $dependency.sha256) { throw "SHA-256 mismatch: $zip. Remove the invalid archive and retry." }
    # Extract the verified archive on each packaging run; cached expanded files are not trusted inputs.
    if (Test-Path -LiteralPath $directory) {
        $resolved = [IO.Path]::GetFullPath($directory)
        if ($resolved -ne (Join-Path $cache $Name) -or $Name -notin @('ffmpeg','qpdf','inno')) { throw "Invalid dependency directory: $resolved" }
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
    Expand-Archive -LiteralPath $zip -DestinationPath $directory
    return Join-Path $directory $dependency.root
}
$previousTemp = $env:TEMP; $previousTmp = $env:TMP
Push-Location $root
try {
    $env:TEMP = Join-Path $root 'build\temp'; $env:TMP = $env:TEMP
    New-Item -ItemType Directory -Force -Path $env:TEMP,(Join-Path $cache 'downloads') | Out-Null
    # Invalidate old deliverables before the first fallible build step.
    if (Test-Path -LiteralPath $installer) { Remove-Item -LiteralPath $installer -Force }
    if (Test-Path -LiteralPath "$installer.sha256") { Remove-Item -LiteralPath "$installer.sha256" -Force }
    Reset-OutputDirectory $reports
    $testStatus = [ordered]@{ core='not run'; shellContract='not run'; application='not run'; ui='not run'; shell='not run'; office='not run'; skipTests=[bool]$SkipTests }
    $testStatus | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $reports 'test-status.json') -Encoding utf8
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $installation = @(& $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json)
    if ($LASTEXITCODE -ne 0 -or $installation.Count -ne 1) { throw 'Install Visual Studio 2022 Build Tools with C++ and Windows SDK 10.0.22621.0.' }
    $vs = $installation[0].installationPath
    $cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    if (-not (Test-Path -LiteralPath $cmake)) { $cmake = (Get-Command cmake -ErrorAction Stop).Source }
    $ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
    Invoke-Tool $cmake @('--preset','windows-x64')
    Invoke-Tool $cmake @('--build','--preset','release')
    & (Join-Path $PSScriptRoot 'build-ui.ps1') -Offline:$Offline
    if (-not $?) { throw 'WinUI build failed.' }
    if (-not $SkipTests) {
        Invoke-Tool $ctest @('--preset','release','--output-junit',(Join-Path $reports 'core-tests.xml'))
        $testStatus.core = 'passed'
        $testStatus.shellContract = 'passed'
        $testStatus | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $reports 'test-status.json') -Encoding utf8
    }
    if ($CompileOnly) { return }
    $ffmpeg = Get-Dependency 'ffmpeg'; $qpdf = Get-Dependency 'qpdf'; $inno = Get-Dependency 'inno'
    Reset-OutputDirectory $stage
    Invoke-Tool $cmake @('--install','build','--config','Release','--prefix',$stage)
    $uiOutput = Join-Path $root 'build\winui'
    $uiFiles = @{}
    foreach ($file in Get-ChildItem -LiteralPath $uiOutput -Recurse -File | Where-Object { $_.Extension -notin @('.pdb','.lib','.exp') }) {
        $relative = $file.FullName.Substring($uiOutput.Length + 1)
        $uiFiles[$relative] = $true
        $destination = Join-Path $stage $relative
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $destination
    }
    foreach ($name in @('ContextActions.exe','ContextActionsShell.dll')) {
        Assert-PublicBinary (Join-Path $stage $name)
    }
    $engines = Join-Path $stage 'engines'; $licenses = Join-Path $stage 'licenses'
    New-Item -ItemType Directory -Force -Path $engines,(Join-Path $licenses 'ffmpeg'),(Join-Path $licenses 'qpdf'),(Join-Path $stage 'packaging') | Out-Null
    Copy-Item -LiteralPath (Join-Path $ffmpeg 'bin\ffmpeg.exe'),(Join-Path $ffmpeg 'bin\ffprobe.exe') -Destination $engines
    Copy-Item -LiteralPath (Join-Path $qpdf 'bin\qpdf.exe') -Destination $engines
    Get-ChildItem -LiteralPath (Join-Path $qpdf 'bin') -Filter '*.dll' | Copy-Item -Destination $engines
    Get-ChildItem -LiteralPath $ffmpeg -File | Copy-Item -Destination (Join-Path $licenses 'ffmpeg')
    Copy-Item -LiteralPath (Join-Path $qpdf 'share') -Destination (Join-Path $licenses 'qpdf') -Recurse
    foreach ($package in $uiPackages) {
        $packageName = "$($package.id).$($package.version)"
        $packageDirectory = Join-Path $cache "nuget\$packageName"
        $licenseDirectory = Join-Path $licenses "windows-app-sdk\$packageName"
        New-Item -ItemType Directory -Force -Path $licenseDirectory | Out-Null
        Get-ChildItem -LiteralPath $packageDirectory -File | Where-Object { $_.Name -match '^(license|notice)' } | Copy-Item -Destination $licenseDirectory
    }
    Copy-Item -LiteralPath 'src\ui\packages.config' -Destination (Join-Path $licenses 'windows-app-sdk\packages.config')
    Copy-Item -LiteralPath 'dependencies.json' -Destination $stage
    Copy-Item -LiteralPath 'packaging\register-shell.ps1' -Destination (Join-Path $stage 'packaging')
    Copy-Item -LiteralPath 'docs\build.md','docs\preview-license.txt' -Destination (Join-Path $stage 'docs')
    if (-not $SkipTests) {
        & (Join-Path $PSScriptRoot 'smoke-test.ps1') -AppDirectory $stage -ReportDirectory $reports
        if (-not $?) { throw 'Application smoke test failed.' }
        $testStatus.application = 'passed'
        $testStatus | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $reports 'test-status.json') -Encoding utf8
    }
    $identity = Join-Path $root 'build\identity'; Reset-OutputDirectory $identity
    New-Item -ItemType Directory -Path (Join-Path $identity 'Assets') | Out-Null
    Copy-Item -LiteralPath 'packaging\AppxManifest.xml' -Destination $identity
    Copy-Item -LiteralPath 'resources\logo-44.png','resources\logo-150.png' -Destination (Join-Path $identity 'Assets')
    $sdk = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\bin\$($config.toolchain.sdk)\x64\MakeAppx.exe"
    # Sparse packages refer to external EXE/DLL files. /nv is Microsoft's documented recipe;
    # the files are checked in the external stage, not required inside this identity-only MSIX.
    foreach ($name in @('ContextActions.exe','ContextActionsShell.dll')) {
        if (-not (Test-Path -LiteralPath (Join-Path $stage $name) -PathType Leaf)) { throw "Missing external content: $name" }
    }
    Invoke-Tool $sdk @('pack','/d',$identity,'/p',(Join-Path $stage 'packaging\ContextActions.msix'),'/o','/nv')
    Copy-Item -LiteralPath $identity -Destination (Join-Path $stage 'packaging\development') -Recurse
    $installerArgs = @('/Q',"/DStageDir=$stage","/DOutputDir=$(Join-Path $root 'dist')","/DAppVersion=$($config.version)")
    $downloadedPayloadBytes = 0
    if ($Release) {
        $ffmpegNotices = @(Get-ChildItem -LiteralPath (Join-Path $licenses 'ffmpeg') -File)
        $qpdfNotices = @(Get-ChildItem -LiteralPath (Join-Path $licenses 'qpdf\share') -File -Recurse)
        $qpdfLibraries = @(Get-ChildItem -LiteralPath $engines -Filter '*.dll' -File)
        $ffmpegBytes = (Get-Item -LiteralPath (Join-Path $engines 'ffmpeg.exe')).Length
        $ffprobeBytes = (Get-Item -LiteralPath (Join-Path $engines 'ffprobe.exe')).Length
        $qpdfBytes = (Get-Item -LiteralPath (Join-Path $engines 'qpdf.exe')).Length
        $ffmpegNoticeBytes = ($ffmpegNotices | Measure-Object -Property Length -Sum).Sum
        $qpdfNoticeBytes = ($qpdfNotices | Measure-Object -Property Length -Sum).Sum
        $qpdfLibraryBytes = ($qpdfLibraries | Measure-Object -Property Length -Sum).Sum
        $downloadedPayloadBytes = $ffmpegBytes + $ffprobeBytes + $qpdfBytes + $ffmpegNoticeBytes + $qpdfNoticeBytes + $qpdfLibraryBytes
        $installerArgs += @(
            '/DDownloadEngines',
            "/DFFmpegUrl=$($config.ffmpeg.url)", "/DFFmpegHash=$($config.ffmpeg.sha256)", "/DFFmpegRoot=$($config.ffmpeg.root)",
            "/DQpdfUrl=$($config.qpdf.url)", "/DQpdfHash=$($config.qpdf.sha256)", "/DQpdfRoot=$($config.qpdf.root)",
            "/DFFmpegBytes=$ffmpegBytes", "/DFFprobeBytes=$ffprobeBytes", "/DFFmpegNoticeBytes=$ffmpegNoticeBytes",
            "/DQpdfBytes=$qpdfBytes", "/DQpdfLibraryBytes=$qpdfLibraryBytes", "/DQpdfNoticeBytes=$qpdfNoticeBytes"
        )
        foreach ($directory in @($engines,(Join-Path $licenses 'ffmpeg'),(Join-Path $licenses 'qpdf'))) {
            $resolved = (Resolve-Path -LiteralPath $directory).ProviderPath
            if ($resolved -ne $directory -or -not $resolved.StartsWith(($stage + '\'),[StringComparison]::OrdinalIgnoreCase)) {
                throw "Refusing to remove $resolved from the release stage"
            }
            Remove-Item -LiteralPath $resolved -Recurse -Force
        }
    }
    $inventory = @(Get-ChildItem -LiteralPath $stage -Recurse -File | ForEach-Object {
        $relative = $_.FullName.Substring($stage.Length + 1)
        $component = if ($relative -match 'ffmpeg|ffprobe') { 'FFmpeg' } elseif ($relative.StartsWith('engines\') -or $relative.StartsWith('licenses\qpdf\')) { 'qpdf and runtime' } elseif ($relative.StartsWith('licenses\windows-app-sdk\') -or ($uiFiles.ContainsKey($relative) -and $relative -notmatch '^(ContextActions\.|App\.xbf$|MainWindow\.xbf$)')) { 'Windows App SDK' } else { 'Context Actions' }
        [pscustomobject]@{ path=$relative; component=$component; bytes=$_.Length; sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
    })
    $inventory | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $reports 'components.json') -Encoding utf8
    $toolchain = [ordered]@{ cmake=(& $cmake --version | Select-Object -First 1); visualStudioVersion=$installation[0].installationVersion; sdk=$config.toolchain.sdk; uiBackend='WinUI 3 / C++/WinRT'; windowsAppSdkSelfContained=$true; uiPackages=$uiPackages; dependencies=$config; releaseInstallerDownloadsEngines=[bool]$Release }
    $toolchain | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $reports 'build-inputs.json') -Encoding utf8
    Invoke-Tool (Join-Path $inno 'ISCC.exe') ($installerArgs + 'packaging\context-actions.iss')
    $installerBytes = (Get-Item -LiteralPath $installer).Length
    $installedBytes = ($inventory | Measure-Object -Property bytes -Sum).Sum + $downloadedPayloadBytes
    $size = [ordered]@{ version=$config.version; installerBytes=$installerBytes; installedPayloadBytes=$installedBytes; downloadedPayloadBytes=$downloadedPayloadBytes; installerMB=[math]::Round($installerBytes/1000000,2); installedPayloadMB=[math]::Round($installedBytes/1000000,2); installerMiB=[math]::Round($installerBytes/1MB,2); installedPayloadMiB=[math]::Round($installedBytes/1MB,2); targetMB=100; limitMB=300; withinLimit=($installerBytes -le 300000000); shellTested=$false; officeTested=$false }
    $size | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $reports 'size.json') -Encoding utf8
    $hash = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash
    "$hash  $([IO.Path]::GetFileName($installer))" | Set-Content -LiteralPath "$installer.sha256" -Encoding ascii
    if ($installerBytes -gt 300000000) { throw "Installer exceeds 300 MB. Review dist/reports/size.json and components.json with the user; do not silently remove components or raise the limit." }
    $size | ConvertTo-Json
} finally {
    $env:TEMP = $previousTemp; $env:TMP = $previousTmp
    Pop-Location
}
