# Lynx-CE
Latest source code version is: 0.01.

Just a minimal text browser inspired from Lynx for Windows CE

9/8/2025 17:03PM JST: There is now the build instructions for experimental (PolarSSL) version. It is untested. Please use it as a reference or a stepping stone.

12/30/2024 18:57PM JST: Now supports following links. Testing form text input. The latest source code is in experimental folder.

Used ChatGPT - o1 to help me make this project work.

This project was successfully compiled using CeGCC.

## Image pipeline flags (WinCE 3.0 low-end profile)

New command-line flags configure optional image downscaling in the lightweight image pipeline:

- `--small-images`
- `--image-scale=<n>` where `<n>` is `1-100`
- `--max-img-width=<n>` where `<n>` is integer pixels
- `--max-img-height=<n>` where `<n>` is integer pixels

### Examples

- `browser-lynx.exe --small-images`
- `browser-lynx.exe --small-images --image-scale=40`
- `browser-lynx.exe --small-images --max-img-width=120`
- `browser-lynx.exe --small-images --max-img-width=120 --max-img-height=100`

### Notes for low-end ARM

- Current image decoder path intentionally starts with HTTP + 24-bit BMP only to keep CPU and memory overhead low.
- Downscaling uses nearest-neighbor integer math only.
- Downscaling runs only when `--small-images` (or `--image-scale=<n>`) is enabled.

Video on YouTube below:

[![Video of Lynx-CE working on NTT Docomo / NEC Sigmarion 3](https://img.youtube.com/vi/A6zFduoXUJo/0.jpg)](https://www.youtube.com/watch?v=A6zFduoXUJo)
