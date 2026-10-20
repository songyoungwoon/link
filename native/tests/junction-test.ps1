param([Parameter(Mandatory=$true)][string]$Executable,[Parameter(Mandatory=$true)][string]$BuildDirectory)
$ErrorActionPreference = 'Stop'
$linkBase = [System.IO.Path]::GetFullPath($BuildDirectory)
$linkFixture = Join-Path $linkBase ('link-junction-fixture-' + [guid]::NewGuid().ToString('N'))
$linkLinkOne = Join-Path $linkFixture 'worlds\escaped'
$linkLinkTwo = Join-Path $linkFixture 'worlds\room-a\escape'
New-Item -ItemType Directory -Path (Join-Path $linkFixture 'worlds\room-a'),(Join-Path $linkFixture 'outside') -Force | Out-Null
Set-Content -LiteralPath (Join-Path $linkFixture 'outside\payload.element') -Value 'scene {}' -Encoding ASCII
try {
    New-Item -ItemType Junction -Path $linkLinkOne -Target (Join-Path $linkFixture 'outside') | Out-Null
    New-Item -ItemType Junction -Path $linkLinkTwo -Target (Join-Path $linkFixture 'outside') | Out-Null
    & $Executable --junction-fixture $linkFixture
    $linkResult = $LASTEXITCODE
} finally {
    foreach ($linkLink in @($linkLinkOne,$linkLinkTwo)) {
        if ([System.IO.Directory]::Exists($linkLink)) { [System.IO.Directory]::Delete($linkLink) }
    }
    $linkResolvedFixture = [System.IO.Path]::GetFullPath($linkFixture)
    if ($linkResolvedFixture.StartsWith($linkBase + [System.IO.Path]::DirectorySeparatorChar,[System.StringComparison]::OrdinalIgnoreCase) -and (Split-Path -Leaf $linkResolvedFixture) -like 'link-junction-fixture-*') {
        Remove-Item -LiteralPath $linkResolvedFixture -Recurse -Force
    } else { throw 'Refusing fixture cleanup outside intended build directory.' }
}
exit $linkResult
