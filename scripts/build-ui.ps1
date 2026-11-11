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
$cache = Join-Path $root '.build-tools'
$downloads = Join-Path $cache 'downloads'
$packages = Join-Path $cache 'nuget'
$config = Join-Path $root 'src\ui\packages.config'
# SHA-256 of the signed archives for the versions in packages.config.
$packageHashes = @{
    'Microsoft.Web.WebView2.1.0.3719.77' = '2F6BE3A10A1A8D6D1FDE986AF4131DAB344F8181FFFE75590824E0F4B037ED73'
    'Microsoft.Windows.CppWinRT.3.0.260818.1' = 'A8993608AE9263A8288E1A1D9EE2684A5632F4ACC59E1E89DDD86B3B889A7367'
    'Microsoft.Windows.SDK.BuildTools.10.0.26100.4654' = '5F5783658A200F83EA2ADDFD406E2303A901B27AD7C49B0347C34AEFCC21883D'
    'Microsoft.Windows.SDK.BuildTools.MSIX.1.7.251221100' = 'E61C560805029F5AE15433BE52E153F3862B6FAAF725292638A9ECE592F7A060'
    'Microsoft.WindowsAppSDK.Base.2.0.4' = 'E3E13478C4C80C59ED5F8F89542FE49A2985DAA484753E93A5858E90C2D46A4D'
    'Microsoft.WindowsAppSDK.Foundation.2.3.9' = '230BC605A3FC9ED689B2117056C5274923BF58B453FA44EDDE18A168BBF628BE'
    'Microsoft.WindowsAppSDK.InteractiveExperiences.2.1.3' = 'E8063437EB853B5ABE4DC6B6BDB2ECFB82F49EC281764C82CD04B4D94E144F68'
    'Microsoft.WindowsAppSDK.Runtime.2.4.0' = '93F48B096416AB7B908EA30690BBA47DE8264CD2F22DD7B0AD97187A86D00120'
    'Microsoft.WindowsAppSDK.WinUI.2.3.6' = '3404F14B5AAD656024CF23EFAB49373E14BE4129B1E7D8CDD18DD26712D3C8E2'
}
$variables = @('TEMP','TMP','PATH','NoDefaultCurrentDirectoryInExePath')
$previous = @{}
foreach ($name in $variables) { $previous[$name] = [Environment]::GetEnvironmentVariable($name,'Process') }
Push-Location $root
try {
    $env:TEMP = Join-Path $root 'build\temp'; $env:TMP = $env:TEMP
    foreach ($directory in @($cache,$downloads,$packages)) {
        $item = Get-Item -LiteralPath $directory -Force -ErrorAction SilentlyContinue
        if (-not $item) {
            New-Item -ItemType Directory -Path $directory | Out-Null
        } elseif (-not $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Refusing to use a linked package directory: $directory"
        }
    }
    New-Item -ItemType Directory -Force -Path $env:TEMP | Out-Null
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [xml]$packageList = Get-Content -LiteralPath $config -Raw
    $sdkPackage = $null
    foreach ($package in $packageList.packages.package) {
        $name = "$($package.id).$($package.version)"
        $expectedHash = $packageHashes[$name]
        if (-not $expectedHash) { throw "Missing SHA-256 for WinUI package $name" }
        $archive = Join-Path $downloads "$name.nupkg"
        $destination = Join-Path $packages $name
        $existing = Get-Item -LiteralPath $destination -Force -ErrorAction SilentlyContinue
        if ($existing -and ($existing.FullName -ne $destination -or -not $existing.PSIsContainer -or
            ($existing.Attributes -band [IO.FileAttributes]::ReparsePoint))) {
            throw "Refusing to replace an unexpected package directory: $destination"
        }
        if (-not (Test-Path -LiteralPath $archive -PathType Leaf)) {
            $previousArchive = Join-Path $destination "$name.nupkg"
            if (Test-Path -LiteralPath $previousArchive -PathType Leaf) {
                Copy-Item -LiteralPath $previousArchive -Destination $archive
            } else {
                if ($Offline) { throw "Offline build: missing WinUI package archive $name" }
                $id = $package.id.ToLowerInvariant()
                $version = $package.version.ToLowerInvariant()
                $uri = "https://api.nuget.org/v3-flatcontainer/$id/$version/$id.$version.nupkg"
                Invoke-WebRequest -Uri $uri -OutFile $archive -UseBasicParsing
            }
        }
        if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expectedHash) {
            throw "SHA-256 mismatch: $archive. Remove the invalid archive and retry."
        }
        if ($existing) {
            $linkedEntry = Get-ChildItem -LiteralPath $destination -Recurse -Force |
                Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint } |
                Select-Object -First 1
            if ($linkedEntry) { throw "Refusing to replace a package containing linked files: $destination" }
            Remove-Item -LiteralPath $destination -Recurse -Force
        }
        [IO.Compression.ZipFile]::ExtractToDirectory($archive, $destination)
        if ($package.id -eq 'Microsoft.Windows.SDK.BuildTools') { $sdkPackage = $destination }
    }
    if (-not $sdkPackage) { throw 'The Windows SDK BuildTools package is required.' }
    $sdkTools = @(Get-ChildItem -LiteralPath (Join-Path $sdkPackage 'bin') -Directory |
        ForEach-Object { Join-Path $_.FullName 'x64' } |
        Where-Object { Test-Path -LiteralPath (Join-Path $_ 'mdmerge.exe') -PathType Leaf })
    if ($sdkTools.Count -ne 1) { throw 'Expected one x64 metadata tool directory in the SDK archive.' }
    $cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    if (-not (Test-Path -LiteralPath $cmake)) { $cmake = (Get-Command cmake -ErrorAction Stop).Source }
    & $cmake --preset windows-x64 "-DCONTEXT_ACTIONS_OFFLINE=$([bool]$Offline)"
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    & $cmake --build --preset release --target actions_core ContextActionsArchive
    if ($LASTEXITCODE -ne 0) { throw 'Core build failed.' }
    # C++/WinRT invokes mdmerge by name in cmd.exe, whose PATH is limited to 8191 characters.
    $env:PATH = "$($sdkTools[0]);$env:SystemRoot\System32;$env:SystemRoot;$env:SystemRoot\System32\WindowsPowerShell\v1.0"
    $env:NoDefaultCurrentDirectoryInExePath = '1'
    & (Join-Path $vs 'MSBuild\Current\Bin\MSBuild.exe') 'src\ui\ContextActions.vcxproj' /p:Configuration=Release /p:Platform=x64 /m:2 /v:quiet /nologo /nodeReuse:false '/flp:logfile=build/winui-build.log;verbosity=normal'
    if ($LASTEXITCODE -ne 0) { throw 'WinUI build failed. See build\winui-build.log.' }
    Copy-Item -LiteralPath 'build\Release\ContextActionsArchive.exe' -Destination 'build\winui'
} finally {
    foreach ($name in $variables) { [Environment]::SetEnvironmentVariable($name,$previous[$name],'Process') }
    Pop-Location
}
