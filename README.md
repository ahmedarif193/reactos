<p align=center>
  <a href="https://reactos.org/">
    <img alt="ReactOS" src="https://reactos.org/wiki/images/0/02/ReactOS_logo.png">
  </a>
</p>

---

<p align=center>
  <a href="https://reactos.org/project-news/reactos-0415-released/">
    <img alt="ReactOS 0.4.15 Release" src="https://img.shields.io/badge/release-0.4.15-0688CB.svg"></a>
  <a href="https://reactos.org/download/">
    <img alt="Download ReactOS" src="https://img.shields.io/badge/download-latest-0688CB.svg"></a>
  <a href="https://sourceforge.net/projects/reactos/">
    <img alt="SourceForge Download" src="https://img.shields.io/sourceforge/dm/reactos.svg?colorB=0688CB"></a>
  <a href="COPYING">
    <img alt="License" src="https://img.shields.io/badge/license-GNU_GPL_2.0-0688CB.svg"></a>
  <a href="https://paypal.me/staillar">
    <img alt="Support this fork" src="https://img.shields.io/badge/%24-donate-E44E4A.svg"></a>
  <a href="https://twitter.com/reactos">
    <img alt="Follow on Twitter" src="https://img.shields.io/twitter/follow/reactos.svg?style=social&label=Follow%20%40reactos"></a>
</p>

## Quick Links
[Website](https://reactos.org/) &bull;
[Official chat](https://chat.reactos.org/) &bull;
[Wiki](https://reactos.org/wiki/) &bull;
[Forum](https://reactos.org/forum/) &bull;
[Community Discord](https://discord.gg/7knjvhT) &bull;
[JIRA Bug Tracker](https://jira.reactos.org/issues/) &bull;
[ReactOS Git mirror](https://git.reactos.org/) &bull;
[Testman](https://reactos.org/testman/)

## What is LiberNT?

LiberNT is a free NT operating system based on ReactOS, the Open Source effort to develop an operating system that is compatible with applications and drivers written for the Microsoft Windows NT family of operating systems.

LiberNT is a fork with extensive changes to kernel-space components and drivers, aimed at stability and modern hardware. It brings up modern platforms (ARM64 first) and targets Windows 11 compatibility. Our goal is a usable, efficient system that also runs on embedded devices.

Its drivers and features are developed by [ahmedarif193](https://github.com/ahmedarif193). Behavior is proven here first, then refined into patches for upstream ReactOS.

LiberNT, like ReactOS, is licensed under [GNU GPL 2.0](COPYING).

### Status

So far, LiberNT has proven more stable and faster than ReactOS in CPU management, memory management and I/O resource handling. Windows 11 is the compatibility reference.

## Building

Use the RosBE package from [winget-rosbe](https://github.com/ahmedarif193/winget-rosbe). This is the RosBE setup associated with this fork and is the expected build environment.

The Linux build path is proven to work with `configure.sh`.

From the repository root:

```sh
./configure.sh
```

That default configuration sets up an `amd64` debug build using GCC.

For Clang:

```sh
./configure.sh --clang
```

For other architectures, use `-a`:

```sh
./configure.sh -a arm64       # ARM64 (debug, GCC)
./configure.sh --clang -a arm64
./configure.sh -a i386        # 32-bit x86
```

For release builds, add `-r` or `--release`:

```sh
./configure.sh -r
./configure.sh --clang -a arm64 --release
```

After configuring, build from the generated output directory with Ninja:

```sh
ninja
ninja bootcd
ninja livecd
```

## What LiberNT Enables

LiberNT brings modern hardware and platforms to the NT architecture. The current focus is:

- ARM64 kernel, HAL, and FreeLoader bring-up
- UEFI boot, GOP framebuffer, and early display support
- Raspberry Pi platform bring-up, including PCIe/RP1-oriented work
- SD/eMMC, USB/xHCI, PCI, PnP, and storage/bus drivers
- Windows 11 (NT10) compatibility and driver-model work

Proven pieces are cleaned up, validated, split into reviewable changes, and proposed upstream.

See [INSTALL](INSTALL) for installation instructions. After building:

```sh
ninja install
```

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) and [PULL_REQUEST_MANAGEMENT.md](PULL_REQUEST_MANAGEMENT.md).

**Legal notice:** If you have seen proprietary Microsoft Windows source code (including but not limited to the leaked Windows NT 3.5, NT 4, 2000 source code and the Windows Research Kernel), your contribution won't be accepted because of potential copyright violation.

## Upstreaming

LiberNT proves fixes, drivers, and platform bring-up work first, then splits them into smaller upstreamable changes for ReactOS master.

Once a change is proven, it is cleaned up, reduced to the minimal correct diff, validated, and proposed back to upstream ReactOS master.

## Use of AI

We want to make it clear: AI is part of how LiberNT is built. It helps us review and write code, and everything it writes is then reviewed by a human. Before anything reaches the main branch, a person runs and validates the end-to-end tests. We care about this project too much to let a regression slip through.

It also helps us read documentation, extract definitions and fix bugs. For any given bug, it compares what the official Microsoft Learn documentation says with what our function actually does, so we can see exactly where our behavior differs.

Like any other tool, AI is there to help us achieve our goals. It follows the same rules we hold ourselves to, set out in [AGENTS.md](AGENTS.md):

- We build from the Windows SDK and WDK headers, Windows 11 public symbols, Microsoft documentation and black-box runs on Windows 11.
- We stay clean-room: we never transcribe disassembled or decompiled Windows code, and we never copy GPL code from Linux.
- A fix is only a fix once we have seen it work in a real boot or test run. Once tests pass, a future CI will keep them passing and preserve our overall progress.
- We compare our test results check by check with the same tests run on Windows 11.

If you use AI in your own contributions, you are welcome here too. Just tell us in the pull request which parts it helped with and how you tested them. That honesty is all we ask.
