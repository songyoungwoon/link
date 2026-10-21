param([switch]$Test, [switch]$Fresh)
$ErrorActionPreference = 'Stop'
$linkRepository = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$linkCmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
$linkCmake = $null
if ($linkCmakeCommand) {
    $linkCmake = $linkCmakeCommand.Source
} else {
    $linkVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $linkVswhere) {
        $linkInstallations = & $linkVswhere -all -products '*' -property installationPath
        foreach ($linkInstallation in $linkInstallations) {
            $linkCandidate = Join-Path $linkInstallation 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
            if (Test-Path -LiteralPath $linkCandidate) { $linkCmake = $linkCandidate; break }
        }
    }
}
if (-not $linkCmake) { throw 'Install Visual Studio 2022 Desktop development with C++ and CMake tools. Cloning the source does not include LINK.exe.' }
$linkBuildDirectory = Join-Path $linkRepository 'build\native'
$linkCache = Join-Path $linkBuildDirectory 'CMakeCache.txt'
if (Test-Path -LiteralPath $linkCache) {
    $linkCachedSource = Get-Content -LiteralPath $linkCache -Encoding UTF8 | Where-Object { $_.StartsWith('CMAKE_HOME_DIRECTORY:INTERNAL=') } | Select-Object -First 1
    if ($linkCachedSource) {
        $linkPreviousSource = $linkCachedSource.Substring('CMAKE_HOME_DIRECTORY:INTERNAL='.Length).Replace('/', '\')
        if (-not [string]::Equals($linkPreviousSource.TrimEnd('\'), $linkRepository.TrimEnd('\'), [StringComparison]::OrdinalIgnoreCase)) {
            Write-Host 'Project directory changed; refreshing the generated CMake cache.'
            $Fresh = $true
        }
    }
}
$linkConfigureArguments = @('-S', $linkRepository, '-B', $linkBuildDirectory, '-G', 'Visual Studio 17 2022', '-A', 'x64')
if ($Fresh) { $linkConfigureArguments += '--fresh' }
& $linkCmake @linkConfigureArguments
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $linkCmake --build $linkBuildDirectory --config Release --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if ($Test) {
    $linkCtest = Join-Path (Split-Path -Parent $linkCmake) 'ctest.exe'
    & $linkCtest --test-dir $linkBuildDirectory -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}