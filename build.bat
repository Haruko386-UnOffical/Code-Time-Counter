@echo off
if not exist bin mkdir bin

echo Compiling Resources...
windres resources/tracker.rc -o bin/manifest.o

echo Compiling Source...
g++ src/main.cpp src/tracker.cpp bin/manifest.o ^
    -o bin/CodeTracker.exe ^
    -std=c++17 ^
    -mwindows ^
    -I./include ^
    -L./bin ^
    -lwebview -lWebView2Loader ^
    -lole32 -lshlwapi -lversion -luuid -loleaut32 ^
    -static-libgcc -static-libstdc++

echo Copying UI...
if not exist "bin\ui" mkdir "bin\ui"
copy ui\index.html bin\ui\index.html

echo Done!
pause