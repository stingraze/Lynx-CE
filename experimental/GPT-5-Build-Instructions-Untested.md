Absolutely—here’s a clean, reproducible way to build what’s in the **experimental** folder of your repo for Windows CE using the CeGCC toolchain.

> Notes I confirmed from your repo/docs: the latest sources are under `experimental/`, the project is compiled with **CeGCC**, and the code uses Winsock (there’s a local `winsock2.h` and a comment about linking). ([GitHub][1]) ([GitHub][2])

# 1) Prereqs (host: Linux, macOS(+WSL), or Windows+MSYS2/Cygwin)

* Install the CeGCC cross toolchain so you have one (or both) of these compilers in PATH:

  * `arm-wince-cegcc-gcc` (links against `cegcc.dll`)
  * `arm-wince-mingw32ce-gcc` (classic mingw32ce flavor)

  CeGCC docs show usage and the simple compile flow; they also note you need the CeGCC runtime DLLs on the device to run binaries. ([cegcc.sourceforge.net][3])

* Why the flags matter:

  * `-mwin32` sets Win32 macros and disables Unix ones under CeGCC. ([cegcc.sourceforge.net][4])
  * Winsock on these toolchains may need **either** `-lws2` (mingw32ce historical) **or** `-lws2_32` (modern MinGW naming). I include both fallbacks below because different CeGCC builds expect different names. ([mail.gnu.org][5], [Medium][6], [Microsoft Learn][7])

# 2) Get the sources

```bash
git clone https://github.com/stingraze/Lynx-CE
cd Lynx-CE/experimental
```

(Your README says the latest source is in `experimental/`.) ([GitHub][1])

# 3) One-liner build (auto-detects toolchain & the right Winsock lib)

Paste this in `experimental/`:

```bash
# pick the available cross-compiler
if command -v arm-wince-cegcc-gcc >/dev/null 2>&1; then
  CC=arm-wince-cegcc-gcc
elif command -v arm-wince-mingw32ce-gcc >/dev/null 2>&1; then
  CC=arm-wince-mingw32ce-gcc
else
  echo "No CeGCC cross-compiler found (arm-wince-*-gcc). Install CeGCC first."; exit 1
fi

# build everything in experimental into a single exe
set -x
$CC -mwin32 -Os -I. -o browser-lynx.exe *.c \
  || $CC -mwin32 -Os -I. -o browser-lynx.exe *.c -lws2 \
  || $CC -mwin32 -Os -I. -o browser-lynx.exe *.c -lws2_32
```

Why `-I.` and the Winsock libs? Your source (`browser.c`) includes a **local** `winsock2.h` and mentions that sometimes you must explicitly add `-lws2`/`-lws2_32` if auto-linking doesn’t happen. The commands above try “no explicit lib”, then `-lws2`, then `-lws2_32`. ([GitHub][2])

# 4) Optional: a tiny Makefile to keep things tidy

Create `Makefile` in `experimental/`:

```make
# Minimal Makefile for Lynx-CE experimental
# Usage: make [CC=arm-wince-cegcc-gcc] [WSL=auto] [REL=1]

SRC := $(wildcard *.c)
APP := browser-lynx.exe

# pick default compiler if not provided
CC  ?= $(shell command -v arm-wince-cegcc-gcc 2>/dev/null || command -v arm-wince-mingw32ce-gcc)
CFLAGS := -mwin32 -I. -Os
LDFLAGS :=

# Allow REL=1 for a smaller, stripped build
ifeq ($(REL),1)
  CFLAGS += -DNDEBUG
endif

# WSL picks winsock library:
#  - auto: try none, then ws2, then ws2_32 (handles different CeGCC builds)
#  - ws2: force -lws2
#  - ws2_32: force -lws2_32
WSL ?= auto

all: $(APP)

$(APP): $(SRC)
ifeq ($(WSL),ws2)
	$(CC) $(CFLAGS) -o $@ $^ -lws2
else ifeq ($(WSL),ws2_32)
	$(CC) $(CFLAGS) -o $@ $^ -lws2_32
else
	-$(CC) $(CFLAGS) -o $@ $^
	@if [ $$? -ne 0 ]; then $(CC) $(CFLAGS) -o $@ $^ -lws2 || $(CC) $(CFLAGS) -o $@ $^ -lws2_32; fi
endif

strip:
	-which arm-wince-cegcc-strip >/dev/null 2>&1 && arm-wince-cegcc-strip $(APP) || true
	-which arm-wince-mingw32ce-strip >/dev/null 2>&1 && arm-wince-mingw32ce-strip $(APP) || true

clean:
	rm -f $(APP) *.o
```

Examples:

```bash
make                   # auto-detect toolchain & winsock lib
make WSL=ws2           # force -lws2
make WSL=ws2_32        # force -lws2_32
make REL=1 strip       # smaller exe
```

# 5) Deploy & run on your device (e.g., Sigmarion 3)

1. Copy `browser-lynx.exe` to the device (SD card/ActiveSync).
2. If the device hasn’t got them yet, install the **CeGCC runtime DLL(s)** (e.g. `cegcc.dll`) that your toolchain expects—CeGCC docs explicitly say the DLLs must be present on the PDA. ([cegcc.sourceforge.net][3])
3. Launch from `cmd` or a file manager. The program opens a minimal text UI and uses Winsock (`WSAStartup`, `socket`, etc.) to fetch pages; your current `browser.c` shows that, and it strips HTML tags naïvely for display. ([GitHub][2])

# 6) Troubleshooting checklist

* **“Not a valid application” on device**
  Architecture/subsystem mismatch or missing CeGCC runtime. Ensure you built with `arm-wince-*` and copied the CeGCC DLLs as noted in CeGCC docs. ([cegcc.sourceforge.net][3])

* **`undefined reference` to Winsock / `WSAStartup` / `socket`**
  Rebuild forcing the right lib: `make WSL=ws2` (mingw32ce often uses `-lws2`) or `make WSL=ws2_32` (modern MinGW naming). See historical mingw32ce thread and modern MinGW guidance. ([mail.gnu.org][5], [Medium][6])

* **No DNS / connection fails**
  Confirm CE network stack is up (try IE or ping tool). Your code uses `gethostbyname`→`connect` on port 80. Some CE images require proxy config or only support HTTP (no TLS). ([GitHub][2])

* **Console I/O quirks**
  On some CE devices, `stdin`/`fgets` behavior from `cmd` can be odd. If input echoing behaves strangely, try running from a different console app, or adapt the code to read from a simple edit control. (Your current loop reads with `fgets()` and uses `getchar()` for menu input.) ([GitHub][2])

* **Stripping to reduce size**
  Use `arm-wince-*-strip` after building: `make strip`.

---

If you want, I can also drop a tiny **Dockerfile** that installs CeGCC on Ubuntu and runs `make` so you get a reproducible cross-build in one go.

[1]: https://github.com/stingraze/Lynx-CE "GitHub - stingraze/Lynx-CE: Just a minimal text browser inspired from Lynx for Windows CE"
[2]: https://github.com/stingraze/Lynx-CE/blob/main/browser.c "Lynx-CE/browser.c at main · stingraze/Lynx-CE · GitHub"
[3]: https://cegcc.sourceforge.net/docs/using.html?utm_source=chatgpt.com "Using CeGCC to compile your Windows CE application"
[4]: https://cegcc.sourceforge.net/docs/details.html?utm_source=chatgpt.com "Details - macros, libraries, .. - CeGCC - SourceForge"
[5]: https://mail.gnu.org/archive/html/libtool/2008-09/msg00013.html "problem when cross compiling with mingw32ce"
[6]: https://medium.com/%40lakpahana/solved-undefined-reference-to-imp-winsock-linker-issues-9584054e1fbe?utm_source=chatgpt.com "[Solved] undefined reference to - WinSock linker issues"
[7]: https://learn.microsoft.com/en-us/windows/win32/winsock/creating-a-basic-winsock-application?utm_source=chatgpt.com "Creating a Basic Winsock Application - Win32 apps"
