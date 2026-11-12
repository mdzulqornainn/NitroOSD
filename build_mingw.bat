@echo off
rem Butuh MinGW-w64 (g++, windres). Hasil: NitroOSD.exe
windres app.rc -O coff -o app.res || exit /b 1
g++ -std=c++17 -O2 -municode -mwindows main.cpp app.res -o NitroOSD.exe -lgdiplus -lgdi32 -luser32 -lsetupapi -lcfgmgr32 -lshell32 -ladvapi32 -lole32 -lshlwapi -luuid -loleaut32 -lwbemuuid
