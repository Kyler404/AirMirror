# Crop a region out of an image and magnify it (nearest neighbour).
# Usage: powershell -File tools/crop.ps1 -In a.png -Out b.png -X 0 -Y 0 -W 300 -H 80 -Scale 4
param(
    [Parameter(Mandatory = $true)][string]$In,
    [Parameter(Mandatory = $true)][string]$Out,
    [int]$X = 0, [int]$Y = 0, [int]$W = 200, [int]$H = 100, [int]$Scale = 4
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$img = [System.Drawing.Image]::FromFile((Resolve-Path $In).Path)
if ($X + $W -gt $img.Width)  { $W = $img.Width - $X }
if ($Y + $H -gt $img.Height) { $H = $img.Height - $Y }

$crop = New-Object System.Drawing.Bitmap $W, $H
$g = [System.Drawing.Graphics]::FromImage($crop)
$g.DrawImage($img,
    (New-Object System.Drawing.Rectangle 0, 0, $W, $H),
    (New-Object System.Drawing.Rectangle $X, $Y, $W, $H),
    [System.Drawing.GraphicsUnit]::Pixel)

$big = New-Object System.Drawing.Bitmap ($W * $Scale), ($H * $Scale)
$g2 = [System.Drawing.Graphics]::FromImage($big)
$g2.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$g2.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
$g2.DrawImage($crop, 0, 0, $W * $Scale, $H * $Scale)
$big.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)

$g.Dispose(); $g2.Dispose(); $crop.Dispose(); $big.Dispose(); $img.Dispose()
Write-Host ("saved {0} ({1}x{2} from {3},{4} {5}x{6})" -f $Out, ($W*$Scale), ($H*$Scale), $X, $Y, $W, $H)
