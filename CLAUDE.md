Autosnip is a command line program that takes screenshots automatically once per second. Windows Snipping Tool can do this manually, but when you are testing a GUI or web application, trying to get screenshots that illustrate a bug, you don't want to be dividing your attention between the tests and manually activating the snipping tool.

It is written in C++ as it is mainly a wrapper around the Windows API. And it doesn't need to handle untrusted data. Nor does it need to worry about the lifetimes of data structures, as everything can be automatically cleaned up when the program exits.
The frequency of once per second can be overridden by a command line parameter.
The screenshots are of the foreground window.
They are subject to change detection. That is, if the next screenshot would be almost identical to the previous one, skip it. The threshold of identical pixels can be configured by a command line parameter.
The program is Windows specific.

The screenshots are saved in PNG format.
They are saved in the current directory.
The file names are numbered in ascending order with leading zeros like 0000.png, 0001.png
The number of digits defaults to 4 but can be overridden by a command line parameter.

Always use US spellings in code and documentation, unless conforming to some existing API requires the use of UK spellings.
All commits should go to the main branch.
Use Unix (LF) line endings in all files, including Windows-specific ones such as .bat files, and keep them LF when checked out on Windows.
