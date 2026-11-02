@echo off
rem ReKapture: crea la carpeta portable (.zip) y el instalador (.exe) en la carpeta "dist".
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\package.ps1"
echo.
echo Pulsa una tecla para cerrar esta ventana.
pause >nul
