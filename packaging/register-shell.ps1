[CmdletBinding()]
param([ValidateSet('Install','Uninstall')][string]$Mode = 'Install')
$ErrorActionPreference = 'Stop'
$appDirectory = Split-Path -Parent $PSScriptRoot
$clsid = '{A836FB4F-6548-4FC3-8D67-50267B25A418}'
$comKey = "Software\Classes\CLSID\$clsid"
$menuKey = 'Software\Classes\*\shell\ContextActions'
# This is invoked only by installation or by an explicit manual command, never by build/tests.
try {
    if ($Mode -eq 'Install') {
        foreach ($file in @('ContextActions.exe','ContextActionsShell.dll','packaging\ContextActions.msix')) {
            if (-not (Test-Path -LiteralPath (Join-Path $appDirectory $file) -PathType Leaf)) { throw "Missing package file: $file" }
        }
        Add-AppxPackage -Path (Join-Path $PSScriptRoot 'ContextActions.msix') -ExternalLocation $appDirectory -AllowUnsigned
        $server = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey("$comKey\InprocServer32")
        try { $server.SetValue('', (Join-Path $appDirectory 'ContextActionsShell.dll')); $server.SetValue('ThreadingModel','Apartment') } finally { $server.Dispose() }
        $menu = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($menuKey)
        try {
            $menu.SetValue('MUIVerb','Context Actions'); $menu.SetValue('ExplorerCommandHandler',$clsid)
            $menu.SetValue('Icon',((Join-Path $appDirectory 'ContextActionsShell.dll') + ',-101'))
            $menu.SetValue('MultiSelectModel','Player'); $menu.SetValue('SubCommands','')
        } finally { $menu.Dispose() }
    } else {
        [Microsoft.Win32.Registry]::CurrentUser.DeleteSubKeyTree($menuKey, $false)
        [Microsoft.Win32.Registry]::CurrentUser.DeleteSubKeyTree($comKey, $false)
        Get-AppxPackage -Name 'ContextActions.Preview' | Remove-AppxPackage
    }
} catch {
    Write-Error "Context Actions shell $Mode failed: $($_.Exception.Message). The preview identity is unsigned; Windows policy or elevation may be required. No Explorer restart was attempted." -ErrorAction Continue
    exit 1
}
