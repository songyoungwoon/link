param(
    [ValidateRange(256, 4096)][int]$LongEdge = 1280,
    [ValidateRange(1, 100)][int]$Quality = 90,
    [switch]$SkipGeneration
)
$ErrorActionPreference = 'Stop'
function Get-ExhibitPhotoSortKey([IO.FileInfo]$Photograph) {
    $exhibitTrailingNumber = [regex]::Match($Photograph.BaseName, '^(.*?)([0-9]+)$')
    if (-not $exhibitTrailingNumber.Success) { return $Photograph.Name }
    $exhibitDigits = $exhibitTrailingNumber.Groups[2].Value.TrimStart([char]'0')
    if ($exhibitDigits.Length -eq 0) { $exhibitDigits = '0' }
    # Equal-length digit strings sort numerically without integer overflow.
    return '{0}|{1:D4}|{2}' -f $exhibitTrailingNumber.Groups[1].Value, $exhibitDigits.Length, $exhibitDigits
}
Add-Type -AssemblyName System.Drawing
$exhibitRepository = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$exhibitWorld = [IO.Path]::GetFullPath((Join-Path $exhibitRepository 'native\worlds\heukryong'))
$exhibitAssets = [IO.Path]::GetFullPath((Join-Path $exhibitWorld 'assets'))
$exhibitAllowed = [IO.Path]::GetFullPath((Join-Path $exhibitRepository 'native\worlds')) + [IO.Path]::DirectorySeparatorChar
if (-not $exhibitAssets.StartsWith($exhibitAllowed, [StringComparison]::OrdinalIgnoreCase)) { throw 'Derivative output must remain under native/worlds.' }
# Generated output must not traverse a directory junction or symbolic link.
foreach ($exhibitOutputDirectory in @((Join-Path $exhibitRepository 'native'), (Join-Path $exhibitRepository 'native\worlds'), $exhibitWorld, $exhibitAssets)) {
    if ((Test-Path -LiteralPath $exhibitOutputDirectory) -and ((Get-Item -LiteralPath $exhibitOutputDirectory -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Derivative output cannot traverse a reparse point: $exhibitOutputDirectory"
    }
}
[IO.Directory]::CreateDirectory($exhibitAssets) | Out-Null
$exhibitEncoder = [Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object { $_.MimeType -eq 'image/jpeg' } | Select-Object -First 1
if (-not $exhibitEncoder) { throw 'System.Drawing JPEG encoder is unavailable.' }
$exhibitRows = [Collections.Generic.List[object]]::new()
$exhibitKeep = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$exhibitFolders = @(
    @{ Folder = ([string][char]0xD751 + [char]0xB8E1 + [char]0xC774); Category = 'cat'; Required = $true },
    @{ Folder = 'songyoungwoon'; Category = 'owner'; Required = $false }
)
foreach ($exhibitGroup in $exhibitFolders) {
    $exhibitFolder = Join-Path (Join-Path $exhibitRepository 'image') $exhibitGroup.Folder
    if (-not (Test-Path -LiteralPath $exhibitFolder -PathType Container)) {
        if ($exhibitGroup.Required) { throw "Missing source image folder: $exhibitFolder" }
        continue
    }
    $exhibitFiles = @(Get-ChildItem -LiteralPath $exhibitFolder -File | Where-Object { $_.Extension.ToLowerInvariant() -in @('.jpg', '.jpeg', '.png', '.bmp') } | Sort-Object @{ Expression = { Get-ExhibitPhotoSortKey $_ } }, Name)
    if ($exhibitFiles.Count -eq 0 -and $exhibitGroup.Required) { throw "No supported photographs in $exhibitFolder" }
    $exhibitIndex = 0
    foreach ($exhibitFile in $exhibitFiles) {
        $exhibitIndex++
        $exhibitName = '{0}-{1:000}.jpg' -f $exhibitGroup.Category, $exhibitIndex
        $exhibitOutput = Join-Path $exhibitAssets $exhibitName
        if ((Test-Path -LiteralPath $exhibitOutput) -and ((Get-Item -LiteralPath $exhibitOutput -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Cannot overwrite a derivative reparse point: $exhibitOutput" }
        [void]$exhibitKeep.Add($exhibitName)
        $exhibitImage = $null; $exhibitBitmap = $null; $exhibitGraphics = $null; $exhibitParameters = $null
        try {
            $exhibitImage = [Drawing.Image]::FromFile($exhibitFile.FullName)
            $exhibitOriginalWidth = $exhibitImage.Width; $exhibitOriginalHeight = $exhibitImage.Height
            $exhibitOrientation = 1
            if ($exhibitImage.PropertyIdList -contains 0x0112) {
                $exhibitOrientation = [BitConverter]::ToUInt16($exhibitImage.GetPropertyItem(0x0112).Value, 0)
            }
            switch ($exhibitOrientation) {
                2 { $exhibitImage.RotateFlip([Drawing.RotateFlipType]::RotateNoneFlipX) }
                3 { $exhibitImage.RotateFlip([Drawing.RotateFlipType]::Rotate180FlipNone) }
                4 { $exhibitImage.RotateFlip([Drawing.RotateFlipType]::Rotate180FlipX) }
                5 { $exhibitImage.RotateFlip([Drawing.RotateFlipType]::Rotate90FlipX) }
                6 { $exhibitImage.RotateFlip([Drawing.RotateFlipType]::Rotate90FlipNone) }
                7 { $exhibitImage.RotateFlip([Drawing.RotateFlipType]::Rotate270FlipX) }
                8 { $exhibitImage.RotateFlip([Drawing.RotateFlipType]::Rotate270FlipNone) }
            }
            $exhibitUprightWidth = $exhibitImage.Width; $exhibitUprightHeight = $exhibitImage.Height
            $exhibitRatio = [Math]::Min(1.0, $LongEdge / [double][Math]::Max($exhibitUprightWidth, $exhibitUprightHeight))
            $exhibitWidth = [Math]::Max(1, [int][Math]::Round($exhibitUprightWidth * $exhibitRatio))
            $exhibitHeight = [Math]::Max(1, [int][Math]::Round($exhibitUprightHeight * $exhibitRatio))
            $exhibitBitmap = [Drawing.Bitmap]::new($exhibitWidth, $exhibitHeight, [Drawing.Imaging.PixelFormat]::Format24bppRgb)
            $exhibitGraphics = [Drawing.Graphics]::FromImage($exhibitBitmap)
            $exhibitGraphics.Clear([Drawing.Color]::White)
            $exhibitGraphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $exhibitGraphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $exhibitGraphics.CompositingQuality = [Drawing.Drawing2D.CompositingQuality]::HighQuality
            $exhibitGraphics.DrawImage($exhibitImage, 0, 0, $exhibitWidth, $exhibitHeight)
            $exhibitParameters = [Drawing.Imaging.EncoderParameters]::new(1)
            $exhibitParameters.Param[0] = [Drawing.Imaging.EncoderParameter]::new([Drawing.Imaging.Encoder]::Quality, [long]$Quality)
            $exhibitBitmap.Save($exhibitOutput, $exhibitEncoder, $exhibitParameters)
            $exhibitRelative = $exhibitFile.FullName.Substring($exhibitRepository.Length + 1).Replace('\', '/')
            $exhibitRows.Add([ordered]@{
                category = $exhibitGroup.Category; index = $exhibitIndex
                source = $exhibitRelative; source_sha256 = (Get-FileHash -LiteralPath $exhibitFile.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
                asset = "assets/$exhibitName"; width = $exhibitWidth; height = $exhibitHeight
                original_width = $exhibitOriginalWidth; original_height = $exhibitOriginalHeight
                upright_width = $exhibitUprightWidth; upright_height = $exhibitUprightHeight
                exif_orientation = $exhibitOrientation
            })
        } finally {
            if ($exhibitParameters) { $exhibitParameters.Dispose() }
            if ($exhibitGraphics) { $exhibitGraphics.Dispose() }
            if ($exhibitBitmap) { $exhibitBitmap.Dispose() }
            if ($exhibitImage) { $exhibitImage.Dispose() }
        }
    }
}
$exhibitInventory = [ordered]@{ version = 1; long_edge = $LongEdge; jpeg_quality = $Quality; count = $exhibitRows.Count; photographs = @($exhibitRows.ToArray()) }
$exhibitJson = $exhibitInventory | ConvertTo-Json -Depth 8
[IO.File]::WriteAllText((Join-Path $exhibitWorld 'inventory.json'), $exhibitJson, [Text.UTF8Encoding]::new($false))
# Only remove obsolete numbered derivatives inside this exact generated directory.
# Source photographs and any unrelated files are never deletion candidates.
$exhibitRemoved = 0
foreach ($exhibitStale in @(Get-ChildItem -LiteralPath $exhibitAssets -File | Where-Object { $_.Name -match '^(cat|owner)-[0-9]{3}\.jpg$' -and -not $exhibitKeep.Contains($_.Name) })) {
    $exhibitStalePath = [IO.Path]::GetFullPath($exhibitStale.FullName)
    if ([IO.Path]::GetDirectoryName($exhibitStalePath) -ne $exhibitAssets) { throw 'Stale derivative escaped its generated output directory.' }
    Remove-Item -LiteralPath $exhibitStalePath -Force
    $exhibitRemoved++
}
Write-Host "Prepared $($exhibitRows.Count) upright display copies; original image files were opened read-only."
Write-Host "Removed $exhibitRemoved obsolete generated display copies."
if (-not $SkipGeneration) {
    & python (Join-Path $PSScriptRoot 'generate_exhibition.py')
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
