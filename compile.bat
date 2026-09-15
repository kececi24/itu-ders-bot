@echo off
set /a rel=0

if %rel% EQU 0 (
    g++ -Wall -Wpedantic -O3 src/main.cpp src/clock.cpp src/token.cpp -I include -o itu_picker.exe -lwinhttp -lwinmm
) else (
    g++ -O3 src/main.cpp src/clock.cpp src/token.cpp -I include -o itu_picker.exe -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic -lwinhttp -lwinmm
)
