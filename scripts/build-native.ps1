param([switch]$Test)
$ErrorActionPreference = 'Stop'
$linkRepository = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$linkCmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
if ($linkCmakeCommand) {
    $linkCmake = $linkCmakeCommand.Source
} else {
    $linkVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $linkInstallations = & $linkVswhere -all -products '*' -property installationPath
    $linkCmake = $null
    foreach ($linkInstallation in $linkInstallations) {
        $linkCandidate = Join-Path $linkInstallation 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
        if (Test-Path -LiteralPath $linkCandidate) { $linkCmake = $linkCandidate; break }
    }
}
if (-not $linkCmake) { throw 'Install Visual Studio 2022 Desktop development with C++ and CMake tools.' }
& $linkCmake -S $linkRepository -B (Join-Path $linkRepository 'build\native') -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $linkCmake --build (Join-Path $linkRepository 'build\native') --config Release --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if ($Test) {
    $linkCtest = Join-Path (Split-Path -Parent $linkCmake) 'ctest.exe'
    & $linkCtest --test-dir (Join-Path $linkRepository 'build\native') -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
