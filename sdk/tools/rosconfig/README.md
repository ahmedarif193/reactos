# rosconfig — menuconfig for the ReactOS build

`rosconfig` is a small, dependency-free host tool that brings an
OpenWrt/kconfig-style `menuconfig` workflow to the ReactOS CMake build.
It is cross-OS and builds on Linux, macOS and Windows (Windows 10+ console),
with gcc, clang or MSVC.

## Usage

```
./menuconfig.sh              # Linux/macOS: open the configuration UI
menuconfig.cmd               # Windows: same
./configure.sh menuconfig    # open the UI, then configure with the result
configure.cmd menuconfig     # Windows: same, restarting with the saved choices
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

Everything lives in the untracked `.rosconfig/` directory at the source
root (the tool's own tmp folder, listed in `.gitignore`):

| File | Purpose |
| --- | --- |
| `.rosconfig/rosconfig(.exe)` | the compiled host tool (built on demand) |
| `.rosconfig/rosconfig*.(o|obj)` and stamps | incremental host-build state |
| `.rosconfig/config.cache` | **the cache** — your persistent selections |
| `.rosconfig/overrides.cmake` | generated CMake fragment |

- `configure.sh` / `configure.cmd` compile the tool if needed, create the
  cache with defaults when it does not exist yet, and regenerate
  `overrides.cmake` from it on every run. The entry point, utilities,
  configuration model, terminal UI and self-test are separate translation
  units, so the host build recompiles only the changed part before relinking.
- On Windows, an already generated `rosconfig.exe` remains usable when no host
  compiler is installed; `build.cmd` reports that it is using the cached tool.
- `/PreLoad.cmake` (auto-loaded by CMake) includes `overrides.cmake` if it
  exists, so the selections also apply to trees configured with plain
  `cmake`. Without a cache nothing changes: the stock defaults from
  `sdk/cmake/config.cmake` apply.
- Precedence, highest first:
  1. explicit `-D` options on the configure/cmake command line
     (the generated fragment uses non-`FORCE` cache sets);
  2. configure script flags (`-a/--arch`, `--gcc/--clang`, `-r`, ...);
  3. the menuconfig cache;
  4. built-in defaults.
- The target selections `ARCH`, `TOOLCHAIN` and `BUILD_TYPE` are "meta"
  options: they are consumed by the configure scripts themselves (choice
  of toolchain and output directory) and are never passed to CMake
  directly.
- Bool options can hold the value `auto`, which means "do not emit to
  CMake" — the conditional defaults in `sdk/cmake/config.cmake` (e.g.
  `DBG` following the build type) stay in charge.
- Changed selections take effect the next time a tree is configured
  (`configure.sh` always starts from a fresh CMake cache).

## Target profiles and modules

The `Target platform` submenu shows the profile selector for the selected
architecture. Profile definitions and their CMake manifests are kept below
`sdk/cmake/rosconfig/profiles/`:

```
profiles/
  profiles.def
  apply.cmake
  amd64/{profiles.def,generic.cmake,lattepandamu.cmake}
  i386/{profiles.def,generic.cmake}
  arm64/{profiles.def,generic.cmake,rpi5.cmake}
```

Each `<variant>.cmake` declares `ROSCONFIG_PROFILE_PACKAGES`, a list of CMake
targets that must exist, and `ROSCONFIG_PROFILE_CONFIGS`, a list of typed,
profile-owned values written as `NAME:TYPE=VALUE`. Package targets are checked after all
normal ReactOS subdirectories have been configured, so an incomplete or
incompatible profile fails during configuration instead of producing a
partially populated image.

Every supported architecture has a `generic` default profile. ARM64
additionally provides `rpi5`, which enables the firmware/device-tree payload
and builds the RP1 Ethernet, CYW43455 Wi-Fi, and Raspberry Pi 5 VC4 display
drivers. AMD64 additionally provides `lattepandamu`. Both board profiles expose
the HTTP boot option. Enabling it from the `Boot options` menu builds the
FreeLdr HTTP path, makes it the zero-timeout default boot entry, and packages
the board's external UEFI network stack. The Raspberry Pi 5 profile leaves this
choice to the user instead of forcing HTTP boot on. Generic builds exclude
these board-only payloads and targets.

Profile-owned config values are enforced when the profile is applied, so an
existing tree can switch profiles without retaining stale values from the old
profile. Explicit `-D` precedence remains unchanged for ordinary menu and
module options. If `ROSCONFIG_PROFILE` is not provided, CMake uses the
architecture's `generic` profile.

Modules are independent switches, not profiles. The `Modules` submenu exposes
the existing RosApps, RosTests, and wallpapers build options. Enabling
`ENABLE_ROSTESTS` builds and packages the test suite and `rosautotest` runner,
and can be combined with either the generic or Raspberry Pi 5 profile.

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
`value <v> "<label>"`, `default`, `depends KEY=VAL` / `KEY!=VAL` (ANDed),
`meta`, `var <CMakeName>`, `cmaketype BOOL|STRING`, `help`.

Kernel debugger implementations are mutually exclusive. External KD transport
DLLs are not independent enable switches: rosconfig selects which transport is
installed as the default `kdcom.dll`, while compatible alternatives remain
available through the boot `DEBUGPORT` option.

Visibility follows the build path that actually consumes each setting:

| Setting group | Visible when |
| --- | --- |
| Target CPU generation | GCC or Clang; MSVC does not consume `OARCH`/`TUNE`. |
| Debug compiler level | Debug builds, with separate GCC/Clang and MSVC value lists. Release flags are fixed by the toolchain files. |
| Release optimizations | Release with GCC or Clang; currently exposes LTCG. |
| Dummy PSEH | GCC or Clang; MSVC always uses native SEH. |
| Debug-symbol controls | GCC or Clang; the MSVC path manages PDB output itself. |
| Stack protector | GCC only, matching `config.cmake`. |
| Runtime checks and static analysis | MSVC only; runtime checks are additionally limited to Debug. |
| ReactOS test suite | Debug builds only. |
| FEX ARM64EC submodule | ARM64 builds only. |
| WoW64 subsystem | AMD64 builds only. |
| WDDM compatibility level | WDDM display-model builds only. |

## Graphics driver model

The `Components and features -> Graphics stack` menu selects either the legacy
XPDM/VideoPort path or the experimental WDDM/dxgkrnl path. XPDM restores the
UEFI framebuffer registration and, for the Raspberry Pi 5 profile, builds the
preserved VC4 XPDM miniport. WDDM builds the DirectX graphics kernel stack and
selects the WDDM VC4 miniport instead.

The WDDM level is a compatibility ceiling, not a capability assertion.
WDDM targets compile against the highest audited shared header layout, while
runtime reporting is capped by the selected level and by the subsystems that
are actually implemented end to end.

## Tool CLI (used by the scripts)

```
rosconfig --def <file> --cache <file> [mode] [--override K=V ...]
rosconfig --self-test
  --menu             interactive UI (default mode)
  --ask-configure    ask on clean exit whether the calling configure workflow
                     should continue (used by configure.sh/configure.cmd)
  --defaults         create/refresh the cache, keeping existing values
                     and preserving unknown lines
  --generate <out>   write the CMake pre-load fragment
  --get <KEY>        print one value (used e.g. for ENABLE_FEX_ARM64EC)
  --self-test        run built-in parser/model/profile/cache/generator checks
  --override K=V     transient value for dependency evaluation only,
                     e.g. the ARCH chosen on the command line
```
