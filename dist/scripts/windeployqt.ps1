param
(
    $NightlyVersion = "",
    [switch]$NoInstaller
)

# Run windeployqt
$isCrossCompile = $env:buildArch -eq 'Arm64'
$winDeployQt = $isCrossCompile ? "$(qmake -query QT_HOST_PREFIX)\bin\windeployqt" : "windeployqt"
$argQtPaths = $isCrossCompile ? "--qtpaths=$env:QT_ROOT_DIR\bin\qtpaths.bat" : $null
& $winDeployQt $argQtPaths --no-compiler-runtime --no-opengl-sw --no-system-d3d-compiler "bin\qView.exe"

# Copy qView's own translation files next to the Qt ones (they are no longer embedded)
if (Test-Path "build\translations") {
    New-Item -ItemType Directory -Force -Path "bin\translations" | Out-Null
    Copy-Item "build\translations\*.qm" "bin\translations" -Force
}

$imfDir = "bin\imageformats"
if ((Test-Path "$imfDir\kimg_tga.dll") -and (Test-Path "$imfDir\qtga.dll")) {
    # Prefer kimageformats TGA plugin which supports more formats
    Write-Output "Removing duplicate TGA plugin"
    Remove-Item "$imfDir\qtga.dll"
}

if ($NoInstaller) {
    # Keep the portable qView.exe as-is, without building an installer
    Write-Host "Skipping installer build"
} elseif ($NightlyVersion -eq '') {
    # Call innomake if we are not building a nightly version (no version passed)
    & "dist/scripts/innomake.ps1"
} else {
    # Do renaming-y stuff otherwise
    mv bin\qView.exe "bin\qView-nightly-$NightlyVersion.exe"
}
