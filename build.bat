@echo off
rem Build autosnip.exe with the Microsoft C++ compiler.
rem Run this from a Developer Command Prompt for Visual Studio.
rem /MT links the C runtime statically, so the exe runs on its own.

cl /nologo /std:c++17 /O2 /W4 /EHsc /MT /DUNICODE /D_UNICODE autosnip.cpp ^
	user32.lib gdi32.lib ole32.lib dwmapi.lib windowscodecs.lib
