# rosconfig — menuconfig for the ReactOS build

`rosconfig` is a small, dependency-free host tool that brings an
OpenWrt/kconfig-style `menuconfig` workflow to the ReactOS CMake build.
It is cross-OS and builds on Linux, macOS and Windows (Windows 10+ console),
with gcc, clang or MSVC.

## Usage

```
./menuconfig.sh              # first run: prepare output-Clang-amd64-debug
menuconfig.cmd               # Windows: prepare output-MinGW-amd64-debug
./menuconfig.sh --build-dir output-Clang-arm64-debug
menuconfig.cmd --build-dir output-Clang-arm64-debug
                              # open one output tree's configuration UI
./configure.sh menuconfig    # choose the target, then configure its output tree
configure.cmd menuconfig     # Windows: configure the selected output tree
./menuconfig.sh --self-test  # build the tool and run its non-interactive tests
menuconfig.cmd --self-test   # same test on Windows
```

Inside the UI, `Enter`/`Right` opens a submenu or edits an option;
`ESC`/`Left`/`Backspace` returns to the parent menu. `Space` toggles or
opens the selected value, `Y`/`N`/`A` set booleans directly, and `D` resets
to the default. `?` opens scrollable context help, `F1` shows the key
reference, `S` saves, and `Q` quits (asking to save if modified). At the
main menu, `ESC` also quits. `/` searches and jumps to symbols, `V` toggles
dependency-hidden entries, `L` reloads the saved cache, and `R` resets all
values after confirmation. When launched by a configure script, a clean exit
asks whether configuration should continue. Choosing No stops before CMake;
`Ctrl+C` is a hard cancel with status 130 and also never starts CMake.

## Kconfig-style UI coverage

| Feature | rosconfig behavior |
| --- | --- |
| Menu tree | `menu` blocks are real, nestable pages with breadcrumbs and restored parent selection. |
| Dependencies | Option and menu dependencies update visibility immediately; parent-menu dependencies are inherited. |
| Show hidden | `V` shows unavailable menus/options dimmed and read-only. Detailed help shows each met/unmet condition and its current value. |
| Search and jump | `/` searches keys, prompts, choice values and help, including hidden symbols. Visible results jump to their menu path; hidden results expose dependency help. |
| Value editors | Boolean/automatic values, radio-style choices and strings have keyboard editors; `Y`/`N`/`A` set booleans directly. |
| Defaults | `D` resets one option and `R` resets the whole configuration after confirmation. |
| Help | Inline summaries, scrollable `?` symbol/menu details and `F1` global key help. |
| Save/reload | `S` saves the selected cache; `L` reloads it, confirming before discarding local edits. Another cache can be selected with `--cache`. |
| Exit safety | Normal quit confirms Save/Discard/Cancel when modified. An integrated clean exit asks whether CMake should run; No stops cleanly, while `Ctrl+C` cancels immediately with status 130. |
| Responsive terminal | Lists, help and search results scroll and are redrawn using the current console size. |
| Self-test | `--self-test` checks parsing (including sourced files), hierarchy, architecture-scoped profiles, inherited dependencies, cache preservation/reload, overrides and CMake generation without opening the UI. |

This is the complete UI workflow for rosconfig's deliberately small
definition language. It is not a claim of full Linux Kconfig language
compatibility: expressions are ANDed `KEY=VALUE`/`KEY!=VALUE` conditions,
and constructs such as `select`, `imply`, numeric ranges and general boolean
expressions are not parsed.

## How it works

The compiled host tool lives in the ignored `.rosconfig/` directory at the
source root. Persistent selections live below the output tree they configure:

| File | Purpose |
| --- | --- |
| `$SOURCE/.rosconfig/rosconfig(.exe)` | the compiled host tool (built on demand) |
| `$SOURCE/.rosconfig/rosconfig*.(o|obj)` and stamps | incremental host-build state |
| `$BUILD/.rosconfig/config.cache` | persistent selections for one output tree |
| `$BUILD/.rosconfig/overrides.cmake` | that tree's generated CMake fragment |

- `configure.sh` / `configure.cmd` compile the tool if needed, create the
  selected output tree's cache when it does not exist yet, persist its target
  architecture/toolchain/build-type identity, and regenerate `overrides.cmake`
  on every run. The entry point, utilities,
  configuration model, terminal UI and self-test are separate translation
  units, so the host build recompiles only the changed part before relinking.
- `menuconfig.sh` / `menuconfig.cmd` can run before the first configure. When
  the selected output directory does not exist, they infer its target identity
  from a conventional `output-<toolchain>-<arch>-<type>` name, create only the
  directory and its `.rosconfig` state, and leave CMake generation to the
  subsequent configure run.
- On Windows, an already generated `rosconfig.exe` remains usable when no host
  compiler is installed; `build.cmd` reports that it is using the cached tool.
- `/PreLoad.cmake` (auto-loaded by CMake) includes
  `$CMAKE_BINARY_DIR/.rosconfig/overrides.cmake` if it exists. A configuration
  from another output tree can therefore never leak into this tree or a
  managed nested build. Without a cache nothing changes: the stock defaults from
  `sdk/cmake/config.cmake` apply.
- Precedence, highest first:
  1. explicit `-D` options on the configure/cmake command line
     (the generated fragment uses non-`FORCE` cache sets);
  2. configure script flags (`-a/--arch`, `--gcc/--clang`, `-r`, ...);
  3. the menuconfig cache;
  4. built-in defaults.
- The target selections `ARCH`, `TOOLCHAIN` and `BUILD_TYPE` are "meta"
  options. They are never emitted to CMake directly. In the integrated
  `configure.sh menuconfig` workflow they select the matching output directory;
  the completed menu is copied there before CMake starts. Explicit configure
  flags still win. The standalone `menuconfig.sh` workflow instead keeps the
  identity encoded by its selected output directory, so use `--build-dir` to
  choose another standalone target tree.
- Bool options can hold the value `auto`, which means "do not emit to
  CMake" — the conditional defaults in `sdk/cmake/config.cmake` (e.g.
  `ENABLE_WOW64`) stay in charge.
- From the source directory, the no-argument wrappers prepare their platform's
  default output tree. From another output directory they use that tree; an
  explicit `--build-dir <output-directory>` selects any conventionally named
  target tree. They do not consult source-global target state.
- Changed selections take effect the next time that tree is configured
  (`configure.sh` always starts from a fresh CMake cache).

## Main menu layout

The main menu starts with the target identity, then the settings that only
affect how the tree is built, then the operating system itself, and ends with
debugging and testing. A submenu is hidden while none of its options apply to
the selected target.

| Entry | Contents |
| --- | --- |
| Target architecture | `ARCH`: x86-64, x86 or AArch64. |
| Target profile | The machine or board to build for; the list depends on the architecture. |
| Compiler toolchain | Clang, GCC or MSVC. |
| Build type | Debug or Release. |
| Code generation | CPU instruction set, tuning, LTO and stack protector (GCC and Clang only). |
| Build options | ccache, separate debug symbol files, `.rossym` compression and MSVC analysis. |
| Boot options | UEFI HTTP boot for the boards that support it. |
| System | Target NT version, ALPC, ISA Plug and Play and the ROSV hypervisor. |
| Graphics | Display driver model, Mesa Gallium and LLVMpipe/Lavapipe. |
| Compatibility layers | WoW64, FEX ARM64EC and validation of their build outputs. |
| Desktop and applications | Early Winlogon background, wallpapers and rosapps. |
| Kernel debugging | Kernel debugger and KD transport. |
| Testing | Test suite and boot-time test automation (Debug builds only). |

## Target profiles

A profile selects the machine to build for. Only the selector of the chosen
architecture is shown. Profile definitions and their CMake manifests are kept
below `sdk/cmake/rosconfig/profiles/`:

```
profiles/
  profiles.def
  apply.cmake
  amd64/{profiles.def,generic.cmake,lattepandamu.cmake}
  i386/{profiles.def,generic.cmake,pc98.cmake,xbox.cmake}
  arm64/{profiles.def,generic.cmake,profile_raspberry.cmake}
```

Each `<variant>.cmake` declares `ROSCONFIG_PROFILE_PACKAGES`, a list of CMake
targets that must exist, and `ROSCONFIG_PROFILE_CONFIGS`, a list of typed,
profile-owned values written as `NAME:TYPE=VALUE`. Package targets are checked after all
normal ReactOS subdirectories have been configured, so an incomplete or
incompatible profile fails during configuration instead of producing a
partially populated image.

Every supported architecture has a `generic` default profile. i386 also
provides `pc98` (NEC PC-9800 series) and `xbox` (Original Xbox), which set the
`SARCH` sub-architecture and with it the HAL, boot loader and display drivers.
The generic i386 profile leaves `SARCH` alone, so an explicit `-DSARCH=` still
works; saved caches that contain a `SARCH` selection migrate to the matching
profile. ARM64 provides
`profile_raspberry`, displayed as **Raspberry Pi 3/5**, which enables both
boards' driver sets in one image: SD/SDIO, DWC2, SMSC95xx and RP1 Ethernet,
CYW43xx Wi-Fi, display, OpenGL and audio, plus the pinned `rpi3winsync`
Windows 10 BSP snapshot. Existing `rpi3` and `rpi5` selections migrate to this
combined profile in both menuconfig and CMake. Hardware IDs retain each
device's driver binding. AMD64 additionally provides `lattepandamu`.
The Raspberry Pi and LattePanda Mu profiles expose the HTTP boot
option. Enabling it from the `Boot options` menu builds the FreeLdr HTTP path,
makes it the zero-timeout default boot entry, and packages the board's external
UEFI network stack. Generic builds keep the Pi-specific drivers and hardware
bindings disabled.

Profile-owned config values are enforced when the profile is applied, so an
existing tree can switch profiles without retaining stale values from the old
profile. Explicit `-D` precedence remains unchanged for ordinary menu and
image-content options. If `ROSCONFIG_PROFILE` is not provided, CMake uses the
architecture's `generic` profile.

Optional image contents are independent switches, not profiles: rosapps and
wallpapers live in `Desktop and applications`, FEX diagnostic payloads in
`Compatibility layers`, and the test suite with its boot-time automation in
`Testing`. Enabling `ENABLE_ROSTESTS` builds and packages the test suite and
`rosautotest` runner, and can be combined with any target profile.

## Option definitions

Options are declared in `sdk/cmake/rosconfig.def` and must mirror
`sdk/cmake/config.cmake`. The syntax is kconfig-inspired and documented at
the top of that file. Quick example:

```
menu "Build options"

    depends ARCH=amd64

menu "Debugging"

config KD_DEBUGGER
    prompt "Kernel debugger mode"
    type choice
    value AUTO     "Automatic"
    value NONE     "Disabled"
    value KDBG     "Integrated ReactOS debugger (KDBG)"
    value EXTERNAL "External KD protocol"
    default AUTO
    help
      Select the mutually exclusive kernel debugger implementation.

endmenu

endmenu
```

`menu` blocks are real, nestable submenus and may have `depends` lines before
their first child. `source "relative/file.def"` includes another definition
relative to the file containing the directive. Supported config directives are
`prompt`, `type bool|choice|string`,
`value <v> "<label>"`, `default`, `depends KEY=VAL` / `KEY!=VAL`,
`meta`, `var <CMakeName>`, `cmaketype BOOL|STRING`, `help`. Separate `depends`
lines are ANDed and terms joined by `||` on one line are alternatives. There
is no `&&`: a term such as `A=x && B=y` compares `A` with the literal text
`x && B=y`, so write one `depends` line per condition instead.

Kernel debugger implementations are mutually exclusive. External KD transport
DLLs are not independent enable switches: rosconfig selects which transport is
installed as the default `kdcom.dll`, while compatible alternatives remain
available through the boot `DEBUGPORT` option.

Visibility follows the build path that actually consumes each setting:

| Setting group | Visible when |
| --- | --- |
| Code generation menu | GCC or Clang; MSVC does not consume `OARCH`/`TUNE`. |
| Link-time optimization | Release with GCC or Clang. |
| Debug-symbol controls | GCC or Clang; the MSVC path manages PDB output itself. |
| Stack protector | GCC only, matching `config.cmake`. |
| Runtime checks and static analysis | MSVC only; runtime checks are additionally limited to Debug. |
| UEFI HTTP boot | LattePanda Mu and Raspberry Pi profiles. |
| ISA Plug and Play | i386, except the Xbox profile. |
| ROSV hypervisor | AMD64 builds only. |
| LLVMpipe and Lavapipe | Clang AMD64 or ARM64 builds with the Mesa Gallium driver enabled. |
| FEX ARM64EC runtime | ARM64 builds only. |
| WoW64 subsystem | AMD64 and ARM64 builds. |
| Testing menu | Debug builds only. |

## Graphics driver model

The `Graphics` menu selects either the legacy XPDM/VideoPort path or the
experimental WDDM/dxgkrnl path.
XPDM restores the UEFI framebuffer registration and, for the Raspberry Pi 5
profile, builds the preserved VC4 XPDM miniport. WDDM builds the DirectX
graphics kernel stack and selects the WDDM VC4 miniport instead.

WDDM builds target the Windows 11 24H2 (WDDM 3.2) contract; `config.cmake`
fixes `REACTOS_WDDM_LEVEL` to 3.2, so there is no selectable level. Individual
features still report only what is implemented end to end.

## Tool CLI (used by the scripts)

```
rosconfig --def <file> --cache <file> [mode] [--set K=V ...] [--override K=V ...]
rosconfig --self-test
  --menu             interactive UI (default mode)
  --ask-configure    ask on clean exit whether the calling configure workflow
                     should continue (used by configure.sh/configure.cmd)
  --defaults         create/refresh the cache, keeping existing values
                     and preserving unknown lines
  --generate <out>   write the CMake pre-load fragment
  --get <KEY>        print one value (used e.g. for ENABLE_FEX_ARM64EC)
  --self-test        run built-in parser/model/profile/cache/generator checks
  --set K=V          validate and set a value before the selected mode;
                     --defaults persists it to the cache
  --override K=V     transient value for dependency evaluation only,
                     e.g. the ARCH chosen on the command line
```
