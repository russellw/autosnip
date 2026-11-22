# autosnip

autosnip is a Windows command line program that takes a screenshot of the foreground window once per second and saves it as a PNG file.

It is meant for testing GUI and web applications. When you are trying to capture screenshots that show a bug, you don't want to divide your attention between the testing and working the Snipping Tool. Start autosnip, do your testing, then look through the screenshots afterwards for the ones you need.

To keep the number of files manageable, a screenshot is skipped if it is almost identical to the last one saved, so leaving autosnip running while nothing happens on screen doesn't fill the directory with duplicates.

## Requirements

Windows 10 or later. autosnip is a single executable with no dependencies beyond the DLLs that come with Windows.

## Usage

Open a terminal in the directory where you want the screenshots to go, and run:

```
autosnip
```

Then switch to the application you are testing. Press Ctrl+C in the terminal to stop.

```
autosnip [-i seconds] [-t percent] [-d digits]
```

| Option | Meaning | Default |
| --- | --- | --- |
| `-i <seconds>` | Interval between screenshots. Fractions such as `0.5` are allowed. | 1 |
| `-t <percent>` | Skip a screenshot if at least this percentage of its pixels are identical to the last one saved. | 99.9 |
| `-d <digits>` | Minimum number of digits in file names. | 4 |
| `-h` | Show help. | |

### Examples

Take a screenshot every half second:

```
autosnip -i 0.5
```

Save every screenshot unless it is exactly the same as the last one saved:

```
autosnip -t 100
```

## How it works

### What is captured

Each screenshot is of the window in the foreground at that moment, including its title bar but not the invisible resize border and drop shadow that Windows draws around it. Windows on top of it don't get in the way, and windows drawn with the GPU, such as web browsers, are captured correctly. autosnip is DPI aware, so screenshots on high-DPI displays are at full resolution.

Nothing is captured while the foreground window is minimized, or while the terminal autosnip was started from is in the foreground. That is the window in the foreground when autosnip starts, so to take screenshots of a terminal, start autosnip from a different one.

### Change detection

Each new screenshot is compared pixel by pixel with the last one saved, not with the last one taken, so slow gradual changes eventually get saved. If at least the threshold percentage of pixels are identical, the screenshot is skipped. Switching to a different window, or resizing the window, always counts as a change.

The default of 99.9% skips screenshots where only something tiny has changed, such as a blinking text cursor. Lower it to skip more, or raise it to 100 to keep everything except exact duplicates.

### File names

Screenshots are saved in the current directory as `0000.png`, `0001.png` and so on, and each file name is printed as it is saved. If numbered files are already there, numbering carries on after the highest one, so earlier screenshots are never overwritten. When the numbers run out of digits, the names simply get longer: `9999.png` is followed by `10000.png`.

If autosnip falls behind, for example when saving a large window at a short interval, it skips the missed screenshots rather than trying to catch up.

## Building

With Visual Studio, open a Developer Command Prompt in the source directory and run:

```
build.bat
```

With MinGW-w64, either on Windows or cross-compiling from Linux, run:

```
g++ -std=c++17 -O2 -Wall -static -o autosnip.exe autosnip.cpp -luser32 -lgdi32 -lole32 -ldwmapi -lwindowscodecs
```

When cross-compiling, the compiler is usually called `x86_64-w64-mingw32-g++`.

## License

MIT. See [LICENSE](LICENSE).
