# SPDX-License-Identifier: Apache-2.0
#
# Generates kf_client application icon app.ico (multi-entry hybrid format):
#   - 32x32  BMP entry  : readable by Qt's qico plugin (runtime setWindowIcon)
#   - 256x256 PNG entry : high-res exe icon for Explorer/taskbar
#
# Why hybrid: Qt's qico plugin only parses legacy BMP-based entries, not
# PNG-in-ICO entries; Win32 (Explorer) supports both. A single PNG-in-ICO
# would make runtime window-icon loading fail and fall back to the Qt logo.
#
# Usage: pwsh gen_icon.ps1    Output: ../resources/app.ico
# NOTE: keep this file pure-ASCII; powershell.exe 5.1 reads no-BOM UTF-8 as
# ANSI/GBK and multi-byte CJK comments can corrupt line boundaries.

Add-Type -AssemblyName System.Drawing

# Draw the icon on the given Graphics, scaling 256-base coords by f = size/256.
function Draw-Icon($g, $size) {
  $f = $size / 256.0
  $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
  $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
  $g.Clear([System.Drawing.Color]::Transparent)

  # Rounded-rect background, blue gradient
  $margin = $f * 4.0
  $r = $f * 56.0
  $rect = [System.Drawing.RectangleF]::new($margin, $margin, ($size - 2 * $margin), ($size - 2 * $margin))
  $bgPath = [System.Drawing.Drawing2D.GraphicsPath]::new()
  $bgPath.AddArc($rect.X, $rect.Y, $r, $r, 180, 90)
  $bgPath.AddArc($rect.Right - $r, $rect.Y, $r, $r, 270, 90)
  $bgPath.AddArc($rect.Right - $r, $rect.Bottom - $r, $r, $r, 0, 90)
  $bgPath.AddArc($rect.X, $rect.Bottom - $r, $r, $r, 90, 90)
  $bgPath.CloseFigure()
  $c1 = [System.Drawing.Color]::FromArgb(53, 120, 194)
  $c2 = [System.Drawing.Color]::FromArgb(22, 58, 102)
  $bgBrush = [System.Drawing.Drawing2D.LinearGradientBrush]::new($rect, $c1, $c2, [float]90)
  $g.FillPath($bgBrush, $bgPath)
  $bgBrush.Dispose()

  # Three green rising candles with white wicks (256-base coords, scaled by f)
  $penW = [float]($f * 5.0)
  $whitePen = [System.Drawing.Pen]::new([System.Drawing.Color]::White, $penW)
  $whitePen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
  $whitePen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
  $greenBrush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(74, 216, 136))

  # Candle 1 (left, shortest)
  $x = 64 * $f
  $g.DrawLine($whitePen, $x, (118 * $f), $x, (210 * $f))
  $g.FillRectangle($greenBrush, [System.Drawing.RectangleF]::new((42 * $f), (148 * $f), (44 * $f), (48 * $f)))
  # Candle 2 (middle)
  $x = 128 * $f
  $g.DrawLine($whitePen, $x, (90 * $f), $x, (196 * $f))
  $g.FillRectangle($greenBrush, [System.Drawing.RectangleF]::new((106 * $f), (116 * $f), (44 * $f), (60 * $f)))
  # Candle 3 (right, tallest)
  $x = 192 * $f
  $g.DrawLine($whitePen, $x, (52 * $f), $x, (176 * $f))
  $g.FillRectangle($greenBrush, [System.Drawing.RectangleF]::new((170 * $f), (78 * $f), (44 * $f), (72 * $f)))

  $whitePen.Dispose()
  $greenBrush.Dispose()
}

# Build 32x32 BMP entry bytes (BITMAPINFOHEADER + XOR pixels + AND mask)
function Build-BmpEntry($sz) {
  $pf = [System.Drawing.Imaging.PixelFormat]::Format32bppArgb
  $bmp = [System.Drawing.Bitmap]::new($sz, $sz, $pf)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  Draw-Icon $g $sz
  $g.Dispose()

  $rect = [System.Drawing.Rectangle]::new(0, 0, $sz, $sz)
  $bd = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, $pf)
  $stride = $bd.Stride
  $pixelLen = $stride * $sz
  $pixels = [byte[]]::new($pixelLen)
  [System.Runtime.InteropServices.Marshal]::Copy($bd.Scan0, $pixels, 0, $pixelLen)
  $bmp.UnlockBits($bd)
  $bmp.Dispose()

  # AND mask: ceil(sz/8) bytes per row, 4-byte aligned; all zero (32bpp opaque)
  $maskRowBytes = [int][Math]::Ceiling($sz / 8.0)
  $maskRowBytes = [int][Math]::Ceiling($maskRowBytes / 4.0) * 4
  $maskLen = $maskRowBytes * $sz
  $mask = [byte[]]::new($maskLen)

  $ms = [System.IO.MemoryStream]::new()
  $w = [System.IO.BinaryWriter]::new($ms)
  $w.Write([uint32]40)                       # biSize
  $w.Write([int32]$sz)                        # biWidth
  $w.Write([int32](2 * $sz))                 # biHeight = 2*sz
  $w.Write([uint16]1)                         # biPlanes
  $w.Write([uint16]32)                       # biBitCount
  $w.Write([uint32]0)                        # biCompression (BI_RGB)
  $w.Write([uint32]($pixelLen + $maskLen))   # biSizeImage
  $w.Write([int32]0)                         # biXPelsPerMeter
  $w.Write([int32]0)                         # biYPelsPerMeter
  $w.Write([uint32]0)                        # biClrUsed
  $w.Write([uint32]0)                        # biClrImportant
  $w.Write($pixels)                          # XOR (GDI+ is already bottom-up)
  $w.Write($mask)                            # AND mask
  $w.Flush()
  $entry = $ms.ToArray()
  $w.Dispose()
  $ms.Dispose()
  return ,$entry
}

# Build 256 PNG entry bytes
function Build-PngEntry() {
  $sz = 256
  $pf = [System.Drawing.Imaging.PixelFormat]::Format32bppArgb
  $bmp = [System.Drawing.Bitmap]::new($sz, $sz, $pf)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  Draw-Icon $g $sz
  $g.Dispose()
  $ms = [System.IO.MemoryStream]::new()
  $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
  $png = $ms.ToArray()
  $ms.Dispose()
  $bmp.Dispose()
  return ,$png
}

# Assemble multi-entry ICO
$bmp32 = Build-BmpEntry 32
$png256 = Build-PngEntry

$outDir = Join-Path $PSScriptRoot '..\resources'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$icoPath = Join-Path $outDir 'app.ico'

$headerLen = 6 + 2 * 16      # ICONDIR + 2*ICONDIRENTRY
$off0 = $headerLen
$off1 = $off0 + $bmp32.Length

$fs = [System.IO.File]::Create($icoPath)
$bw = [System.IO.BinaryWriter]::new($fs)
# ICONDIR
$bw.Write([uint16]0)    # reserved
$bw.Write([uint16]1)    # type = icon
$bw.Write([uint16]2)    # image count
# entry0: 32x32 BMP
$bw.Write([byte]32)                 # width
$bw.Write([byte]32)                 # height
$bw.Write([byte]0)                  # colors
$bw.Write([byte]0)                  # reserved
$bw.Write([uint16]1)                # planes
$bw.Write([uint16]32)              # bit count
$bw.Write([uint32]$bmp32.Length)   # bytes in res
$bw.Write([uint32]$off0)           # offset
# entry1: 256x256 PNG
$bw.Write([byte]0)                  # width 256 => 0
$bw.Write([byte]0)                  # height 256 => 0
$bw.Write([byte]0)                  # colors
$bw.Write([byte]0)                  # reserved
$bw.Write([uint16]1)                # planes
$bw.Write([uint16]32)              # bit count
$bw.Write([uint32]$png256.Length)  # bytes in res
$bw.Write([uint32]$off1)           # offset
# image data
$bw.Write($bmp32)
$bw.Write($png256)
$bw.Flush()
$bw.Dispose()
$fs.Dispose()

$len = (Get-Item $icoPath).Length
Write-Host ("Generated: {0} ({1} bytes; 32x32 BMP={2}, 256x256 PNG={3})" -f $icoPath, $len, $bmp32.Length, $png256.Length)
