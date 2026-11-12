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
    $chooserArguments = @('--choose-action','--',('"' + $source + '"'))
    $process = Start-Process -FilePath $AppPath -ArgumentList $chooserArguments -WorkingDirectory (Split-Path -Parent $AppPath) -WindowStyle Hidden -PassThru
    $processId = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::ProcessIdProperty, $process.Id)
    $window = Wait-For { [System.Windows.Automation.AutomationElement]::RootElement.FindFirst([System.Windows.Automation.TreeScope]::Children, $processId) } 'Action chooser did not appear.'
    Invoke-Control 'CancelChooser'
    if (-not $process.WaitForExit(10000)) { throw 'Cancel did not close the action chooser.' }
    if (-not (Test-Path -LiteralPath $source) -or (Test-Path -LiteralPath $result)) { throw 'Cancel changed the selected file.' }

    $process = Start-Process -FilePath $AppPath -ArgumentList $chooserArguments -WorkingDirectory (Split-Path -Parent $AppPath) -WindowStyle Hidden -PassThru
    $processId = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::ProcessIdProperty, $process.Id)
    $window = Wait-For { [System.Windows.Automation.AutomationElement]::RootElement.FindFirst([System.Windows.Automation.TreeScope]::Children, $processId) } 'Action chooser did not reappear.'
    $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::AutomationIdProperty, 'ChooseAction-compress')
    if ($window.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condition)) { throw 'Chooser offered video compression for a text file.' }
    Invoke-Control 'ChooseAction-rename'
    $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::AutomationIdProperty, 'Run')
    $window = Wait-For {
        $windows = [System.Windows.Automation.AutomationElement]::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, $processId)
        foreach ($candidate in $windows) {
            try {
                if ($candidate.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condition)) { return $candidate }
            } catch [System.Windows.Automation.ElementNotAvailableException] {
                # The chooser can close while the next window is being discovered.
            }
        }
    } 'Choosing Rename did not open its settings.'
    if (-not (Test-Path -LiteralPath $source)) { throw 'The configure-first launch changed the file before Run.' }
    Invoke-Control 'PreviewNames'
    $filesCondition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::AutomationIdProperty, 'Files')
    $files = $window.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $filesCondition)
    $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::ControlTypeProperty, [System.Windows.Automation.ControlType]::ListItem)
    $row = $files.FindFirst([System.Windows.Automation.TreeScope]::Children, $condition)
    ([System.Windows.Automation.SelectionItemPattern]$row.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern)).Select()
    $selectedFileIsVisible = {
        try {
            $visibleFiles = $window.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $filesCondition)
            if ($null -eq $visibleFiles -or $visibleFiles.Current.IsOffscreen) { return $false }
            $selection = ([System.Windows.Automation.SelectionPattern]$visibleFiles.GetCurrentPattern([System.Windows.Automation.SelectionPattern]::Pattern)).Current.GetSelection()
            return $selection.Count -eq 1 -and $selection[0].Current.Name -eq $source
        } catch [System.Windows.Automation.ElementNotAvailableException] {
            return $false
        }
    }
    Wait-For $selectedFileIsVisible 'The file did not become selected before opening Settings.' | Out-Null
    Invoke-Control 'Settings'
    Select-Language ('Fran' + [char]231 + 'ais')
    Select-Language 'English'
    Invoke-Control 'BackToFiles'
    Wait-For $selectedFileIsVisible 'Language changes lost the selected file.' | Out-Null
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
    $report = "PASS: action chooser filtering, cancel, configure-first launch, rename preview, Settings round trip, compact-to-full switch, real rename, narrow result footer and undo."

    $process.CloseMainWindow() | Out-Null
    if (-not $process.WaitForExit(10000)) { throw 'Rename test window did not close normally.' }
    $fixture = Join-Path $scratch 'archive-fixture'
    $zip = Join-Path $scratch 'repack.zip'
    $tarGz = Join-Path $scratch 'repack.tar.gz'
    $sevenZip = Join-Path $scratch 'repack.7z'
    New-Item -ItemType Directory -Path $fixture | Out-Null
    Set-Content -LiteralPath (Join-Path $fixture 'item.txt') -Value 'Archive UI smoke' -Encoding ascii
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::CreateFromDirectory($fixture, $zip)
    $zipHash = (Get-FileHash -LiteralPath $zip).Hash
    $process = Start-Process -FilePath $AppPath -ArgumentList @('--action','repack','--',('"' + $zip + '"')) -WorkingDirectory (Split-Path -Parent $AppPath) -WindowStyle Hidden -PassThru
    $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::ProcessIdProperty, $process.Id)
    $window = Wait-For { [System.Windows.Automation.AutomationElement]::RootElement.FindFirst([System.Windows.Automation.TreeScope]::Children, $condition) } 'Repack test window did not appear.'
    $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::AutomationIdProperty, 'ArchiveFormat')
    $formats = Wait-For { $window.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condition) } 'Archive format selector did not appear.'
    $selection = [System.Windows.Automation.SelectionPattern]$formats.GetCurrentPattern([System.Windows.Automation.SelectionPattern]::Pattern)
    $selectedFormat = $selection.Current.GetSelection()
    if ($selectedFormat.Count -ne 1 -or $selectedFormat[0].Current.Name -ne 'tar.gz') { throw 'ZIP did not default to TAR.GZ.' }
    $expand = [System.Windows.Automation.ExpandCollapsePattern]$formats.GetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern)
    $expand.Expand()
    $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::NameProperty, '7z')
    Wait-For { $window.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condition) } '7Z was not offered as a target for ZIP.' | Out-Null
    $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::NameProperty, 'zip')
    if ($window.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condition)) { throw 'ZIP was offered as a target for a ZIP source.' }
    $expand.Collapse()
    if (Test-Path -LiteralPath $tarGz) { throw 'Configure-first launch started repacking before Run.' }
    Invoke-Control 'Run'
    Wait-For { Test-Path -LiteralPath $tarGz } 'Repack did not publish the TAR.GZ file.' | Out-Null
    $item = & "$env:SystemRoot\System32\tar.exe" -xOf $tarGz 'item.txt'
    if ($LASTEXITCODE -ne 0 -or $item -ne 'Archive UI smoke') { throw 'Repack changed or omitted the archived file.' }
    if ((Get-FileHash -LiteralPath $zip).Hash -ne $zipHash) { throw 'Repack changed the source archive.' }
    Wait-For { $formats.Current.IsEnabled } 'Archive selector did not become enabled after the first repack.' | Out-Null
    $expand.Expand()
    $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::NameProperty, '7z')
    $option = Wait-For { $window.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condition) } '7Z option did not reappear.'
    ([System.Windows.Automation.SelectionItemPattern]$option.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern)).Select()
    $expand.Collapse()
    Invoke-Control 'Run'
    Wait-For { Test-Path -LiteralPath $sevenZip } 'Repack did not use the selected 7Z target.' | Out-Null
    # The system tar on Windows 10 cannot reliably extract 7z archives.
    $roundTripDirectory = Join-Path $scratch 'verify-7z'
    New-Item -ItemType Directory -Path $roundTripDirectory | Out-Null
    $roundTripReport = Join-Path $roundTripDirectory 'report.txt'
    & $AppPath --headless --report $roundTripReport --action repack --format zip --output $roundTripDirectory -- $sevenZip | Out-Null
    $roundTripZip = Join-Path $roundTripDirectory 'repack.zip'
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $roundTripZip -PathType Leaf)) { throw "The app could not read back the 7Z archive. Report: $roundTripReport" }
    [IO.Compression.ZipFile]::ExtractToDirectory($roundTripZip, $roundTripDirectory)
    $recoveredItem = Join-Path $roundTripDirectory 'item.txt'
    if (-not (Test-Path -LiteralPath $recoveredItem -PathType Leaf) -or
        (Get-FileHash -LiteralPath $recoveredItem).Hash -ne (Get-FileHash -LiteralPath (Join-Path $fixture 'item.txt')).Hash) {
        throw 'Repack changed or omitted the 7Z archived file.'
    }

    $process.CloseMainWindow() | Out-Null
    if (-not $process.WaitForExit(10000)) { throw 'Repack test window did not close normally.' }
    $process = Start-Process -FilePath $AppPath -ArgumentList @('--action','repack','--',('"' + $zip + '"'),('"' + $tarGz + '"'),('"' + $sevenZip + '"')) -WorkingDirectory (Split-Path -Parent $AppPath) -WindowStyle Hidden -PassThru
    $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::ProcessIdProperty, $process.Id)
    $window = Wait-For { [System.Windows.Automation.AutomationElement]::RootElement.FindFirst([System.Windows.Automation.TreeScope]::Children, $condition) } 'Archive batch window did not appear.'
    $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::AutomationIdProperty, 'ArchiveFormat')
    $formats = Wait-For { $window.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condition) } 'Archive batch format selector did not appear.'
    $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::AutomationIdProperty, 'Run')
    $run = Wait-For { $window.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condition) } 'Archive batch Run button did not appear.'
    if ($formats.Current.IsEnabled -or $run.Current.IsEnabled) { throw 'Archive batch permitted Run without a common target.' }
    $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::AutomationIdProperty, 'Files')
    $files = $window.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condition)
    $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::ControlTypeProperty, [System.Windows.Automation.ControlType]::ListItem)
    $rows = $files.FindAll([System.Windows.Automation.TreeScope]::Children, $condition)
    if ($rows.Count -ne 3) { throw 'Archive batch did not show all input archives.' }
    ([System.Windows.Automation.SelectionItemPattern]$rows[2].GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern)).Select()
    Invoke-Control 'RemoveFiles'
    Wait-For { $formats.Current.IsEnabled -and $run.Current.IsEnabled } 'Removing 7Z did not restore the common target.' | Out-Null
    $selection = [System.Windows.Automation.SelectionPattern]$formats.GetCurrentPattern([System.Windows.Automation.SelectionPattern]::Pattern)
    $selectedFormat = $selection.Current.GetSelection()
    if ($selectedFormat.Count -ne 1 -or $selectedFormat[0].Current.Name -ne '7z') { throw 'ZIP and TAR.GZ did not select 7Z as the common target.' }
    $report += ' Archive launch, target selection, ZIP repack and batch target refresh passed.'

    if (Test-Path -LiteralPath (Join-Path (Split-Path -Parent $AppPath) 'engines\ffmpeg.exe')) {
        $process.CloseMainWindow() | Out-Null
        if (-not $process.WaitForExit(10000)) { throw 'Repack test window did not close normally.' }
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
