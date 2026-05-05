# Lynx-CE
Latest source code version is: 0.01.

Just a minimal text browser inspired from Lynx for Windows CE

9/8/2025 17:03PM JST: There is now the build instructions for experimental (PolarSSL) version. It is untested. Please use it as a reference or a stepping stone.

12/30/2024 18:57PM JST: Now supports following links. Testing form text input. The latest source code is in experimental folder.

Used ChatGPT - o1 to help me make this project work.

This project was successfully compiled using CeGCC.

Video on YouTube below:

[![Video of Lynx-CE working on NTT Docomo / NEC Sigmarion 3](https://img.youtube.com/vi/A6zFduoXUJo/0.jpg)](https://www.youtube.com/watch?v=A6zFduoXUJo)


## Transport/TLS notes (WinCE 3.0+)

- Network fetch now goes through `net_transport` abstraction (`net_transport.h/.c`) with HTTP (plain TCP) and HTTPS routing.
- HTTPS URLs (`https://`) are parsed and routed to the TLS backend.
- Certificate verification is **not** disabled by default. Testing-only bypass is available via runtime flag: `--tls-insecure`.
- CA bundle path can be supplied with `--ca-bundle=<path>` and hostname is passed to TLS verification APIs when supported by the linked PolarSSL/MbedTLS build.

### Conservative TLS profile guidance for slow ARM devices

For CE 3.0-era hardware, prefer:
- TLS 1.0/1.1 compatibility mode when TLS 1.2 is not practical in your PolarSSL/MbedTLS version.
- ECDHE-RSA-AES128-SHA or AES128-SHA style suites (avoid large RSA key-exchange and heavy ciphers where possible).
- Session reuse enabled in TLS library config to reduce repeat handshake cost.
- Minimal enabled cipher list to shrink code size and handshake footprint.

Exact protocol/cipher availability depends on the PolarSSL/MbedTLS version and compile-time configuration used for your toolchain.

## Recommended contribution flow for untested TLS changes

If TLS changes are not validated on a real WinCE target yet:

1. Create and push a branch from a personal fork (for example `tls-test`).
2. Open a **Draft PR** to this repository.
3. Use the checklist below before marking Ready for Review.

### TLS validation checklist

- [ ] HTTP path still works (`http://`).
- [ ] HTTPS handshake succeeds on known-good sites (`https://`).
- [ ] Certificate verification fails for invalid/self-signed endpoints by default.
- [ ] `--tls-insecure` allows testing bypass only when explicitly set.
- [ ] `--ca-bundle=<path>` loads CAs and validates chain.
- [ ] Hostname mismatch is rejected.
- [ ] Repeated connections show session reuse behavior (if enabled in TLS library build).
- [ ] Tested on at least one WinCE device/emulator target used by maintainers.
