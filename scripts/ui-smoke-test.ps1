[CmdletBinding()]
param([string]$AppPath)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $AppPath) { $AppPath = Join-Path $root 'build\winui\ContextActions.exe' }
$AppPath = [IO.Path]::GetFullPath($AppPath)
if (-not (Test-Path -LiteralPath $AppPath -PathType Leaf)) { throw "Build the WinUI app first: $AppPath" }
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$scratch = Join-Path $root ('build\ui-smoke-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch | Out-Null
$source = Join-Path $scratch 'preview.txt'
$result = Join-Path $scratch 'preview_1.txt'
Set-Content -LiteralPath $source -Value 'UI rename regression' -Encoding utf8
$previousData = $env:CONTEXT_ACTIONS_DATA_DIR
$env:CONTEXT_ACTIONS_DATA_DIR = Join-Path $scratch 'state'
New-Item -ItemType Directory -Path $env:CONTEXT_ACTIONS_DATA_DIR | Out-Null
Set-Content -LiteralPath (Join-Path $env:CONTEXT_ACTIONS_DATA_DIR 'settings.ini') -Value "[ui]`nlanguage=en" -Encoding ascii
$process = $null
$window = $null
function Wait-For([scriptblock]$Probe, [string]$Failure) {
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    do {
        $found = & $Probe
        if ($found) { return $found }
        if ($process.HasExited) { throw "Application exited ($($process.ExitCode)): $Failure" }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw $Failure
}
function Invoke-Control([string]$Id) {
    $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::AutomationIdProperty, $Id)
    $control = Wait-For {
        $candidate = $window.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condition)
        if ($null -ne $candidate -and $candidate.Current.IsEnabled) { $candidate }
    } "Control did not become available: $Id"
    $pattern = $control.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern)
    ([System.Windows.Automation.InvokePattern]$pattern).Invoke()
}
function Select-Language([string]$Name) {
    $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::AutomationIdProperty, 'Language')
    $language = Wait-For { $window.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condition) } 'Language selector did not appear.'
    $expand = [System.Windows.Automation.ExpandCollapsePattern]$language.GetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern)
    $expand.Expand()
    $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::NameProperty, $Name)
    $option = Wait-For { $window.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condition) } "Language option did not appear: $Name"
    ([System.Windows.Automation.SelectionItemPattern]$option.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern)).Select()
    $expand.Collapse()
}
try {
    # This test intentionally opens its own native window. It never interacts with another process.
    $process = Start-Process -FilePath $AppPath -ArgumentList @('--action','rename','--',('"' + $source + '"')) -WorkingDirectory (Split-Path -Parent $AppPath) -WindowStyle Hidden -PassThru
    $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::ProcessIdProperty, $process.Id)
    $window = Wait-For { [System.Windows.Automation.AutomationElement]::RootElement.FindFirst([System.Windows.Automation.TreeScope]::Children, $condition) } 'WinUI window did not appear.'
    if (-not (Test-Path -LiteralPath $source)) { throw 'The configure-first launch changed the file before Run.' }
    Invoke-Control 'PreviewNames'
    $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::AutomationIdProperty, 'Files')
    $files = $window.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condition)
    $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::ControlTypeProperty, [System.Windows.Automation.ControlType]::ListItem)
    $row = $files.FindFirst([System.Windows.Automation.TreeScope]::Children, $condition)
    ([System.Windows.Automation.SelectionItemPattern]$row.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern)).Select()
    Invoke-Control 'Settings'
    Select-Language ('Fran' + [char]231 + 'ais')
    Select-Language 'English'
    Invoke-Control 'BackToFiles'
    $selection = ([System.Windows.Automation.SelectionPattern]$files.GetCurrentPattern([System.Windows.Automation.SelectionPattern]::Pattern)).Current.GetSelection()
    if ($selection.Count -ne 1 -or $selection[0].Current.Name -ne $source) { throw 'Language changes lost the selected file.' }
    Invoke-Control 'ExpandWorkspace'
    Invoke-Control 'Run'
    Wait-For { Test-Path -LiteralPath $result } 'Rename did not run: preview may have been lost when switching views.' | Out-Null
    if (Test-Path -LiteralPath $source) { throw 'Rename left the source path in place.' }
    if ((Get-Content -LiteralPath $result -Raw).Trim() -ne 'UI rename regression') { throw 'Rename changed file contents.' }
    $transform = [System.Windows.Automation.TransformPattern]$window.GetCurrentPattern([System.Windows.Automation.TransformPattern]::Pattern)
    $transform.Resize(380,540)
    Wait-For {
        $bounds = $window.Current.BoundingRectangle
        $controls = @{}
        foreach ($id in @('OpenButton','RevealButton','Run')) {
            $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::AutomationIdProperty, $id)
            $control = $window.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condition)
            if ($null -eq $control -or $control.Current.IsOffscreen) { return $false }
            $rect = $control.Current.BoundingRectangle
            if ($rect.IsEmpty -or -not $bounds.Contains($rect)) { return $false }
            $controls[$id] = $rect
        }
        return -not ($controls['Run'].IntersectsWith($controls['OpenButton']) -or $controls['Run'].IntersectsWith($controls['RevealButton']))
    } 'Narrow footer clips result links or overlaps them with Run.' | Out-Null
    $transform.Resize(1040,760)
    Invoke-Control 'UndoRename'
    Wait-For { Test-Path -LiteralPath $source } 'Undo did not restore the source.' | Out-Null
    $report = "PASS: configure-first launch, rename preview, Settings round trip, compact-to-full switch, real rename, narrow result footer and undo."
    if (Test-Path -LiteralPath (Join-Path (Split-Path -Parent $AppPath) 'engines\ffmpeg.exe')) {
        $process.CloseMainWindow() | Out-Null
        if (-not $process.WaitForExit(10000)) { throw 'Rename test window did not close normally.' }
        $broken = Join-Path $scratch 'broken.png'
        Set-Content -LiteralPath $broken -Value 'Invalid image for multiline diagnostic regression' -Encoding ascii
        $process = Start-Process -FilePath $AppPath -ArgumentList @('--action','image','--',('"' + $broken + '"')) -WorkingDirectory (Split-Path -Parent $AppPath) -WindowStyle Hidden -PassThru
        $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::ProcessIdProperty, $process.Id)
        $window = Wait-For { [System.Windows.Automation.AutomationElement]::RootElement.FindFirst([System.Windows.Automation.TreeScope]::Children, $condition) } 'Diagnostic test window did not appear.'
        $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::AutomationIdProperty, 'ImageWidthBox')
        $width = Wait-For { $window.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condition) } 'Image width did not appear.'
        $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::AutomationIdProperty, 'InputBox')
        $input = $width.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condition)
        $inputValue = [System.Windows.Automation.ValuePattern]$input.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern)
        $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::AutomationIdProperty, 'Run')
        $run = $window.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condition)
        foreach ($invalidWidth in @('1','',(2.5).ToString([Globalization.CultureInfo]::CurrentCulture))) {
            $inputValue.SetValue($invalidWidth)
            $run.SetFocus()
            Invoke-Control 'Run'
            Wait-For { $width.Current.HelpText -match 'whole number' } 'Invalid width did not show an accessible field error.' | Out-Null
            if ($invalidWidth -eq '1') {
                Invoke-Control 'Settings'
                Select-Language ('Fran' + [char]231 + 'ais')
                Invoke-Control 'BackToFiles'
                if ($width.Current.HelpText -notmatch 'nombre entier') { throw 'The existing width error did not translate into French.' }
                Invoke-Control 'Settings'
                Select-Language 'English'
                Invoke-Control 'BackToFiles'
            }
        }
        $inputValue.SetValue('32')
        $run.SetFocus()
        Wait-For { -not $width.Current.HelpText } 'Correcting width did not clear its field error.' | Out-Null
        Invoke-Control 'Run'
        $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::AutomationIdProperty, 'ErrorDetails')
        Wait-For { $candidate = $window.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condition); $null -ne $candidate -and -not $candidate.Current.IsOffscreen } 'The corrupt image did not report a failure.' | Out-Null
        Invoke-Control 'Settings'
        Select-Language ('Fran' + [char]231 + 'ais')
        Invoke-Control 'BackToFiles'
        $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::AutomationIdProperty, 'Status')
        $status = $window.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condition)
        if ($status.Current.Name -notmatch '^Termin.*0.*1.*chec$') { throw 'Completed failure summary did not relocalize or lost its counts.' }
        Invoke-Control 'ErrorDetails'
        $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::AutomationIdProperty, 'ErrorText')
        $errorText = Wait-For { $window.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condition) } 'Diagnostic text did not appear.'
        $value = [System.Windows.Automation.ValuePattern]$errorText.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern)
        if (-not $value.Current.IsReadOnly -or $value.Current.Value -notmatch '[\r\n]' -or $value.Current.Value -notmatch 'Nothing was written') { throw 'Diagnostic viewer is editable or truncates the multiline engine error.' }
        $report += ' Selection continuity, invalid-width field feedback/correction, completed-status localization and multiline read-only engine error details passed.'
    } else { Write-Warning 'Engine error-detail check skipped: pass -AppPath pointing to the packaged app.' }
    Set-Content -LiteralPath (Join-Path $scratch 'result.txt') -Value $report -Encoding utf8
    $report
} finally {
    $env:CONTEXT_ACTIONS_DATA_DIR = $previousData
    if ($null -ne $process -and -not $process.HasExited) {
        # Close only this test's process through its normal window handler; never force termination.
        $process.CloseMainWindow() | Out-Null
        $process.WaitForExit(10000) | Out-Null
        if (-not $process.HasExited) { Write-Warning "The test window is still open (PID $($process.Id)); close it manually." }
    }
}
