# AGENTS.md

This file introduces LiberNT and sets the working rules and engineering criteria for everyone who changes it: human contributors, AI agents and other automated tools. Start with the introduction; the rules follow it.

## About LiberNT

LiberNT is a free NT operating system. It is a fork of ReactOS with extensive changes to kernel-space components and drivers, aimed at stability and modern hardware. It brings up modern platforms, ARM64 first, and targets NT10/Windows 11 compatibility. Behavior is proven here first, then refined into patches for upstream ReactOS.

- Target architectures: amd64, i386 and arm64, with RISC-V64/PPC in progress.
- Windows 11 is the compatibility reference.
- A subdirectory can have its own `AGENTS.md`; its rules add to these for that subtree. The kernel has one for the memory manager.

### How LiberNT differs from ReactOS

Most of the tree is still shared with ReactOS, and the components synchronised with Wine are listed in `media/doc/WINESYNC.txt`. The main departures are:

| Area | In LiberNT | Where |
|---|---|---|
| Memory manager | NVS, the NT Virtual Memory Subsystem: a clean-room NT10 memory manager with its own kernel pool. The ReactOS ARM3 memory manager has been removed. | `ntoskrnl/nvs` |
| Cache manager | A clean-room cache manager. | `ntoskrnl/cc` |
| ARM64 | The ARM64 port: kernel, HAL and FreeLoader. | `ntoskrnl/ke/arm64`, `hal/halarm64`, `boot/freeldr/freeldr/arch/arm64` |
| RISC-V64 | The HAL of the port in progress. | `hal/halriscv64` |
| 32-bit applications | WoW64 on every 64-bit target. On ARM64, FEX runs the x86 code, and ARM64EC/CHPE is supported. | `dll/win32/wow64`, `dll/win32/wow64cpu`, `dll/win32/wow64win` |
| Graphics | A WDDM graphics kernel and DWM desktop composition. | `drivers/directx/dxgkrnl`, `base/system/dwm` |
| File systems | A new NTFS driver and library. The original driver is still in the tree. | `drivers/filesystems/ntfs_new`, `sdk/lib/fslib/ntfslib_new` |
| Buses | USB xHCI and SD bus drivers. | `drivers/usb/usbxhci`, `drivers/bus/sd` |
| Boards | Raspberry Pi drivers, including VideoCore graphics, RP1 Ethernet and CYW43455 Wi-Fi. | `drivers/directx/rpi3vc4`, `drivers/directx/rpi5vc4`, `drivers/network/dd/rp1gem`, `drivers/network/dd/cyw43455` |

### Reading the rules

- The rules are written as instructions to whoever makes the change. Where a rule says "the user", read the maintainer or the person directing the work.
- The paths in "Testing in a VM" are those of the maintainer's test machine. Adapt them to your own setup.

## Principles

- Make the smallest correct change. No unrelated refactors, renames or style changes; report out-of-scope ideas instead of implementing them.
- Fix the underlying behavior, not the symptom. Use a workaround only to test a hypothesis, then remove it and make the real fix.
- Never invent an API, structure, offset, constant or behavior. When the sources of truth do not answer, report the gap as `INFO_NEEDED` instead of guessing.
- When a request allows several readings, state the one you proceed on.
- Replace legacy subsystems instead of extending them: provide every symbol in the new implementation, then drop the old files from the build. Never add hooks, flags or init calls to the code being replaced.

## Sources of truth

| Question | Where the answer comes from, in order |
|---|---|
| Prototypes, structures, enums, flags, GUIDs | The official Windows SDK and WDK headers, kernel mode included. Check every SDK version available before calling a definition missing. |
| Layouts and offsets the headers do not publish | Windows 11 public symbols, then a Windows 11 test run that dumps them. |
| API contracts and behavior | Microsoft documentation and documented status codes, then black-box runs on Windows 11. |
| Undocumented user-mode shapes and behavior | Wine. |
| How hardware has to be driven | The hardware specification, then the Linux and EDK II drivers for the same part. |

- Say which source a definition came from.
- ReactOS's own headers show what ReactOS declares today, not what is right. Check them against the sources above.
- Never transcribe disassembled or decompiled Windows code. Use Windows 11 as a black box. Inspect the Windows 11 binaries in the cleanroom store only when there is no way around it: no header, public symbol, documentation or Windows 11 run answers the question. Use what you read only to understand the behavior, never as the implementation: the Windows code itself is not necessarily correct. Record only the fact you needed (a layout, offset, constant or behavior), never the code.
- Never copy GPL code from Linux. Study the technique and reimplement it.
- Never use legacy ReactOS code (the ARM3 memory manager, NT4 and NT5-era designs) as a reference.

## Compatibility criteria

- Implement NT10/Windows 11 semantics unconditionally. Do not add version-gated NT5 paths or keep NT5 defaults for compatibility.
- Judge behavior, performance and timing against Windows 11 and Linux, not against an ideal. A delay or retry is a bug only when Windows 11 and Linux avoid it and no specification requires it. Classify each finding as required by the specification, matching parity, or excess to fix.
- Where the documentation is silent, pick the documented-safe behavior or report `INFO_NEEDED`. Validation checks observed on Windows 11 may be kept when the documentation gives nothing.

## Kernel structures and ABI

- New kernel structure state goes in the field Windows 11 defines for it, at the offset its public symbols give. Fill in missing intermediate members from the symbols and add ARM64 `C_ASSERT(FIELD_OFFSET(...))` checks.
- Never add ReactOS-private tail fields (`#if defined(__REACTOS__)`) to Windows structures. If Windows 11 has no field for the state, report `INFO_NEEDED`.
- For every ARM64 layout decision (field, offset, size, constant, intrinsic, generated assembler offset), use the first evidence that exists: the public headers, Windows 11 public symbols, a Windows 11 runtime dump, or an existing ReactOS consumer. Reject a change that has none.
- A field backed only by an existing ReactOS consumer is ReactOS-private scaffolding. Keep it stable until that code moves to the Windows field.
- A field missing from the public SDK is private, not gone. Delete a field only after proving that nothing consumes it and Windows does not need it.
- Each structure needs its own evidence. Verifying one field does not verify its neighbors.
- Every generated assembler offset maps to a validated C field.
- Layout test expectations come from the same evidence, never invented. When ReactOS lacks a field Windows has, keep the check visible as a logged note instead of deleting it.
- When Windows 11 has something ReactOS lacks, record the gap. Do not silently rewrite Windows-mirrored headers.
- Parity is not a clean build, a boot or a passing ReactOS test; those tests check their own constants.
- Use the correct calling conventions and annotations, follow the existing error handling (`NTSTATUS`, `EFI_STATUS`), and respect IRQL, locking and paging constraints in kernel code.

## Architecture neutrality

- Keep common code architecture neutral and suitable for existing ports and the RISC-V64 port.
- No architecture `#if`/`#ifdef` in common code: kernel, rtl, ntdll, kernel32, CRT, memory manager and WoW64. Add a hook per architecture in the architecture layer, with a no-op or the existing behavior on the other architectures, and call it unconditionally. Use `sizeof`, not `_WIN64`, for pointer width.
- If Windows does something on every architecture, implement it on every architecture.
- Architecture-specific files need no architecture gates.
- Outside the architecture layer, only Windows ARM64 structure layouts and the x86 emulator backend (FEX on ARM64) may be ARM64-specific.
- ARM64EC/CHPE code is still gated to ARM64. A later RV64EC target will reuse it, so do not treat it as ARM64-only by design, and do not generalize the gates before that work starts.
- WoW64 means a 64-bit host running 32-bit code, independent of the host architecture. Build it for every 64-bit target, never for a list such as "amd64 or arm64". Only the x86 CPU backend is host-specific.
- Keep debug logging architecture neutral so runs can be compared across architectures.
- Before finishing, build amd64 and i386 as well as arm64.
- Do not use common code for experiments or application-specific workarounds. Add special cases only when their necessity is demonstrated.
- Board neutrality applies to common components such as the HAL, the kernel and bus drivers. A dedicated hardware driver is the right home for board knowledge; never delete a working driver as board cleanup.
- ARM64 is weakly ordered. Express memory ordering with explicit barriers or acquire/release operations; never assume it.

## Memory manager

- The memory manager (NVS, the NT Virtual Memory Subsystem) and the cache manager are clean-room implementations. Derive their contracts from Microsoft documentation, the WDK headers and documented status codes.
- Architecture knowledge goes through the memory manager's architecture descriptor and hooks (`MI_ARCH_DESCRIPTOR`, `MiArch*`), never through `#if`.
- The kernel directory's `AGENTS.md` has the remaining rules.

## Shell and UI

- Icons and glyphs are genuine Fluent UI System Icons used unmodified: the nearest existing asset in the right weight and size. Never author, compose, overlay, shift or gamma-adjust them.
- Tray icons stay at 18 px. Windows 11 tray parity covers layout, grouping, the clock and overflow, not icon size.

## Testing against Windows 11

- Before changing behavior to satisfy a ReactOS test, run the same test on Windows 11 and compare per-check results, not totals. Many ReactOS apitests are not Windows 11 clean.
- If Windows 11 fails a check too, the expectation is the bug.
- Run the Windows 11 baseline once per test binary and reuse its results. Rerun it only when the test binary changes in a way that alters results, and batch those reruns.
- Use Windows 11 as a black box only: observable status codes, outputs and sizes.
- Never modify the Windows 11 reference image; use a throwaway overlay for each run.
- Compare under equivalent privileges, and note when one side runs as an administrator and the other as SYSTEM.
- A virtual machine proves only that a driver installs and loads. Binding and behavior need the real hardware.

## Testing in a VM

- Reuse the existing build directories and artifacts. Do not duplicate build caches, executables or disk images, or create per-test, per-session, dated or prefixed output paths.
- Run builds with `CCACHE_DISABLE=1`; compiler caching is disabled to avoid duplicate artifacts.
- Keep one working log at `/tmp/freeldr_arm64.log`, overwritten in place. Use the existing build's `ReactOS.img` and the fixed test payload `/Users/HOME/reactos-scripts/appdisk/out/apps.img`. Pass the raw payload image to `--apps`; passing a directory creates additional cached images and overlays.
- Run `vm_monitor.py` with `--timeout 30 --stall 6`. Never use a timeout above 30 seconds or a stall timeout above 6 seconds.
- Run without a display by setting `ROS_QEMU_DISPLAY=none`; omit screen arguments.
- Never rebuild or repack a disk image while a VM is using it.
- Read above the given error, as the cause often appears well sometimes before the obvious error.
- Silence is not always a hang. Without a debugger backtrace, first check whether the boot is slow but progressing by tracing the next region. A quiet log once the desktop is up is an idle desktop.
- note that there is kdgdb in case of stall to attach live to the OS and debug using gdb client, like gdb multiarch
- Compare benchmark numbers only between runs made the same way (headless, same CPU count, host not throttling the VM), and A/B against the baseline build before blaming a change.

## Verifying a fix

- A fix counts only after a real boot or test run shows it working. A clean build proves nothing about behavior.
- Fix a broken change in place. Never build on a change that does not work, and never set it aside to test something else.
- Revert a fix proven ineffective instead of layering another on it.
- For races, lost wakeups, deadlocks, lock ordering and hot paths: hold the first hypothesis loosely, get an independent adversarial review, accept only facts verified in the source, and confirm them with a real boot. Treat every second opinion, and your own conclusion, as claims to verify.

## Code style

- No comments in code diffs. Leave existing comments where they are instead of moving or re-adding them.
- New files start with this header. `PROJECT` names the component, for example "LiberNT API tests" or "LiberNT Kernel"; tests use GPL-3.0-or-later.

```c
/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     <one line>
 * COPYRIGHT:   Copyright <year> <name> <email>
 */
```

## Changes and commits

- Leave changes uncommitted unless the user explicitly requests a commit.
- Keep doubtful or incomplete changes unstaged, and remove temporary diagnostics before committing.
- Use a `[COMPONENT]` subject and a body with `Problem:`, `Why:`, and `How:` paragraphs separated by blank lines. For ABI and layout changes, name the evidence.
- For short feature commits, keep the same `[COMPONENT]` subject and use a body beginning with `feat:` instead of the Problem/Why/How format. Put `feat:` in the description, never in the title. Separate the title and body with a blank line. Keep the body to two or three brief lines, respecting the user's requested length.
- When the diff is small and speaks for itself, such as a wording, typo or formatting change, commit with the `[COMPONENT]` title only. Leave out the `Problem:`/`Why:`/`How:` or `feat:` body when you judge that the title says everything.
- Fix a committed mistake with a follow-up commit; never rewrite published history.
- Include related AGENTS.md updates in a relevant change commit; do not create a separate commit just for those instructions.
