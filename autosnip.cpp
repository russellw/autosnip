// autosnip: take screenshots of the foreground window at regular intervals,
// skipping any that are almost identical to the previous one saved.
//
// Screenshots are saved as numbered PNG files (0000.png, 0001.png, ...) in
// the current directory. Numbering continues after the highest existing
// number, so earlier screenshots are never overwritten.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>
#include <wincodec.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <vector>

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

namespace {

// Command line options
double interval = 1.0;   // seconds between screenshots
double threshold = 99.9; // percentage of identical pixels at or above which a screenshot is skipped
int digits = 4;          // minimum number of digits in file names

IWICImagingFactory* wic;
HANDLE stopEvent;

struct Image {
	HWND window = nullptr;
	int width = 0;
	int height = 0;
	// Top-down rows of BGR pixels, one uint32_t per pixel, top byte zero
	std::vector<uint32_t> pixels;
};

void usage(FILE* f) {
	fputs("Usage: autosnip [options]\n"
		  "\n"
		  "Take screenshots of the foreground window at regular intervals, saving them\n"
		  "as numbered PNG files in the current directory. Press Ctrl+C to stop.\n"
		  "\n"
		  "Options:\n"
		  "  -i <seconds>  Interval between screenshots (default 1, fractions allowed)\n"
		  "  -t <percent>  Skip a screenshot if at least this percentage of its pixels\n"
		  "                are identical to the last one saved (default 99.9)\n"
		  "  -d <digits>   Minimum number of digits in file names (default 4)\n"
		  "  -h            Show this help\n",
		f);
}

[[noreturn]] void badArg(const char* message, const char* arg) {
	fprintf(stderr, "autosnip: %s: %s\n", message, arg);
	exit(1);
}

[[noreturn]] void fail(const char* what, HRESULT hr) {
	fprintf(stderr, "autosnip: %s failed (error 0x%08lx)\n", what, (unsigned long)hr);
	exit(1);
}

void check(HRESULT hr, const char* what) {
	if (FAILED(hr))
		fail(what, hr);
}

double parseNumber(const char* s) {
	char* end;
	double x = strtod(s, &end);
	if (end == s || *end)
		badArg("expected a number", s);
	return x;
}

void parseArgs(int argc, char** argv) {
	for (int i = 1; i < argc; i++) {
		const char* a = argv[i];
		if (!strcmp(a, "-h") || !strcmp(a, "--help") || !strcmp(a, "/?")) {
			usage(stdout);
			exit(0);
		}
		if (!strcmp(a, "-i") || !strcmp(a, "-t") || !strcmp(a, "-d")) {
			if (i + 1 == argc)
				badArg("missing value for option", a);
			const char* v = argv[++i];
			double x = parseNumber(v);
			switch (a[1]) {
			case 'i':
				if (!(x > 0))
					badArg("interval must be greater than zero", v);
				interval = x;
				break;
			case 't':
				if (!(0 <= x && x <= 100))
					badArg("threshold must be between 0 and 100", v);
				threshold = x;
				break;
			case 'd':
				if (!(1 <= x && x <= 18) || x != (int)x)
					badArg("digits must be a whole number between 1 and 18", v);
				digits = (int)x;
				break;
			}
			continue;
		}
		fprintf(stderr, "autosnip: unknown option: %s\n\n", a);
		usage(stderr);
		exit(1);
	}
}

// Without DPI awareness, Windows reports scaled-down coordinates on high-DPI
// screens and the screenshots come out blurry or cropped.
void setDpiAware() {
	using SetContext = BOOL(WINAPI*)(HANDLE);
	auto user32 = GetModuleHandleW(L"user32.dll");
	auto f = reinterpret_cast<SetContext>(reinterpret_cast<void*>(GetProcAddress(user32, "SetProcessDpiAwarenessContext")));
	// DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2, available from Windows 10 1703
	if (f && f(reinterpret_cast<HANDLE>(-4)))
		return;
	SetProcessDPIAware();
}

BOOL WINAPI ctrlHandler(DWORD type) {
	switch (type) {
	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
	case CTRL_CLOSE_EVENT:
		// Let the main loop finish any screenshot it is writing, then exit
		SetEvent(stopEvent);
		return TRUE;
	}
	return FALSE;
}

// The number to give the next screenshot: one more than the highest number
// among existing files in the current directory named like 0123.png.
long long nextNumber() {
	long long next = 0;
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW(L"*.png", &fd);
	if (h == INVALID_HANDLE_VALUE)
		return next;
	do {
		const wchar_t* name = fd.cFileName;
		size_t n = wcslen(name);
		// The wildcard can also match names that merely contain .png, via
		// their short 8.3 names, so check the extension explicitly
		if (n < 5 || _wcsicmp(name + n - 4, L".png"))
			continue;
		size_t stem = n - 4;
		if (stem > 18)
			continue;
		bool allDigits = true;
		for (size_t i = 0; i < stem; i++)
			if (!(L'0' <= name[i] && name[i] <= L'9'))
				allDigits = false;
		if (!allDigits)
			continue;
		next = std::max(next, wcstoll(name, nullptr, 10) + 1);
	} while (FindNextFileW(h, &fd));
	FindClose(h);
	return next;
}

// Capture the visible part of a window: the area inside the invisible resize
// border and drop shadow that Windows 10 and later draw around windows.
bool capture(HWND hwnd, Image& image) {
	if (IsIconic(hwnd) || !IsWindowVisible(hwnd))
		return false;

	RECT whole;
	if (!GetWindowRect(hwnd, &whole))
		return false;
	RECT visible;
	if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &visible, sizeof visible)))
		visible = whole;
	visible.left = std::max(visible.left, whole.left);
	visible.top = std::max(visible.top, whole.top);
	visible.right = std::min(visible.right, whole.right);
	visible.bottom = std::min(visible.bottom, whole.bottom);

	int wholeWidth = whole.right - whole.left;
	int wholeHeight = whole.bottom - whole.top;
	int width = visible.right - visible.left;
	int height = visible.bottom - visible.top;
	if (width <= 0 || height <= 0)
		return false;

	HDC screen = GetDC(nullptr);
	HDC mem = CreateCompatibleDC(screen);
	BITMAPINFO bi{};
	bi.bmiHeader.biSize = sizeof bi.bmiHeader;
	bi.bmiHeader.biWidth = wholeWidth;
	bi.bmiHeader.biHeight = -wholeHeight; // negative means top-down rows
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	void* bits = nullptr;
	HBITMAP bitmap = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
	bool ok = false;
	if (bitmap) {
		HGDIOBJ old = SelectObject(mem, bitmap);
		// PW_RENDERFULLCONTENT makes this work for windows drawn with the GPU,
		// such as web browsers, and unlike copying from the screen, it is not
		// affected by other windows on top
		if (PrintWindow(hwnd, mem, PW_RENDERFULLCONTENT)) {
			GdiFlush();
			image.width = width;
			image.height = height;
			image.pixels.resize((size_t)width * height);
			auto src = static_cast<const uint32_t*>(bits);
			int dx = visible.left - whole.left;
			int dy = visible.top - whole.top;
			for (int y = 0; y < height; y++) {
				const uint32_t* row = src + (size_t)(y + dy) * wholeWidth + dx;
				uint32_t* out = image.pixels.data() + (size_t)y * width;
				// The alpha byte is undefined after PrintWindow
				for (int x = 0; x < width; x++)
					out[x] = row[x] & 0xffffff;
			}
			ok = true;
		}
		SelectObject(mem, old);
		DeleteObject(bitmap);
	}
	DeleteDC(mem);
	ReleaseDC(nullptr, screen);
	return ok;
}

// Whether a new screenshot is close enough to the last one saved to skip.
// A different window, or the same window at a different size, always counts
// as a change.
bool similar(const Image& a, const Image& b) {
	if (a.window != b.window || a.width != b.width || a.height != b.height)
		return false;
	size_t n = a.pixels.size();
	size_t same = 0;
	for (size_t i = 0; i < n; i++)
		same += a.pixels[i] == b.pixels[i];
	return (double)same * 100 >= threshold * (double)n;
}

void savePng(const Image& image, const wchar_t* path) {
	// Convert to 24-bit BGR, which the PNG encoder accepts directly
	UINT stride = (image.width * 3 + 3) & ~3u;
	std::vector<BYTE> data((size_t)stride * image.height);
	for (int y = 0; y < image.height; y++) {
		const uint32_t* in = image.pixels.data() + (size_t)y * image.width;
		BYTE* out = data.data() + (size_t)y * stride;
		for (int x = 0; x < image.width; x++) {
			uint32_t p = in[x];
			*out++ = (BYTE)p;
			*out++ = (BYTE)(p >> 8);
			*out++ = (BYTE)(p >> 16);
		}
	}

	IWICStream* stream;
	check(wic->CreateStream(&stream), "CreateStream");
	check(stream->InitializeFromFilename(path, GENERIC_WRITE), "creating file");
	IWICBitmapEncoder* encoder;
	check(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder), "CreateEncoder");
	check(encoder->Initialize(stream, WICBitmapEncoderNoCache), "IWICBitmapEncoder::Initialize");
	IWICBitmapFrameEncode* frame;
	check(encoder->CreateNewFrame(&frame, nullptr), "CreateNewFrame");
	check(frame->Initialize(nullptr), "IWICBitmapFrameEncode::Initialize");
	check(frame->SetSize(image.width, image.height), "SetSize");
	WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
	check(frame->SetPixelFormat(&format), "SetPixelFormat");
	if (format != GUID_WICPixelFormat24bppBGR)
		fail("PNG encoder pixel format negotiation", E_FAIL);
	check(frame->WritePixels(image.height, stride, (UINT)data.size(), data.data()), "WritePixels");
	check(frame->Commit(), "committing frame");
	check(encoder->Commit(), "writing file");
	// Release explicitly so the file is closed now rather than at exit
	frame->Release();
	encoder->Release();
	stream->Release();
}

} // namespace

int main(int argc, char** argv) {
	parseArgs(argc, argv);
	setDpiAware();
	check(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), "CoInitializeEx");
	check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)),
		"creating WIC imaging factory");

	stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	SetConsoleCtrlHandler(ctrlHandler, TRUE);

	// Don't take screenshots of the terminal autosnip was started from. With
	// the classic console, that is the console window; with Windows Terminal,
	// the console window is hidden, but the terminal is in the foreground at
	// startup.
	HWND console = GetConsoleWindow();
	HWND startWindow = GetForegroundWindow();

	long long number = nextNumber();
	wchar_t name[64];
	swprintf(name, sizeof name / sizeof *name, L"%0*lld.png", digits, number);
	printf("autosnip: every %g seconds, starting at %ls. Press Ctrl+C to stop.\n", interval, name);

	Image last;
	bool haveLast = false;
	auto step = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(interval));
	auto next = std::chrono::steady_clock::now();
	for (;;) {
		HWND hwnd = GetForegroundWindow();
		if (hwnd && hwnd != console && hwnd != startWindow) {
			Image image;
			image.window = hwnd;
			if (capture(hwnd, image) && !(haveLast && similar(image, last))) {
				swprintf(name, sizeof name / sizeof *name, L"%0*lld.png", digits, number++);
				savePng(image, name);
				printf("%ls\n", name);
				fflush(stdout);
				last = std::move(image);
				haveLast = true;
			}
		}

		// Keep to a steady schedule, but if a screenshot took longer than the
		// interval, skip the missed times rather than trying to catch up
		next += step;
		auto now = std::chrono::steady_clock::now();
		if (next < now)
			next = now;
		auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(next - now).count();
		if (WaitForSingleObject(stopEvent, (DWORD)wait) == WAIT_OBJECT_0)
			break;
	}
	return 0;
}
