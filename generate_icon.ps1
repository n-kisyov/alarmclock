Add-Type -AssemblyName System.Drawing

function New-ClockIcon {
    param(
        [int]$Size = 32,
        [string]$OutputPath
    )

    $bmp = New-Object System.Drawing.Bitmap($Size, $Size)
    $g   = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)

    $cx  = $Size / 2.0
    $cy  = $Size / 2.0
    $pad = 1.5

    $outer    = [System.Drawing.RectangleF]::new($pad, $pad, $Size - 2*$pad, $Size - 2*$pad)
    $rimColor = [System.Drawing.Color]::FromArgb(255, 48, 56, 65)
    $faceColor = [System.Drawing.Color]::FromArgb(255, 245, 245, 250)
    $handColor = [System.Drawing.Color]::FromArgb(255, 48, 56, 65)
    $centerColor = [System.Drawing.Color]::FromArgb(255, 220, 38, 38)

    $rimPen = New-Object System.Drawing.Pen($rimColor, 2.0)
    $g.FillEllipse([System.Drawing.Brushes]::White, $outer)
    $g.DrawEllipse($rimPen, $outer)

    $facePen = New-Object System.Drawing.Pen($faceColor, 0)
    $inner = [System.Drawing.RectangleF]::new($pad + 2, $pad + 2, $Size - 2*$pad - 4, $Size - 2*$pad - 4)
    $g.FillEllipse([System.Drawing.Brushes]::White, $inner)

    $handPen = New-Object System.Drawing.Pen($handColor, 2.0)
    $handPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $handPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round

    $hourR   = ($Size / 2.0 - $pad - 2) * 0.50
    $minuteR = ($Size / 2.0 - $pad - 2) * 0.70

    $hourAngle   = -30.0 * [Math]::PI / 180.0
    $minuteAngle = 60.0  * [Math]::PI / 180.0

    $g.DrawLine($handPen,
        $cx, $cy,
        $cx + $hourR   * [Math]::Cos($hourAngle),
        $cy + $hourR   * [Math]::Sin($hourAngle))

    $minPen = New-Object System.Drawing.Pen($handColor, 1.5)
    $minPen.EndCap   = [System.Drawing.Drawing2D.LineCap]::Round
    $minPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $g.DrawLine($minPen,
        $cx, $cy,
        $cx + $minuteR * [Math]::Cos($minuteAngle),
        $cy + $minuteR * [Math]::Sin($minuteAngle))

    $dotPen = New-Object System.Drawing.Pen($centerColor, 3.0)
    $dotPen.EndCap   = [System.Drawing.Drawing2D.LineCap]::Round
    $dotPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $g.DrawLine($dotPen, $cx, $cy, $cx + 0.01, $cy + 0.01)

    $g.Dispose()
    $rimPen.Dispose()
    $handPen.Dispose()
    $minPen.Dispose()
    $dotPen.Dispose()
    $facePen.Dispose()

    $hIcon = $bmp.GetHicon()
    $icon  = [System.Drawing.Icon]::FromHandle($hIcon)
    $fs    = New-Object System.IO.FileStream($OutputPath, [System.IO.FileMode]::Create)
    $icon.Save($fs)
    $fs.Close()
    $icon.Dispose()
    $bmp.Dispose()

    Write-Host "Generated icon: $OutputPath ($Size x $Size)"
}

$outIcon = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) "resources\alarmclock.ico"
New-ClockIcon -Size 32 -OutputPath $outIcon
