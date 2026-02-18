<p align="center">
  <img alt="ReactOS" src="https://reactos.org/wiki/images/0/02/ReactOS_logo.png">
</p>

# ReactOS Build Guide

Building ReactOS is handled entirely by the `./configurev2.sh` script. It automatically downloads the required compilers (MinGW/GCC), sets up the Rust toolchain, and configures the build environment for you.

## Supported Features (This fork)

- Modern HAL work with MSI/MSI-X interrupt support.
- UEFI support path for `amd64` and `arm64`.
- Ramdisk boot support for bring-up and live images.
- Reworked USB stack targeting up to USB 3.2 class hardware, including MSI/MSI-X-capable controller paths.
- Stabilized Plug and Play (PnP) behavior for USB and PCI (SDIO support is added to the roadmap).

## Driver Support Status (in testing, to be upstreamed later)

| Area | Status |
| --- | --- |
| ARM64 CPU power management | Ongoing |
| Intel CPU power management | Ongoing |
| AHCI storage port | Done |
| USB RNDIS NIC | Done (VBox, QEMU, Alder Lake Intel CPU targets) |
| USB video class | Done (VBox, QEMU, Alder Lake Intel CPU targets) |
| USB xHCI (USB 3.x) | Done (VBox, QEMU, Alder Lake Intel CPU targets) |
| NDIS 6 stack | Ongoing |
| Intel e1000 | Done |

Validation note: this fork is proven to work on Intel e1000 and Intel N100 CPU targets in bare UEFI mode (no CSM). Additional hardware validation is still pending.

README note: a supported hardware list will be added in a later update; current bring-up/testing includes LattePanda Mu hardware.

## Quick Start

The script automatically fetches the necessary MinGW toolchains and sets up Rust (via rustup) if they are missing. On macOS, it will also check for Homebrew dependencies.

The default build creates a **Debug** version for **amd64** using the **MinGW** compiler.

```bash
# 1. Configure
./configurev2.sh

# 2. Compile
cd output-MinGW-amd64-Debug
ninja livecd
```

## Build Configuration Matrix

You can customize the architecture, compiler, and build type using flags.

### 1. Architectures

Change the target CPU using `-a`. The script defaults to `amd64`.

**i386 (32-bit)**

```bash
./configurev2.sh -a i386
cd output-MinGW-i386-Debug && ninja livecd
```

**ARM64 (AArch64)**

```bash
./configurev2.sh -a arm64
cd output-MinGW-arm64-Debug && ninja livecd
```

### 2. Clang Support

To build using LLVM/Clang instead of GCC, add the `--clang` flag. You can optionally specify a version.

**Default Clang (amd64)**

```bash
./configurev2.sh --clang
cd output-Clang-amd64-Debug && ninja livecd
```

**Specific Clang Version (e.g., v21)**

```bash
./configurev2.sh --clang=21
cd output-Clang-amd64-Debug && ninja livecd
```

### 3. Release Builds

By default, the script builds in Debug mode. For an optimized build, add `-r` (or `--release`).

**Release Mode (MinGW)**

```bash
./configurev2.sh -r
cd output-MinGW-amd64-MinSizeRel && ninja livecd
```

**Release Mode (Clang)**

```bash
./configurev2.sh --clang -r
cd output-Clang-amd64-MinSizeRel && ninja livecd
```

## Command Reference

| Flag | Description | Values |
| --- | --- | --- |
| `-a` | Architecture | `amd64` (default), `i386`, `arm64` |
| `-r` | Release Mode | Switches build type to `MinSizeRel` |
| `--clang` | Compiler | Switches from MinGW to Clang |
| `--clang=XX` | Compiler Version | Uses specific Clang version (e.g., `21`) |
| `-c` | Ccache | Enables ccache for faster rebuilds |
| `--clean` | Clean | Wipes the build directory before starting |
