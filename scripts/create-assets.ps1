$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$destination = Join-Path $PSScriptRoot '..\resources'
# A document with an action arrow, drawn once for all icon sizes; no image runtime dependency.
function New-Logo([int]$size) {
    $bitmap = [Drawing.Bitmap]::new($size, $size)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.ScaleTransform($size / 64.0, $size / 64.0)
    $blue = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(255, 30, 107, 230))
    $white = [Drawing.Pen]::new([Drawing.Color]::White, 4)
    $graphics.FillRectangle($blue, 4, 4, 56, 56)
    $graphics.DrawLines($white, [Drawing.PointF[]]@([Drawing.PointF]::new(19,16),[Drawing.PointF]::new(36,16),[Drawing.PointF]::new(45,25),[Drawing.PointF]::new(45,48),[Drawing.PointF]::new(19,48),[Drawing.PointF]::new(19,16)))
    $graphics.DrawLine($white,24,33,39,33)
    $graphics.DrawLines($white,[Drawing.PointF[]]@([Drawing.PointF]::new(33,27),[Drawing.PointF]::new(39,33),[Drawing.PointF]::new(33,39)))
    $white.Dispose(); $blue.Dispose(); $graphics.Dispose()
    return $bitmap
}
$sizes = @(16,24,32,48,64,256)
$images = foreach ($size in $sizes) {
    $bitmap = New-Logo $size
    $stream = [IO.MemoryStream]::new()
    $bitmap.Save($stream, [Drawing.Imaging.ImageFormat]::Png)
    ,$stream.ToArray()
    $stream.Dispose(); $bitmap.Dispose()
}
$file = [IO.File]::Create((Join-Path $PSScriptRoot '..\src\windows\resources\app.ico'))
$writer = [IO.BinaryWriter]::new($file)
$writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $dimension = if ($sizes[$i] -eq 256) { 0 } else { $sizes[$i] }
    $writer.Write([byte]$dimension); $writer.Write([byte]$dimension); $writer.Write([uint16]0)
    $writer.Write([uint16]1); $writer.Write([uint16]32); $writer.Write([uint32]$images[$i].Length); $writer.Write([uint32]$offset)
    $offset += $images[$i].Length
}
foreach ($bytes in $images) { $writer.Write([byte[]]$bytes) }
$writer.Dispose(); $file.Dispose()
foreach ($size in @(44,150)) {
    $bitmap = New-Logo $size
    $bitmap.Save((Join-Path $destination "logo-$size.png"),[Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
}
