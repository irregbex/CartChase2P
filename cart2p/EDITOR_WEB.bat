@echo off
rem ============================================================
rem  Cart Chase 2P - EDITOR DE ETAPAS EN EL NAVEGADOR (doble clic)
rem  Levanta el servidor (tools\cart2p\editor_server.py) y abre
rem  http://127.0.0.1:8765/ en el navegador predeterminado.
rem  Esta ventana ES el servidor: cerrarla al terminar de editar.
rem  Si el servidor ya estaba corriendo, solo abre el navegador.
rem ============================================================
setlocal EnableExtensions
color 0B
title Cart Chase 2P - EDITOR WEB (cerrar al terminar)
cd /d "%~dp0.."
set "URL=http://127.0.0.1:8765/"

where python >nul 2>nul
if errorlevel 1 (
  echo ERROR: no se encontro python en el PATH.
  pause
  exit /b 1
)

echo.
echo  Editor de etapas: %URL%
echo  Esta ventana es el servidor: cerrarla al terminar de editar.
echo.
python tools\cart2p\editor_server.py
if errorlevel 1 (
  echo.
  echo  El servidor no pudo arrancar ^(probablemente ya estaba abierto en
  echo  otra ventana^). Se abre el navegador en %URL%
  start "" "%URL%"
  pause
)
exit /b 0
