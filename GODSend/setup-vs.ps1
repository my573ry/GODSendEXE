# setup-vs.ps1 - Load Visual Studio Build Tools into current PowerShell session

# Find vcvarsall.bat using vswhere
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"

if (Test-Path $vswhere) {
    $vcvarsall = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($vcvarsall) {
        $vcvarsall = Join-Path $vcvarsall 'VC\Auxiliary\Build\vcvarsall.bat'
    }
} else {
    # Fallback: Try common locations
    $paths = @(
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
    )
    
    $vcvarsall = $paths | Where-Object { Test-Path $_ } | Select-Object -First 1
}

if ($vcvarsall -and (Test-Path $vcvarsall)) {
    Write-Host "Found Visual Studio at: $vcvarsall" -ForegroundColor Green
    
    # Run vcvarsall.bat and capture environment variables
    cmd /c "`"$vcvarsall`" x64 & set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') {
            $name = $matches[1]
            $value = $matches[2]
            Set-Item -Force -Path "ENV:\$name" -Value $value
        }
    }
    
    Write-Host "Visual Studio environment loaded successfully!" -ForegroundColor Green
    Write-Host "You can now use: cl, link, nmake, etc." -ForegroundColor Cyan
} else {
    Write-Host "Visual Studio not found. Install Visual Studio Build Tools:" -ForegroundColor Red
    Write-Host "  winget install --id=Microsoft.VisualStudio.2022.BuildTools -e" -ForegroundColor Yellow
}