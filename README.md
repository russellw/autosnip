# autosnip

Takes a screenshot of the foreground window once per second while you test a GUI or web application, so you can concentrate on the testing instead of on working the Snipping Tool. Screenshots that are almost identical to the last one saved are skipped.

Screenshots are saved in the current directory as `0000.png`, `0001.png` and so on. If numbered files are already there, numbering carries on after the highest one, so nothing is overwritten. Windows 10 or later is required.

## Usage

```
autosnip [-i seconds] [-t percent] [-d digits]
```

- `-i <seconds>`: interval between screenshots (default 1; fractions such as 0.5 are allowed).
- `-t <percent>`: skip a screenshot if at least this percentage of its pixels are identical to the last one saved (default 99.9). A different window, or a change in window size, always counts as a change.
- `-d <digits>`: minimum number of digits in file names (default 4).
- `-h`: show help.

Press Ctrl+C to stop. The terminal autosnip was started from is never captured, and nothing is captured while the foreground window is minimized.

## Building

From a Developer Command Prompt for Visual Studio, run `build.bat`. With MinGW-w64, run:

```
g++ -std=c++17 -O2 -Wall -static -o autosnip.exe autosnip.cpp -luser32 -lgdi32 -lole32 -ldwmapi -lwindowscodecs
```
