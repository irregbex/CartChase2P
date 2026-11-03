@echo off
rem ============================================================
rem  Cart Chase 2P - UNICO .BAT DEL PROYECTO (doble clic)
rem  Menu:
rem    1 Jugar (igual que el acceso directo "Cart Chase 2P"): el juego
rem      trae su propio menu (jugar, etapas al azar, volumen, muerte
rem      subita, salir).
rem    2 Editor de etapas (se abre en el navegador; cerrar la ventana
rem      negra del editor para terminarlo)
rem    3 Crear etapas al azar (recetas + menu)
rem    4 Compilar las recetas (despues de editar un .json a mano)
rem    5 Compilar el juego (Release)
rem    6 Preparar desde la ROM (estados, pieles, etapas y datos del editor
rem      que salen del juego; solo crea lo que falta)
rem  Sin menu (VS Code, consola):
rem    JUGAR.bat jugar
rem    JUGAR.bat preparar
rem    JUGAR.bat compilar [release^|debug] [regen] [test] [nopause]
rem  Registro de la partida: cart2p\logs\ultima_partida.txt
rem  F10 en el juego intercambia los mandos de P1 y P2.
rem ============================================================
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0.."
set "ROOT=%CD%"
if /i "%~1"=="compilar" goto :compilar
if /i "%~1"=="jugar" goto :jugar
if /i "%~1"=="preparar" goto :preparar

:menu
color 0A
title Cart Chase 2P
cls
echo.
echo   CART CHASE 2P
echo   ------------------------------------------
echo   1  Jugar
echo   2  Editor de etapas (navegador)
echo   3  Crear etapas al azar
echo   4  Compilar las recetas
echo   5  Compilar el juego (Release)
echo   6  Preparar desde la ROM
echo   0  Salir
echo.
set "OP="
set /p "OP=  Opcion: "
if "%OP%"=="1" goto :jugar
if "%OP%"=="2" goto :editor
if "%OP%"=="3" goto :azar
if "%OP%"=="4" goto :recetas
if "%OP%"=="5" goto :compilar
if "%OP%"=="6" goto :preparar
if "%OP%"=="0" exit /b 0
goto :menu

rem ------------------------------------------------------------ jugar
:jugar
rem El juego se configura solo con --cart2p (lista, records, mandos,
rem opciones en cart2p\opciones.txt, registro en cart2p\logs\). Es lo mismo
rem que el acceso directo "Cart Chase 2P" de la raiz del workspace.
set "EXE=build-windows\release\DKC1Recomp.exe"
if not exist "%EXE%" (
  echo ERROR: falta %EXE%. Usar la opcion 5 Compilar el juego.
  pause & goto :menu
)
if not exist "cart2p\states\carnage_montado.state" (
  echo Falta preparar desde la ROM: se hace ahora ^(una sola vez^).
  call :preparar_sub
  if errorlevel 1 ( pause & goto :menu )
)
start "" "%EXE%" --cart2p
exit /b 0

rem ------------------------------------------------------------ preparar
:preparar
call :preparar_sub
pause
if /i "%~1"=="preparar" exit /b %errorlevel%
goto :menu

:preparar_sub
python tools\cart2p\preparar_desde_rom.py
if errorlevel 1 ( echo ERROR: no se pudo preparar desde la ROM. & exit /b 1 )
exit /b 0

rem ------------------------------------------------------------ azar
:azar
echo.
set "N="
set /p "N=  Cuantas etapas nuevas (Enter = 6): "
if "%N%"=="" set "N=6"
python tools\cart2p\azar_etapas.py --cantidad %N%
if errorlevel 1 echo ATENCION: no se generaron todas las etapas pedidas.
echo.
pause
goto :menu

rem ------------------------------------------------------------ editor
:editor
echo.
echo  Abriendo el editor de etapas en el navegador...
echo  Esta ventana es el servidor del editor: cerrarla al terminar de editar.
python tools\cart2p\editor_server.py
pause
goto :menu

rem ------------------------------------------------------------ recetas
:recetas
echo.
python tools\cart2p\compilar_etapas.py
echo.
pause
goto :menu

rem ------------------------------------------------------------ compilar
:compilar
color 5F
title Cart Chase 2P - COMPILAR
set "ROM=%ROOT%\rom\dkc1.sfc"
set "CFG=Release"
set "BDIR=build-windows\release"
set "REGEN=0"
set "TEST=0"
set "NOPAUSE=0"
set "FROMMENU=1"
if /i "%~1"=="compilar" set "FROMMENU=0"
for %%a in (%*) do (
  if /i "%%a"=="debug"   ( set "CFG=RelWithDebInfo" & set "BDIR=build-windows\relwithdebinfo" )
  if /i "%%a"=="release" ( set "CFG=Release" & set "BDIR=build-windows\release" )
  if /i "%%a"=="regen"   set "REGEN=1"
  if /i "%%a"=="test"    set "TEST=1"
  if /i "%%a"=="nopause" set "NOPAUSE=1"
)
set "BRC=0"
echo === Cart Chase 2P - Compilar %CFG% ===

where cl >nul 2>&1 && goto :c_haveenv
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSPATH="
if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH (
  echo ERROR: no se encontro MSVC x64 - Visual Studio Build Tools.
  set "BRC=1" & goto :c_fin
)
call "%VSPATH%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -no_logo
:c_haveenv
where cl >nul 2>&1 || ( echo ERROR: cl.exe no disponible & set "BRC=1" & goto :c_fin )
where cmake >nul 2>&1 || ( echo ERROR: cmake no disponible & set "BRC=1" & goto :c_fin )
where ninja >nul 2>&1 || ( echo ERROR: ninja no disponible & set "BRC=1" & goto :c_fin )

rem Motor snesrecomp (licencia PolyForm Noncommercial, se baja aparte).
if not exist "%ROOT%\snesrecomp\runner\src\cpu_state.c" (
  echo [motor] bajando snesrecomp ^(commit 75223f3^)...
  git clone https://github.com/elliotttate/snesrecomp.git "%ROOT%\snesrecomp" || ( echo ERROR: no se pudo bajar snesrecomp & set "BRC=1" & goto :c_fin )
  git -C "%ROOT%\snesrecomp" checkout -q 75223f3 || ( echo ERROR: no esta el commit 75223f3 & set "BRC=1" & goto :c_fin )
)
rem La ROM se verifica siempre por SHA-256 antes de generar o preparar.
python tools\cart2p\verificar_rom.py || ( set "BRC=1" & goto :c_fin )

set "NEEDGEN=%REGEN%"
if not exist "%ROOT%\generated\snesrecomp\" set "NEEDGEN=1"
if "%NEEDGEN%"=="1" (
  if not exist "%ROM%" ( echo ERROR: falta rom\dkc1.sfc & set "BRC=1" & goto :c_fin )
  echo [1/3] Generando generated\snesrecomp desde la ROM...
  python scripts\generate_snesrecomp.py --rom "%ROM%" --analysis-backend python
  if errorlevel 1 ( echo ERROR en generacion & set "BRC=1" & goto :c_fin )
) else (
  echo [1/3] generated\snesrecomp ya existe; usar "regen" para forzar
)

rem Si la carpeta del proyecto se movio, los caches de CMake (tambien los de
rem _deps) apuntan a la ruta vieja y el configure falla: la carpeta de build
rem se aparta con otro nombre (no se borra) y se compila desde cero.
set "ROOTF=%ROOT:\=/%"
if exist "%BDIR%\CMakeCache.txt" (
  findstr /i /c:"CMAKE_HOME_DIRECTORY:INTERNAL=%ROOTF%" "%BDIR%\CMakeCache.txt" >nul
  if errorlevel 1 (
    for %%d in ("%BDIR%") do set "BAK=%%~nxd_ruta_anterior_!RANDOM!"
    echo La carpeta del proyecto cambio de lugar: build anterior apartado como !BAK!
    ren "%BDIR%" "!BAK!"
    if exist "%BDIR%\" ( echo ERROR: no se pudo apartar %BDIR% & set "BRC=1" & goto :c_fin )
  )
)
echo [2/3] CMake configure %BDIR%
cmake -S . -B "%BDIR%" -G Ninja -DCMAKE_BUILD_TYPE=%CFG% -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DSNESRECOMP_SDL_BACKEND=SDL2 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_POLICY_VERSION_MINIMUM=3.5
if errorlevel 1 ( echo ERROR en CMake configure & set "BRC=1" & goto :c_fin )
echo [3/3] Compilando...
cmake --build "%BDIR%" --parallel
if errorlevel 1 ( echo ERROR de compilacion & set "BRC=1" & goto :c_fin )
rem Un enlace fallido borra el .exe aunque cmake devuelva 0: verificar salida.
if not exist "%BDIR%\DKC1Recomp.exe" ( echo ERROR: no se genero DKC1Recomp.exe & set "BRC=1" & goto :c_fin )
if not exist "%BDIR%\dkc1_snesrecomp_headless.exe" ( echo ERROR: no se genero dkc1_snesrecomp_headless.exe & set "BRC=1" & goto :c_fin )
copy /y "%BDIR%\compile_commands.json" "%ROOT%\build-windows\compile_commands.json" >nul 2>&1

if "%TEST%"=="1" (
  echo [test] ctest
  ctest --test-dir "%BDIR%" --output-on-failure
  if errorlevel 1 set "BRC=1"
  echo [test] unittest Python
  python -m unittest discover -s tests
  if errorlevel 1 set "BRC=1"
)

echo.
echo === BUILD OK: %BDIR%\DKC1Recomp.exe  ^(SDL2.dll al lado^) ===
if /i "%BDIR%"=="build-windows\release" (
  echo [preparar] estados, pieles y etapas desde la ROM ^(solo lo que falta^)
  call :preparar_sub
  if errorlevel 1 set "BRC=1"
)

:c_fin
if not "%BRC%"=="0" echo === BUILD CON ERRORES ===
if "%NOPAUSE%"=="0" pause
if "%FROMMENU%"=="1" goto :menu
exit /b %BRC%
