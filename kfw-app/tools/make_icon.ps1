# Generates the KFW application icon (multi-size .ico from GDI+ drawing)
Add-Type -AssemblyName System.Drawing

function New-RoundedPath([float]$x, [float]$y, [float]$w, [float]$h, [float]$r)
{
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = 2 * $r
    $p.AddArc($x, $y, $d, $d, 180, 90)
    $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $p.CloseFigure()
    return $p
}

function New-IconBitmap([int]$s)
{
    $bmp = New-Object System.Drawing.Bitmap($s, $s)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality

    $u = $s / 256.0   # design in 256-space, scale everything

    # rounded navy background with vertical gradient
    $bg = New-RoundedPath (16*$u) (16*$u) (224*$u) (224*$u) (54*$u)
    $grad = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        (New-Object System.Drawing.Point(0, [int](16*$u))),
        (New-Object System.Drawing.Point(0, $s)),
        [System.Drawing.Color]::FromArgb(255, 30, 42, 59),
        [System.Drawing.Color]::FromArgb(255, 12, 21, 34))
    $g.FillPath($grad, $bg)
    $g.DrawPath((New-Object System.Drawing.Pen(
            [System.Drawing.Color]::FromArgb(120, 96, 170, 255), (3*$u))), $bg)
    $bg.Dispose(); $grad.Dispose()

    # magnifier lens
    $cx = 114*$u; $cy = 112*$u; $r = 58*$u
    $lensRect = New-Object System.Drawing.Rectangle(
        [float]($cx - $r), [float]($cy - $r), [float](2*$r), [float](2*$r))
    $g.DrawEllipse((New-Object System.Drawing.Pen(
            [System.Drawing.Color]::FromArgb(255, 79, 176, 255), (15*$u))), $lensRect)

    # handle
    $hp = New-Object System.Drawing.Pen(
        [System.Drawing.Color]::FromArgb(255, 79, 176, 255), (24*$u))
    $hp.StartCap = $hp.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $dx = 0.7071; $edge = $r - 6*$u
    $g.DrawLine($hp,
        ($cx + $edge*$dx), ($cy + $edge*$dx),
        ($cx + $edge*$dx + 40*$u*$dx), ($cy + $edge*$dx + 40*$u*$dx))
    $hp.Dispose()

    # waveform bars inside the lens
    $wp = New-Object System.Drawing.Pen(
        [System.Drawing.Color]::FromArgb(255, 143, 216, 255), (7*$u))
    $wp.StartCap = $wp.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $bars = @(@(92,24), @(108,44), @(124,30), @(140,50), @(154,18))
    foreach ($b in $bars) {
        $bx = $b[0]*$u; $bh = $b[1]*$u
        $g.DrawLine($wp, $bx, ($cy - $bh/2), $bx, ($cy + $bh/2))
    }
    $wp.Dispose()
    $g.Dispose()
    return $bmp
}

$sizeEmblem = [System.Drawing.Imaging.ImageFormat]::Png
$big = New-IconBitmap 256
$big.Save("$PSScriptRoot\icon-preview-256.png", $sizeEmblem)
$big.Dispose()

$sizes = @(256, 128, 64, 48, 32, 24, 16)
$pngs = @{}
foreach ($s in $sizes) {
    $b = New-IconBitmap $s
    $ms = New-Object System.IO.MemoryStream
    $b.Save($ms, $sizeEmblem)
    $pngs[$s] = $ms.ToArray()
    $ms.Dispose()
    $b.Dispose()
}

$out = "$PSScriptRoot\..\kfw-app\ui\kfw.ico"
$fs = [System.IO.File]::Create($out)
$bw = New-Object System.IO.BinaryWriter -ArgumentList $fs
$offset = 6 + 16 * $sizes.Count
$bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]$sizes.Count)
foreach ($s in $sizes) {
    $data = $pngs[$s]
    $wb = [byte]($(if ($s -eq 256) { 0 } else { $s }))
    $bw.Write($wb); $bw.Write($wb)
    $bw.Write([byte]0); $bw.Write([byte]0)
    $bw.Write([uint16]1); $bw.Write([uint16]32)
    $bw.Write([uint32]$data.Length)
    $bw.Write([uint32]$offset)
    $offset += $data.Length
}
foreach ($s in $sizes) { $bw.Write($pngs[$s]) }
$bw.Dispose(); $fs.Dispose()
Write-Host ("kfw.ico written: {0} sizes, {1} bytes" -f $sizes.Count, (Get-Item $out).Length)
