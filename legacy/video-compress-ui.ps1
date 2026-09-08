[CmdletBinding()]
param(
    [string]$OutputFile
)

$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$defaultSource = [Environment]::GetFolderPath('MyVideos')

function Select-SourcePath {
    $dialog = New-Object System.Windows.Forms.OpenFileDialog
    $dialog.Title = 'Select a video file to compress'
    $dialog.Filter = 'Video files|*.mp4;*.mkv;*.mov;*.avi;*.webm|All files|*.*'
    $dialog.Multiselect = $false
    if (Test-Path -LiteralPath $defaultSource) {
        $dialog.InitialDirectory = $defaultSource
    }

    if ($dialog.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK -and $dialog.FileName) {
        return $dialog.FileName
    }

    return $null
}

function Select-OutputPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SourcePath
    )

    if (Test-Path -LiteralPath $SourcePath -PathType Container) {
        $baseFolder = $SourcePath
    } else {
        $baseFolder = Split-Path -LiteralPath $SourcePath -Parent
    }

    $defaultOutput = Join-Path -Path $baseFolder -ChildPath 'converted-hevc'

    $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
    $dialog.Description = 'Select the output folder (Cancel uses the default folder)'
    if (Test-Path -LiteralPath $baseFolder) {
        $dialog.SelectedPath = $baseFolder
    }

    if ($dialog.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK -and $dialog.SelectedPath) {
        return $dialog.SelectedPath
    }

    return $defaultOutput
}

function Select-QualityProfile {
    $form = New-Object System.Windows.Forms.Form
    $form.Text = 'Compression Quality'
    $form.StartPosition = 'CenterScreen'
    $form.ClientSize = [System.Drawing.Size]::new(420, 180)
    $form.FormBorderStyle = 'FixedDialog'
    $form.MaximizeBox = $false
    $form.MinimizeBox = $false

    $label = New-Object System.Windows.Forms.Label
    $label.Location = [System.Drawing.Point]::new(16, 16)
    $label.Size = [System.Drawing.Size]::new(380, 20)
    $label.Text = 'Choose a quality profile:'
    $form.Controls.Add($label)

    $combo = New-Object System.Windows.Forms.ComboBox
    $combo.Location = [System.Drawing.Point]::new(16, 44)
    $combo.Size = [System.Drawing.Size]::new(180, 24)
    $combo.DropDownStyle = 'DropDownList'
    [void]$combo.Items.AddRange(@('high', 'balanced', 'compact'))
    $combo.SelectedItem = 'balanced'
    $form.Controls.Add($combo)

    $description = New-Object System.Windows.Forms.Label
    $description.Location = [System.Drawing.Point]::new(16, 80)
    $description.Size = [System.Drawing.Size]::new(388, 42)
    $description.Text = 'balanced: safer default with smaller size and low visible loss.'
    $form.Controls.Add($description)

    $updateDescription = {
        switch ($combo.SelectedItem) {
            'high' { $description.Text = 'high: best visual quality, larger output files.' }
            'balanced' { $description.Text = 'balanced: safer default with smaller size and low visible loss.' }
            'compact' { $description.Text = 'compact: stronger compression and smaller files.' }
        }
    }

    $combo.add_SelectedIndexChanged($updateDescription)

    $okButton = New-Object System.Windows.Forms.Button
    $okButton.Location = [System.Drawing.Point]::new(228, 132)
    $okButton.Size = [System.Drawing.Size]::new(80, 28)
    $okButton.Text = 'OK'
    $okButton.DialogResult = [System.Windows.Forms.DialogResult]::OK
    $form.Controls.Add($okButton)

    $cancelButton = New-Object System.Windows.Forms.Button
    $cancelButton.Location = [System.Drawing.Point]::new(324, 132)
    $cancelButton.Size = [System.Drawing.Size]::new(80, 28)
    $cancelButton.Text = 'Cancel'
    $cancelButton.DialogResult = [System.Windows.Forms.DialogResult]::Cancel
    $form.Controls.Add($cancelButton)

    $form.AcceptButton = $okButton
    $form.CancelButton = $cancelButton

    if ($form.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK -and $combo.SelectedItem) {
        return [string]$combo.SelectedItem
    }

    return $null
}

try {
    $source = Select-SourcePath
    if (-not $source) {
        exit 2
    }

    $output = Select-OutputPath -SourcePath $source
    $quality = Select-QualityProfile
    if (-not $quality) {
        exit 2
    }

    $lines = @(
        "source=$source"
        "output=$output"
        "quality=$quality"
    )

    if ($OutputFile) {
        $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
        [System.IO.File]::WriteAllLines($OutputFile, $lines, $utf8NoBom)
    } else {
        $lines
    }
} catch {
    [System.Windows.Forms.MessageBox]::Show(
        $_.Exception.Message,
        'Compression UI Error',
        [System.Windows.Forms.MessageBoxButtons]::OK,
        [System.Windows.Forms.MessageBoxIcon]::Error
    ) | Out-Null
    exit 1
}
