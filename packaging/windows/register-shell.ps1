[CmdletBinding()]
param([ValidateSet('Install','Uninstall')][string]$Mode = 'Install', [switch]$Development)
$ErrorActionPreference = 'Stop'
$appDirectory = Split-Path -Parent $PSScriptRoot
$clsid = '{A836FB4F-6548-4FC3-8D67-50267B25A418}'
$comKey = "Software\Classes\CLSID\$clsid"
$menuKey = 'Software\Classes\*\shell\ContextActions'
$classicClsid = '{EE9D6C0B-0A19-4F8C-8E61-C78DD8816DB0}'
$classicComKey = "Software\Classes\CLSID\$classicClsid"
$classicMenuKey = 'Software\Classes\*\shell\ContextActions.Legacy'
$shellDll = Join-Path $appDirectory 'ContextActionsShell.dll'
$windows11 = $false
$registry = $null

function Register-InprocServer([string]$Key, [string]$Path) {
    $server = $registry.CreateSubKey("$Key\InprocServer32")
    try {
        $server.SetValue('', $Path)
        $server.SetValue('ThreadingModel', 'Apartment')
    } finally { $server.Dispose() }
}

function Remove-Windows10Menu {
    $registry.DeleteSubKeyTree($classicMenuKey, $false)
    $registry.DeleteSubKeyTree($classicComKey, $false)
}

function Register-Windows10Menu {
    $previousServer = $registry.OpenSubKey("$classicComKey\InprocServer32")
    $previousPath = if ($previousServer) {
        try { $previousServer.GetValue('') } finally { $previousServer.Dispose() }
    }
    try {
        Register-InprocServer $classicComKey $shellDll
        $menu = $registry.CreateSubKey($classicMenuKey)
        try {
            $menu.SetValue('', 'Context Actions')
            $menu.SetValue('Icon', "$shellDll,-101")
            $menu.SetValue('MultiSelectModel', 'Player')
            $menu.SetValue('NeverDefault', '')
            $command = $menu.CreateSubKey('command')
            try { $command.SetValue('DelegateExecute', $classicClsid) } finally { $command.Dispose() }
        } finally { $menu.Dispose() }
    } catch {
        if ($previousPath) { Register-InprocServer $classicComKey $previousPath }
        else { Remove-Windows10Menu }
        throw
    }
}

# This is invoked only by installation or by an explicit manual command, never by build/tests.
try {
    $windowsBuild = [int](Get-ItemPropertyValue -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion' -Name CurrentBuildNumber)
    $windows11 = $windowsBuild -ge 22000
    $registry = [Microsoft.Win32.RegistryKey]::OpenBaseKey('CurrentUser', 'Registry64')
    if ($Mode -eq 'Install') {
        if ($windowsBuild -lt 19045) { throw 'Context Actions requires Windows 10 22H2 or newer.' }
        foreach ($file in @('ContextActions.exe','ContextActionsShell.dll')) {
            if (-not (Test-Path -LiteralPath (Join-Path $appDirectory $file) -PathType Leaf)) { throw "Missing package file: $file" }
        }
        if (-not $windows11) {
            Register-Windows10Menu
            return
        }

        if (-not (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'ContextActions.msix') -PathType Leaf)) { throw 'Missing package file: packaging\ContextActions.msix' }
        if ($Development) {
            $developerMode = Get-ItemPropertyValue -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock' -Name AllowDevelopmentWithoutDevLicense -ErrorAction SilentlyContinue
            if ($developerMode -ne 1) { throw 'Development registration requires Developer Mode to be enabled beforehand. This script does not change that setting.' }
            Add-AppxPackage -Register (Join-Path $PSScriptRoot 'development\AppxManifest.xml') -ExternalLocation $appDirectory
        } else {
            Add-AppxPackage -Path (Join-Path $PSScriptRoot 'ContextActions.msix') -ExternalLocation $appDirectory
        }
        Register-InprocServer $comKey $shellDll
        $menu = $registry.CreateSubKey($menuKey)
        try {
            $menu.SetValue('MUIVerb','Context Actions'); $menu.SetValue('ExplorerCommandHandler',$clsid)
            $menu.SetValue('Icon', "$shellDll,-101")
            $menu.SetValue('MultiSelectModel','Player'); $menu.SetValue('SubCommands','')
        } finally { $menu.Dispose() }
        Remove-Windows10Menu
    } else {
        Remove-Windows10Menu
        $registry.DeleteSubKeyTree($menuKey, $false)
        $registry.DeleteSubKeyTree($comKey, $false)
        if ($windows11) { Get-AppxPackage -Name 'ContextActions.Preview' | Remove-AppxPackage }
    }
} catch {
    $message = "Context Actions shell $Mode failed: $($_.Exception.Message)"
    if ($windows11 -and $Mode -eq 'Install') {
        $message += ' Normal installation requires a trusted signed identity package. For local preview testing with Developer Mode already enabled, use -Development.'
    }
    Write-Error $message -ErrorAction Continue
    exit 1
} finally { if ($registry) { $registry.Dispose() } }
