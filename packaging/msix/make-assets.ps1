# Placeholder logos for the MSIX package, drawn at build time so no binary is
# committed. A Store listing will want designed artwork; these exist so the
# package is complete and installable. Usage: make-assets.ps1 -OutDir <dir>
param([Parameter(Mandatory = $true)][string]$OutDir)

Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$sizes = @{
    'StoreLogo.png'          = 50
    'Square44x44Logo.png'    = 44
    'Square150x150Logo.png'  = 150
}
foreach ($name in $sizes.Keys) {
    $n = $sizes[$name]
    $bmp = New-Object System.Drawing.Bitmap $n, $n
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $g.TextRenderingHint = 'AntiAliasGridFit'
    $g.Clear([System.Drawing.Color]::FromArgb(255, 32, 64, 128))
    $font = New-Object System.Drawing.Font 'Segoe UI', ([single]($n * 0.42)), ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
    $fmt = New-Object System.Drawing.StringFormat
    $fmt.Alignment = 'Center'
    $fmt.LineAlignment = 'Center'
    $g.DrawString('gB', $font, [System.Drawing.Brushes]::White, (New-Object System.Drawing.RectangleF 0, 0, $n, $n), $fmt)
    $bmp.Save((Join-Path $OutDir $name), [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
}
