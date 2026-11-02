# ReKapture - produccion: carpeta portable (.zip) e instalador (.exe)
# Uso:  powershell -ExecutionPolicy Bypass -File D:\Dev\ReKapture\scripts\package.ps1
# Opciones:  -SkipBuild  (reutiliza el ultimo build)   -ObsDir D:\Dev\obs-studio
# Resultado en D:\Dev\ReKapture\dist\ y un informe: dist\package-report.txt
param(
    [string]$ObsDir = 'D:\Dev\obs-studio',
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$root   = Split-Path $PSScriptRoot -Parent
$dist   = Join-Path $root 'dist'
$build  = Join-Path $root 'build'
$report = Join-Path $dist 'package-report.txt'

New-Item -ItemType Directory -Force -Path $dist | Out-Null
"=== ReKapture package report ($(Get-Date -Format s)) ===" | Out-File $report -Encoding utf8
function Log($t) { Write-Host $t; $t | Out-File -FilePath $report -Append -Encoding utf8 }
function Fail($t) { Log "ERROR: $t"; Write-Host "Sube $report a Claude." -ForegroundColor Yellow; exit 1 }

# Version desde CMakeLists.txt
$m = Select-String -Path (Join-Path $root 'CMakeLists.txt') -Pattern 'project\(ReKapture VERSION ([0-9.]+)'
if (-not $m) { Fail 'No se pudo leer la version de CMakeLists.txt' }
$ver = $m.Matches[0].Groups[1].Value
Log "Version: $ver"

# 1) Compilar
if (-not $SkipBuild) {
    Log '1/6 Compilando (RelWithDebInfo)...'
    $obsFwd = $ObsDir -replace '\\','/'
    cmd /c "cmake -S `"$root`" -B `"$build`" -G `"Visual Studio 18 2026`" -A x64 -DOBS_SOURCE_DIR=`"$obsFwd`" > `"$dist\configure.log`" 2>&1"
    if ($LASTEXITCODE -ne 0) { Fail "Fallo la configuracion (ver dist\configure.log)" }
    cmd /c "cmake --build `"$build`" --config RelWithDebInfo > `"$dist\build.log`" 2>&1"
    if ($LASTEXITCODE -ne 0) {
        Log (Select-String -Path "$dist\build.log" -Pattern 'error' -SimpleMatch | Select-Object -First 15 | ForEach-Object { $_.Line })
        Fail "Fallo la compilacion (ver dist\build.log)"
    }
    Log 'Compilacion correcta.'
} else {
    Log '1/6 Compilacion omitida (-SkipBuild).'
}

$deploy = Join-Path $build 'deploy'
if (-not (Test-Path (Join-Path $deploy 'bin\64bit\ReKapture.exe'))) { Fail "No existe $deploy\bin\64bit\ReKapture.exe" }

# 2) Carpeta de trabajo limpia
Log '2/6 Preparando la carpeta portable...'
$stage = Join-Path $dist "ReKapture-$ver-portable"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
Copy-Item -Recurse -Force $deploy $stage

# Limpieza: simbolos de depuracion, el OBS completo y modulos que ReKapture no usa
Get-ChildItem $stage -Recurse -Filter *.pdb | Remove-Item -Force
Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $stage 'bin\64bit\obs64.exe')
Remove-Item -Recurse -Force -ErrorAction SilentlyContinue (Join-Path $stage 'data\obs-studio')
$keepModules = @('obs-ffmpeg','obs-nvenc','obs-outputs','obs-qsv11','obs-x264','win-capture','win-wasapi','image-source','obs-filters','win-dshow','obs-vst')
$core = Join-Path $stage 'core'
if (Test-Path $core) {
    Get-ChildItem $core -Directory | Where-Object { $keepModules -notcontains $_.Name } | Remove-Item -Recurse -Force
}

# 3) Archivos de apoyo: modo portable, lanzador, licencias
Log '3/6 Anadiendo lanzador, modo portable y licencias...'
'Modo portable de ReKapture: los ajustes, los registros y las skins se guardan en la carpeta UserData.' |
    Out-File -FilePath (Join-Path $stage 'portable.flag') -Encoding ascii
"@echo off`r`nstart `"`" `"%~dp0bin\64bit\ReKapture.exe`"`r`n" | Out-File -FilePath (Join-Path $stage 'ReKapture.cmd') -Encoding ascii

@"
ReKapture $ver (portable)
=========================
Abre ReKapture.cmd (o bin\64bit\ReKapture.exe).

- Los ajustes, el registro y las skins se guardan en la carpeta UserData, junto al programa.
- Las grabaciones se guardan por defecto en Videos\ReKapture (puedes cambiarlo en la ventana).
- Para desinstalar, borra la carpeta.
- Si Windows SmartScreen avisa en el primer arranque, es porque el programa no esta firmado digitalmente.
"@ | Out-File -FilePath (Join-Path $stage 'LEEME-portable.txt') -Encoding utf8

$lic = Join-Path $stage 'LICENSES'
New-Item -ItemType Directory -Force -Path $lic | Out-Null
Copy-Item -Force (Join-Path $root 'LICENSES\LEEME.txt') $lic -ErrorAction SilentlyContinue
if (Test-Path "$ObsDir\COPYING") { Copy-Item -Force "$ObsDir\COPYING" (Join-Path $lic 'OBS-Studio-COPYING.txt') } else { Log 'AVISO: no se encontro COPYING de OBS.' }
foreach ($pattern in @('obs-deps-qt6-*-x64', 'obs-deps-2*-x64')) {
    $dir = Get-ChildItem "$ObsDir\.deps" -Directory -Filter $pattern -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($dir -and (Test-Path "$($dir.FullName)\licenses")) {
        Copy-Item -Recurse -Force "$($dir.FullName)\licenses" (Join-Path $lic $dir.Name)
    } else { Log "AVISO: no se encontraron licencias en $pattern" }
}

# 4) Autotest sin ventana
Log '4/6 Ejecutando el autotest del paquete (libobs + modulos)...'
$exe = Join-Path $stage 'bin\64bit\ReKapture.exe'
$proc = Start-Process -FilePath $exe -ArgumentList '--selftest' -WorkingDirectory (Split-Path $exe) -PassThru -Wait
$selfLog = Join-Path $stage 'UserData\logs\rekapture.log'
if (Test-Path $selfLog) {
    Log (Select-String -Path $selfLog -Encoding UTF8 -Pattern 'AUTOTEST|Modulo no|Error|ERROR' | Select-Object -Last 12 | ForEach-Object { $_.Line })
}
Log "Codigo de salida del autotest: $($proc.ExitCode)"
if (Test-Path (Join-Path $stage 'UserData')) { Remove-Item -Recurse -Force (Join-Path $stage 'UserData') }  # el paquete se entrega limpio
if ($proc.ExitCode -ne 0) { Fail 'El autotest fallo: el paquete portable no es fiable. Revisa el registro.' }

# 5) Zip portable
Log '5/6 Creando el .zip portable...'
$zip = Join-Path $dist "ReKapture-$ver-portable.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $stage -DestinationPath $zip -CompressionLevel Optimal

# 6) Instalador (Inno Setup, gratuito)
Log '6/6 Creando el instalador...'
function Find-Iscc {
    @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
      "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
}
$iscc = Find-Iscc
if (-not $iscc -and (Get-Command winget -ErrorAction SilentlyContinue)) {
    $answer = Read-Host 'Inno Setup (gratuito, necesario para crear el instalador) no esta instalado. Instalarlo ahora con winget? (S/N)'
    if ($answer -match '^[sSyY]') {
        winget install --id JRSoftware.InnoSetup -e --silent --accept-package-agreements --accept-source-agreements
        $iscc = Find-Iscc
    }
}
$setup = $null
if ($iscc) {
    cmd /c "`"$iscc`" /Qp `"/DAppVersion=$ver`" `"/DSourceDir=$stage`" `"$root\installer\ReKapture.iss`" > `"$dist\installer.log`" 2>&1"
    if ($LASTEXITCODE -ne 0) {
        Log (Get-Content "$dist\installer.log" -Tail 15)
        Log 'AVISO: no se pudo crear el instalador (el .zip portable si esta listo).'
    } else {
        $setup = Join-Path $dist "ReKapture-Setup-$ver.exe"
    }
} else {
    Log 'AVISO: Inno Setup no esta instalado, asi que no se creo el instalador.'
    Log 'Para crearlo: winget install --id JRSoftware.InnoSetup -e   y vuelve a ejecutar este script con -SkipBuild'
}

# Informe final
Log "`n--- Resultado ---"
foreach ($f in @($zip, $setup) | Where-Object { $_ -and (Test-Path $_) }) {
    $item = Get-Item $f
    $hash = (Get-FileHash $f -Algorithm SHA256).Hash
    Log ("{0}  {1:N1} MB  SHA256={2}" -f $item.Name, ($item.Length/1MB), $hash)
}
$size = (Get-ChildItem $stage -Recurse -File | Measure-Object Length -Sum).Sum
Log ("Carpeta portable: {0}  ({1:N0} MB descomprimida)" -f $stage, ($size/1MB))
Write-Host "`nTERMINADO. Sube a Claude: $report" -ForegroundColor Green
