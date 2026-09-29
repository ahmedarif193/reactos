<p align=center>
  <img alt="LiberNT" src="media/graphics/branding/libernt-logo.png" width="480">
</p>

<p align=center>
  <a href="COPYING3">
    <img alt="License" src="https://img.shields.io/badge/license-GNU_GPL_3.0-0161B6.svg"></a>
  <a href="https://github.com/sponsors/ahmedarif193">
    <img alt="Support LiberNT" src="https://img.shields.io/badge/support-LiberNT-E44E4A.svg"></a>
</p>

## Quick Links
[Website](https://libernt.com/) &bull;
[Source code](https://github.com/ahmedarif193/LiberNT) &bull;
[Issues](https://github.com/ahmedarif193/LiberNT/issues) &bull;
[Wiki](https://github.com/ahmedarif193/LiberNT/wiki) &bull;
[Build environment](https://github.com/ahmedarif193/winget-rosbe)

## What is LiberNT?

LiberNT is a free NT operating system, built out of love for the NT architecture. The name says it: *liber* is Latin for free. LiberNT targets Windows® 11 compatibility, with room for freestyle and experimental features.

LiberNT is based on ReactOS, the Open Source effort to develop an operating system that is compatible with applications and drivers written for the Microsoft Windows NT family of operating systems. It is a fork with extensive changes to kernel-space components and drivers, aimed at stability and modern hardware, and it brings up modern platforms, ARM64 first.

Our goal is a system that stays maintainable in the long run: stable, reliable and efficient, running on embedded devices too, and able to support every feature a modern operating system should have.

LiberNT is licensed under the [GNU GPL 3.0](COPYING3). Code inherited from ReactOS keeps the licence stated in its file headers, most of it GPL 2.0 or later.

### Status

So far, LiberNT has proven more stable and faster than ReactOS in CPU management, memory management and I/O resource handling. Windows 11 is the compatibility reference.

## Support LiberNT

We want LiberNT to be a free NT operating system that everyone can rely on, every day. Support our goal by [donating](https://github.com/sponsors/ahmedarif193) or by joining our new community. See [Contributing](#contributing) to get started.

## Upstream ReactOS

LiberNT would not exist without ReactOS. With all our respect and gratitude to the ReactOS Team & Contributors, whose work LiberNT builds on, you can find the upstream source code at [github.com/reactos/reactos](https://github.com/reactos/reactos).

## Building

LiberNT builds with RosBE, a cross-build toolchain for Linux, macOS and Windows. The steps below use `configure.sh` on Linux and macOS.

### 1. Install the tools

You need Git, CMake, Ninja and a C/C++ compiler for your own machine. RosBE adds the compilers that build LiberNT.

On Linux or macOS:

```sh
curl -fsSL https://raw.githubusercontent.com/ahmedarif193/winget-rosbe/main/rosbe-unix-bootstrap.sh | sh
```

On Windows, install RosBE with `winget install AhmedArif.RosBE`, then run `rosbe install` and `rosbe enable`. See [winget-rosbe](https://github.com/ahmedarif193/winget-rosbe) for details.

### 2. Get the source

```sh
git clone https://github.com/ahmedarif193/LiberNT.git
cd LiberNT
```

The first configure run also fetches the submodule and external sources the build needs.

### 3. Configure

```sh
./configure.sh
```

With no options, this sets up an AMD64 Debug build with Clang in the folder `output-Clang-amd64-debug`. The folder name always follows the pattern `output-<compiler>-<architecture>-<debug|release>`.

| Option | What it does |
|---|---|
| `-a`, `--arch <arch>` | Chooses the target architecture: `amd64` (default), `i386`, `arm64` or `riscv64`. |
| `--clang` | Builds with Clang/LLVM from RosBE. This is the default. |
| `--gcc` | Builds with GCC from RosBE instead of Clang. The macOS RosBE does not include GCC. |
| `-r`, `--release` | Makes an optimized Release build. Without it you get a Debug build, which is easier to debug. |
| `menuconfig` | Opens a menu to choose optional components first. Your choices are kept in the output folder. |
| `makefiles` | Uses `make` instead of Ninja. |
| `--no-feeds-update` | Skips fetching the external sources listed in `feeds.conf`, for example when offline. |
| `-D<name>=<value>` | Passes a setting straight to CMake. |

For example:

```sh
./configure.sh -a arm64              # ARM64 Debug build with Clang
./configure.sh -a i386 --gcc         # 32-bit x86 build with GCC
./configure.sh -a amd64 --release    # optimized AMD64 build
```

### 4. Build

Go to the output folder and run Ninja with the target you want:

```sh
cd output-Clang-amd64-debug
ninja reactosimg
```

| Command | Creates | Use it to |
|---|---|---|
| `ninja bootcd` | `bootcd.iso` | Install LiberNT from a CD image, in a virtual machine or on a PC. |
| `ninja reactosimg` | `ReactOS.img` | Boot a disk with LiberNT already installed. Start it in a virtual machine such as QEMU, or write it to a USB drive or SD card. It boots with UEFI or BIOS. |
| `ninja reactosvhd` | `ReactOS.vhd` | Use the same preinstalled disk in virtual machines that take VHD disks, such as Hyper-V and VirtualBox. |

Each target builds everything it needs first, and the files are written to the output folder.

## What LiberNT Enables

LiberNT brings modern hardware and platforms to the NT architecture. The current focus is:

- ARM64 kernel, HAL, and FreeLoader bring-up
- UEFI boot, GOP framebuffer, and early display support
- Raspberry Pi platform bring-up, including PCIe/RP1-oriented work
- SD/eMMC, USB/xHCI, PCI, PnP, and storage/bus drivers
- Windows 11 (NT10) compatibility and driver-model work

See [INSTALL](INSTALL) for installation instructions. After building:

```sh
ninja install
```

## Contributing

Report bugs and suggest features in [Issues](https://github.com/ahmedarif193/LiberNT/issues), and find guides and notes in the [Wiki](https://github.com/ahmedarif193/LiberNT/wiki).

See [CONTRIBUTING.md](CONTRIBUTING.md) and [PULL_REQUEST_MANAGEMENT.md](PULL_REQUEST_MANAGEMENT.md).

**Legal notice:** If you have seen proprietary Microsoft Windows source code (including but not limited to the leaked Windows NT 3.5, NT 4, 2000 source code and the Windows Research Kernel), your contribution won't be accepted because of potential copyright violation.

## Use of AI

We want to make it clear: AI is part of how LiberNT is built. It helps us review and write code, and everything it writes is then reviewed by a human. Before anything reaches the main branch, a person runs and validates the end-to-end tests. We care about this project too much to let a regression slip through.

It also helps us read documentation, extract definitions and fix bugs. For any given bug, it compares what the official Microsoft Learn documentation says with what our function actually does, so we can see exactly where our behavior differs.

Like any other tool, AI is there to help us achieve our goals. It follows the same rules we hold ourselves to, set out in [AGENTS.md](AGENTS.md):

- We build from the Windows SDK and WDK headers, Windows 11 public symbols, Microsoft documentation and black-box runs on Windows 11.
- We stay clean-room: we never transcribe disassembled or decompiled Windows code, and we never copy GPL code from Linux.
- A fix is only a fix once we have seen it work in a real boot or test run. Once tests pass, a future CI will keep them passing and preserve our overall progress.
- We compare our test results check by check with the same tests run on Windows 11.

If you use AI in your own contributions, you are welcome here too. Just tell us in the pull request which parts it helped with and how you tested them. That honesty is all we ask.
