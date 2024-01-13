@echo off

"C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/Tools/VsDevCmd.bat" && ^
cl /nologo /EHsc /GA /Zo- /I include /Fe: compact-weather.exe main.cc user32.lib gdi32.lib