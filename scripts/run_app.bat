@echo off
setlocal EnableDelayedExpansion

set "MODE=debug"
set "FORCE_RECONFIGURE=0"
set "USE_CONSOLE=0"
set "APP_ARGS="

:parse_args
if "%~1"=="" goto :args_done
if /i "%~1"=="debug" (
    set "MODE=debug"
    shift
    goto :parse_args
)
if /i "%~1"=="release" (
    set "MODE=release"
    shift
    goto :parse_args
)
if /i "%~1"=="--reconfigure" (
    set "FORCE_RECONFIGURE=1"
    shift
    goto :parse_args
)
if /i "%~1"=="--console" (
    set "USE_CONSOLE=1"
)
set "APP_ARGS=!APP_ARGS! "%~1""
shift
goto :parse_args

:args_done
set "CONFIG=Debug"
set "PRESET=default"
if /i "%MODE%"=="release" (
    set "CONFIG=Release"
    set "PRESET=release"
)

for %%I in ("%~dp0.") do set "SCRIPT_DIR=%%~fI"
if exist "%SCRIPT_DIR%\CMakePresets.json" (
    set "ROOT=%SCRIPT_DIR%"
) else (
    for %%I in ("%SCRIPT_DIR%\..") do set "ROOT=%%~fI"
)
pushd "%ROOT%" >nul || exit /b 1

set "CACHE_FILE=build\CMakeCache.txt"
set "EXE=build\%CONFIG%\spectre.exe"

echo.
echo === %CONFIG% ===
if "%FORCE_RECONFIGURE%"=="1" (
    call :run cmake --preset %PRESET% || goto :fail
) else if not exist "%CACHE_FILE%" (
    call :run cmake --preset %PRESET% || goto :fail
) else (
    echo.
    echo ^> using existing CMake cache: %CACHE_FILE%
)

call :run cmake --build build --config %CONFIG% --parallel || goto :fail

if not exist "%EXE%" (
    echo.
    echo Missing executable: %EXE%
    goto :fail
)

echo.
echo ^> %EXE%%APP_ARGS%
if "%USE_CONSOLE%"=="1" (
    call "%EXE%"%APP_ARGS%
) else (
    start "" /wait "%EXE%"%APP_ARGS%
)
set "STATUS=%errorlevel%"
popd >nul
exit /b %STATUS%

:run
echo.
echo ^> %*
%*
exit /b %errorlevel%

:fail
set "STATUS=%errorlevel%"
if "%STATUS%"=="" set "STATUS=1"
popd >nul
exit /b %STATUS%
