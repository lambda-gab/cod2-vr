@echo off
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
call "%VSDIR%\VC\Auxiliary\Build\vcvarsamd64_x86.bat" >nul 2>nul
cd /d "%~dp0"
if not exist out mkdir out
cl /nologo /O2 /W3 /I..\..\deps\openxr\include overlaytest.c ..\overlay.c /Foout\ /Feout\overlaytest.exe /link d3d11.lib dxguid.lib user32.lib || exit /b 1
