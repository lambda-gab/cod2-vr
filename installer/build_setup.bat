@echo off
setlocal
rem Builds CoD2-VR-Setup.exe. Run ..\proxy\build.bat and ..\launcher\build_launcher.bat first (the setup embeds their output).
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR ( echo Visual Studio with C++ tools not found & exit /b 1 )
call "%VSDIR%\VC\Auxiliary\Build\vcvarsamd64_x86.bat" >nul 2>nul
cd /d "%~dp0"
if not exist ..\proxy\out\d3d9.dll ( echo Build the proxy first: ..\proxy\build.bat & exit /b 1 )
if not exist "..\launcher\out\Call of Duty 2 VR.exe" ( echo Build the launcher first: ..\launcher\build_launcher.bat & exit /b 1 )
if not exist out mkdir out
rc /nologo /fo out\setup.res setup.rc || exit /b 1
cl /nologo /O2 /MT /W3 setup.c out\setup.res /Foout\ /Feout\CoD2-VR-Setup.exe /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTUAC:NO /MANIFESTINPUT:setup.manifest user32.lib shell32.lib ole32.lib advapi32.lib uuid.lib gdi32.lib || exit /b 1
del /q out\*.obj 2>nul
