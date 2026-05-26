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
  <a href="https://reactos.org/donate/">
    <img alt="Donate" src="https://img.shields.io/badge/%24-donate-E44E4A.svg"></a>
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

## What is ReactOS?

ReactOS is an Open Source effort to develop a quality operating system that is compatible with applications and drivers written for the Microsoft Windows NT family of operating systems.

This repository is a development and bring-up fork based on ReactOS master. Its purpose is to bring up modern hardware support, modern platform support (notably ARM64), and modern Windows NT compatibility work that can later be submitted upstream to ReactOS master.

You will find experimental drivers and features here. They are developed by [ahmedarif193](https://github.com/ahmedarif193) in the scope of proving working behavior first, then refining the result into upstreamable patches in the future.

The code of ReactOS is licensed under [GNU GPL 2.0](COPYING).

### Quality warning

ReactOS is currently an Alpha quality operating system, and this fork is even more experimental than the upstream master branch. Test it on virtual machines, development boards, or systems with no sensitive data.

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

## What This Fork Enables

This fork collects experimental bring-up work for modern ReactOS targets. The current focus is:

- ARM64 kernel, HAL, and FreeLoader bring-up
- UEFI boot, GOP framebuffer, and early display support
- Raspberry Pi platform bring-up, including PCIe/RP1-oriented work
- SD/eMMC, USB/xHCI, PCI, PnP, and storage/bus driver experiments
- Selected modern Windows NT compatibility and driver-model work

These features are experimental proof-of-work paths. Working pieces are expected to be cleaned up, validated, split into reviewable changes, and proposed upstream when they are ready.

See [INSTALL](INSTALL) for installation instructions. After building:

```sh
ninja install
```

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) and [PULL_REQUEST_MANAGEMENT.md](PULL_REQUEST_MANAGEMENT.md).

**Legal notice:** If you have seen proprietary Microsoft Windows source code (including but not limited to the leaked Windows NT 3.5, NT 4, 2000 source code and the Windows Research Kernel), your contribution won't be accepted because of potential copyright violation.

## Upstreaming

This is an experimental working fork for proving fixes, drivers, and platform bring-up work before splitting them into smaller upstreamable changes for ReactOS master.

When a change becomes stable enough, it should be cleaned up, reduced to the minimal correct diff, validated, and proposed back to upstream ReactOS master.
