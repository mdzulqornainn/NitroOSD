@echo off
rem Jalankan dari "x64 Native Tools Command Prompt for VS". Hasil: NitroOSD.exe
rc /nologo app.rc || exit /b 1
cl /nologo /std:c++17 /O2 /EHsc /DUNICODE /D_UNICODE main.cpp app.res /Fe:NitroOSD.exe /link /SUBSYSTEM:WINDOWS gdiplus.lib gdi32.lib user32.lib setupapi.lib cfgmgr32.lib shell32.lib advapi32.lib ole32.lib shlwapi.lib uuid.lib oleaut32.lib wbemuuid.lib
