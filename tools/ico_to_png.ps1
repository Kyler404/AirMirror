# Dump an .ico to a magnified PNG so the artwork can be inspected.
# Usage: powershell -File tools/ico_to_png.ps1 -In app.ico -Out big.png [-Size 16] [-Scale 16]
param(
    [Parameter(Mandatory = $true)][string]$In,
    [Parameter(Mandatory = $true)][string]$Out,
    [int]$Size = 16,
    [int]$Scale = 16
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$ico = New-Object System.Drawing.Icon((Resolve-Path $In).Path, $Size, $Size)
$src = $ico.ToBitmap()
Write-Host ("source bitmap: {0}x{1}" -f $src.Width, $src.Height)

$w = $src.Width * $Scale
$h = $src.Height * $Scale
$big = New-Object System.Drawing.Bitmap $w, $h
$g = [System.Drawing.Graphics]::FromImage($big)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
$g.DrawImage($src, 0, 0, $w, $h)
$big.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $big.Dispose(); $src.Dispose(); $ico.Dispose()
Write-Host ("saved {0} ({1}x{2})" -f $Out, $w, $h)
