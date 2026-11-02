# ReKapture - compila, ejecuta y genera un informe (run-report.txt en la carpeta del proyecto)
# Uso:  powershell -ExecutionPolicy Bypass -File D:\Dev\ReKapture\scripts\build.ps1
param([string]$ObsDir = 'D:\Dev\obs-studio')

$root   = Split-Path $PSScriptRoot -Parent
$build  = Join-Path $root 'build'
$report = Join-Path $root 'run-report.txt'
$cfgLog = Join-Path $root 'cmake-configure.log'
$bldLog = Join-Path $root 'cmake-build.log'

function Add-Report($t) { $t | Out-File -FilePath $report -Append -Encoding utf8 }
"=== ReKapture run report ($(Get-Date -Format s)) ===" | Out-File $report -Encoding utf8

$obsFwd = $ObsDir -replace '\\','/'

Write-Host '1/3 Configurando...' -ForegroundColor Cyan
cmd /c "cmake -S `"$root`" -B `"$build`" -G `"Visual Studio 18 2026`" -A x64 -DOBS_SOURCE_DIR=`"$obsFwd`" > `"$cfgLog`" 2>&1"
Add-Report "configure exit code: $LASTEXITCODE"
if ($LASTEXITCODE -ne 0) {
    Add-Report '--- log de configuracion (ultimas 40 lineas) ---'
    Add-Report (Get-Content $cfgLog -Tail 40)
    Write-Host "Fallo la configuracion. Sube $report a Claude." -ForegroundColor Red; exit 1
}

Write-Host '2/3 Compilando...' -ForegroundColor Cyan
cmd /c "cmake --build `"$build`" --config RelWithDebInfo > `"$bldLog`" 2>&1"
Add-Report "build exit code: $LASTEXITCODE"
if ($LASTEXITCODE -ne 0) {
    Add-Report '--- primeros errores ---'
    Add-Report (Select-String -Path $bldLog -Pattern 'error' -SimpleMatch | Select-Object -First 20 | ForEach-Object { $_.Line })
    Add-Report '--- ultimas 30 lineas ---'
    Add-Report (Get-Content $bldLog -Tail 30)
    Write-Host "Fallo la compilacion. Sube $report a Claude." -ForegroundColor Red; exit 1
}

$exe = Join-Path $build 'deploy\bin\64bit\ReKapture.exe'
if (-not (Test-Path $exe)) { Add-Report "No existe $exe"; Write-Host 'No se genero el ejecutable.' -ForegroundColor Red; exit 1 }

Write-Host '3/3 Abriendo ReKapture. Mira la ventana y CIERRALA cuando termines.' -ForegroundColor Cyan
$proc = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -Wait -PassThru
Add-Report "ReKapture se cerro. Codigo de salida: $($proc.ExitCode)"
if ($proc.ExitCode -ne 0) {
    Add-Report '--- eventos de error de Windows recientes (ReKapture) ---'
    $ev = Get-WinEvent -FilterHashtable @{LogName='Application'; ProviderName='Application Error'} -MaxEvents 10 -ErrorAction SilentlyContinue | Where-Object { $_.Message -like '*ReKapture*' } | Select-Object -First 2
    foreach ($e in $ev) { Add-Report $e.Message }
}

$log = Join-Path $env:LOCALAPPDATA 'ReKapture\logs\rekapture.log'
Add-Report "`n--- rekapture.log (ultimas 70 lineas) ---"
if (Test-Path $log) { Add-Report (Get-Content $log -Tail 70 -Encoding UTF8) } else { Add-Report "No existe $log" }

$rec = Join-Path ([Environment]::GetFolderPath('MyVideos')) 'ReKapture'
Add-Report "`n--- grabaciones en $rec (ultimas 5) ---"
if (Test-Path $rec) {
    $shell = New-Object -ComObject Shell.Application
    $folder = $shell.Namespace($rec)
    Get-ChildItem $rec -File | Sort-Object LastWriteTime -Descending | Select-Object -First 5 | ForEach-Object {
        $item = $folder.ParseName($_.Name)
        $dur = $folder.GetDetailsOf($item, 27)
        Add-Report ("{0}  {1:N1} MB  duracion={2}" -f $_.Name, ($_.Length/1MB), $dur)
    }
} else { Add-Report 'No existe la carpeta de grabaciones.' }

Add-Report "`n--- resumen de tiempos de grabacion (todo el log) ---"
if (Test-Path $log) {
    Add-Report (Select-String -Path $log -Encoding UTF8 -Pattern 'Modo de captura|Capturando juego|Se perdi|Todav|Iniciando grabaci|iniciada\.|Deteniendo|obs_output_stop|detenida|Resultado|Video reconfigurado|Bitrate objetivo|Atajo|combinaci' | Select-Object -Last 40 | ForEach-Object { $_.Line })
}

$core = Join-Path $build 'deploy\core'
Add-Report "`n--- DLL bajo deploy\core (primeras 40) ---"
if (Test-Path $core) {
    Add-Report (Get-ChildItem $core -Recurse -Filter *.dll | Select-Object -First 40 | ForEach-Object { $_.FullName.Substring($core.Length) })
}

Write-Host "`nTERMINADO. Sube a Claude: $report" -ForegroundColor Green
