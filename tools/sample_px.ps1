# Sample pixel colours from a reference image.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools/sample_px.ps1 -Path img.jpg -Points "10,10;500,300"
param(
    [Parameter(Mandatory = $true)][string]$Path,
    [Parameter(Mandatory = $true)][string]$Points
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$img = [System.Drawing.Image]::FromFile($Path)
Write-Host ("image: {0}x{1}" -f $img.Width, $img.Height)

foreach ($p in $Points.Split(';')) {
    $xy = $p.Split(',')
    $x = [int]$xy[0]
    $y = [int]$xy[1]
    if ($x -lt 0 -or $y -lt 0 -or $x -ge $img.Width -or $y -ge $img.Height) {
        Write-Host ("{0},{1} -> out of bounds" -f $x, $y)
        continue
    }
    $c = $img.GetPixel($x, $y)
    Write-Host ("({0,4},{1,4})  #{2:X2}{3:X2}{4:X2}  rgb({2},{3},{4})" -f $x, $y, $c.R, $c.G, $c.B)
}
$img.Dispose()
