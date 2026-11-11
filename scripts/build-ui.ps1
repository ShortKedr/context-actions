[CmdletBinding()]
param([switch]$Offline)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$components = @('Microsoft.VisualStudio.ComponentGroup.UWP.VC.BuildTools','Microsoft.VisualStudio.ComponentGroup.UWP.BuildTools')
$vs = & $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 $components -property installationPath
if (-not $vs) {
    $components = @('Microsoft.VisualStudio.Component.WindowsAppSdkSupport.Cpp','Microsoft.VisualStudio.ComponentGroup.UWP.Support','Microsoft.VisualStudio.ComponentGroup.UWP.VC')
    $vs = & $vswhere -latest -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 $components -property installationPath
}
if (-not $vs) {
    throw 'WinUI build support is missing. In Visual Studio Installer, add C++ WinUI app development build tools and Universal Windows Platform build tools to VS 2022 Build Tools. This script never installs system components.'
}
$packages = Join-Path $root '.build-tools\nuget'
$config = Join-Path $root 'src\ui\packages.config'
$nuget = Join-Path $root '.build-tools\downloads\nuget-6.14.0.exe'
$nugetHash = '92DBED160DDEE0F64B901E907439E021211B428E57C089ECC12FC38DCC4BD9A5'
$variables = @('TEMP','TMP','NUGET_PACKAGES','NUGET_HTTP_CACHE_PATH','NUGET_PLUGINS_CACHE_PATH')
$previous = @{}
foreach ($name in $variables) { $previous[$name] = [Environment]::GetEnvironmentVariable($name,'Process') }
Push-Location $root
try {
    $env:TEMP = Join-Path $root 'build\temp'; $env:TMP = $env:TEMP
    $env:NUGET_PACKAGES = $packages
    $env:NUGET_HTTP_CACHE_PATH = Join-Path $root '.build-tools\nuget-http'
    $env:NUGET_PLUGINS_CACHE_PATH = Join-Path $root '.build-tools\nuget-plugins'
    New-Item -ItemType Directory -Force -Path $env:TEMP,(Split-Path -Parent $nuget) | Out-Null
    if (-not (Test-Path -LiteralPath $nuget)) {
        if ($Offline) { throw "Offline build: missing $nuget" }
        Invoke-WebRequest -Uri 'https://dist.nuget.org/win-x86-commandline/v6.14.0/nuget.exe' -OutFile $nuget
    }
    if ((Get-FileHash -LiteralPath $nuget -Algorithm SHA256).Hash -ne $nugetHash) { throw "NuGet SHA-256 mismatch: $nuget" }
    [xml]$packageList = Get-Content -LiteralPath $config -Raw
    $missing = @($packageList.packages.package | Where-Object { -not (Test-Path -LiteralPath (Join-Path $packages "$($_.id).$($_.version)")) })
    if ($missing.Count) {
        if ($Offline) { throw "Offline build: missing packages $($missing.id -join ', ')" }
        & $nuget restore $config -PackagesDirectory $packages -ConfigFile 'src\ui\NuGet.Config' -DirectDownload -NonInteractive -Verbosity quiet
        if ($LASTEXITCODE -ne 0) { throw 'WinUI package restore failed.' }
    }
    $cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    if (-not (Test-Path -LiteralPath $cmake)) { $cmake = (Get-Command cmake -ErrorAction Stop).Source }
    & $cmake --preset windows-x64
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    & $cmake --build --preset release --target actions_core
    if ($LASTEXITCODE -ne 0) { throw 'Core build failed.' }
    & (Join-Path $vs 'MSBuild\Current\Bin\MSBuild.exe') 'src\ui\ContextActions.vcxproj' /p:Configuration=Release /p:Platform=x64 /m:2 /v:quiet /nologo /nodeReuse:false '/flp:logfile=build/winui-build.log;verbosity=normal'
    if ($LASTEXITCODE -ne 0) { throw 'WinUI build failed. See build\winui-build.log.' }
} finally {
    foreach ($name in $variables) { [Environment]::SetEnvironmentVariable($name,$previous[$name],'Process') }
    Pop-Location
}
