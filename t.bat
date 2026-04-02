@echo off
pushd "%~dp0" >nul || exit /b 1
call ".\scripts\run_tests.bat" %*
set "STATUS=%errorlevel%"
popd >nul
exit /b %STATUS%
