param(
    [string]$JuceSource = "",
    [ValidateSet("Release", "Debug")][string]$Configuration = "Release",
    [switch]$Package
)
$ErrorActionPreference = "Stop"
$projectDirectory = $PSScriptRoot
$buildDirectory = Join-Path $projectDirectory "build"
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
$cmakePath = if ($cmakeCommand) { $cmakeCommand.Source } else { "C:\Program Files\CMake\bin\cmake.exe" }
if (-not (Test-Path -LiteralPath $cmakePath)) { throw "Install CMake 3.22 or newer." }
$configure = @("-S", $projectDirectory, "-B", $buildDirectory, "-G", "Visual Studio 17 2022", "-A", "x64")
if ($JuceSource) { $configure += "-DBREAKVAR_JUCE_SOURCE=$JuceSource" }
& $cmakePath @configure
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed." }
foreach ($targetName in @("BreakVar_VST3", "SHFLRStandalone", "BreakVarModelTests", "BreakVarEngineTests", "BreakVarLibraryTests", "BreakVarProcessorTests", "BreakVarEditorTests")) {
    # Explicit projects avoid MSBuild ALL_BUILD worker-node stalls in restricted shells.
    # JUCE also enables parallel compilation inside each project. Limit compiler
    # processes when several of its large Windows modules need rebuilding.
    & $cmakePath --build $buildDirectory --config $Configuration --target $targetName --parallel 1 -- /nodeReuse:false /p:CL_MPCount=2
    if ($LASTEXITCODE -ne 0) { throw "Build failed for $targetName." }
}
$ctestPath = Join-Path (Split-Path $cmakePath) "ctest.exe"
& $ctestPath --test-dir $buildDirectory -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Tests failed." }
if ($Package) {
    if ($Configuration -ne "Release") { throw "Only Release builds can be packaged." }
    $bundlePath = Join-Path $buildDirectory "BreakVar_artefacts\Release\VST3\SHFLR.vst3"
    $standalonePath = Join-Path $buildDirectory "SHFLRStandalone_artefacts\Release\SHFLR.exe"
    $versionLine = Select-String -LiteralPath (Join-Path $buildDirectory "CMakeCache.txt") -Pattern '^CMAKE_PROJECT_VERSION:STATIC=(\d+\.\d+\.\d+)$'
    if (-not $versionLine) { throw "Missing project version in the configured build." }
    $version = $versionLine.Matches[0].Groups[1].Value
    $destination = Join-Path $projectDirectory "dist\SHFLR-$version-Windows-x64"
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    # Copy the complete VST3 bundle; never install into the system plugin folder.
    Copy-Item -LiteralPath $bundlePath -Destination $destination -Recurse -Force
    Copy-Item -LiteralPath $standalonePath -Destination $destination -Force
    Copy-Item -LiteralPath (Join-Path $projectDirectory "README.md") -Destination $destination -Force
    $zipPath = "$destination.zip"
    Compress-Archive -LiteralPath (Join-Path $destination "SHFLR.vst3"), (Join-Path $destination "SHFLR.exe"), (Join-Path $destination "README.md") -DestinationPath $zipPath -Force
    Write-Output "Testable VST3: $bundlePath"
    Write-Output "Standalone: $standalonePath"
    Write-Output "Package: $zipPath"
}
