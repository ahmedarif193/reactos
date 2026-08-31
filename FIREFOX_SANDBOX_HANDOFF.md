# Firefox 157 ARM64 sandbox handoff

Date: 2026-08-31

## Outcome

Official Mozilla Firefox Nightly 157.0a1 for Windows ARM64 launches on the
ReactOS ARM64 build and renders the first-run window.  The result is a
compatibility-mode smoke test, not Windows 11 security parity: Firefox's child
process sandboxes are disabled, and navigation to a web page was not tested.

No Firefox-specific ReactOS source changes were retained, committed, or
pushed for this result.  The Firefox payload, launcher, image, logs, and
screenshot are build-local artifacts.

## Tested Firefox artifact

- Version: Firefox Nightly 157.0a1
- Build ID: `20260829211045`
- Mozilla source revision: `c64776bf9a03d7baae8c7aaca9e133ce1437f5ed`
- Target: `aarch64-pc-mingw32`
- Archive: `firefox-157.0a1.en-US.win64-aarch64.zip`
- Archive SHA-256: `33d4f990fd9b4f1c86dc27d9506d49ce76d5e1224b238205e3c2edcdb89572d0`
- `firefox.exe` SHA-256: `0b1f86120691aedbe8c2612c9ca56eaee0389dfe7b6aba9b32648edec034ab97`

Build-local paths:

- Firefox directory: `output-Clang23-arm64-debug/firefox-ci-arm64-157.0a1-20260829211045/`
- Launcher: `output-Clang23-arm64-debug/firefox-arm64-boot.cmd`
- Preinstall manifest: `output-Clang23-arm64-debug/firefox-arm64-preinstall.lst`
- Screenshot: `output-Clang23-arm64-debug/Firefox-157.0a1-ReactOS-ARM64.png`
- Successful-run log: `/tmp/reactos-firefox157-minimal.log`

The successful image contained the exact `firefox.exe` above, installed the
launcher as `Profiles/Default User/My Documents/app_launcher.cmd`, did not
contain the previous Claude payload, and retained approximately 263 MiB of
free NTFS space.

## What the Firefox process sandbox does

Firefox uses multiple processes for content, graphics, networking, media
decoding, and utility work.  On Windows, the parent process acts as a broker
while child processes run with reduced operating-system authority.  Depending
on the process type and Firefox version, the restrictions use mechanisms such
as restricted tokens, integrity levels, job objects, access-control checks,
and brokered operations.

The boundary limits the damage from a compromised renderer or media process.
It is intended to prevent compromised web content from freely reading files,
changing the registry, launching programs, or controlling unrelated
processes.

## Compatibility settings used by the successful run

The launcher sets:

```text
MOZ_CRASHREPORTER_DISABLE=1
MOZ_DISABLE_CONTENT_SANDBOX=1
MOZ_DISABLE_GMP_SANDBOX=1
MOZ_DISABLE_GPU_SANDBOX=1
MOZ_DISABLE_RDD_SANDBOX=1
MOZ_DISABLE_SOCKET_PROCESS_SANDBOX=1
MOZ_DISABLE_UTILITY_SANDBOX=1
MOZ_NO_REMOTE=1
```

Firefox therefore displays the expected warning that the security sandbox is
disabled and the configuration is unsupported.  Child processes can still
exist, but they do not have the normal Windows sandbox restrictions.  Do not
use this configuration for untrusted browsing.

The isolated Firefox profile also contains:

```text
user_pref("widget.windows.windowsappsdk.enabled", false);
```

This preference is separate from the process sandbox.  It disables Firefox's
optional Windows App SDK title-bar path and avoids loading
`Microsoft.Internal.FrameworkUdk.dll` and its currently missing dependency
chain (`Bcp47Langs.dll`, `CoreMessaging.dll`, and `dcomp.dll`).

## Runtime evidence

The successful QEMU ARM64 run used four virtual CPUs and 4 GiB of RAM.  It
reached these launcher markers:

```text
FIREFOX_BOOT_BEGIN
FIREFOX_LAUNCH_OK
APPLAUNCH_DONE
```

After 45 seconds, the framebuffer showed Firefox Nightly's complete first-run
"Welcome to Firefox" interface.  The successful minimal run had no
process-scoped loader failure, hard-error dialog, or exception in the captured
log.

This proves initial process startup and UI rendering only.  It does not prove:

- navigation, DNS, TLS, downloads, audio, video, WebGL, or sustained browsing;
- Firefox 157 operation with any process sandbox enabled;
- Windows 11 security or behavioral parity;
- the optional Windows App SDK integration path.

## Known sandbox boundary

Earlier ARM64 tracing of a sandbox-enabled Firefox build found a CSRSS-like
process terminating a Firefox utility process with
`STATUS_ABANDONED_WAIT_0` shortly after Firefox logged `Building sandbox for
utility process`.  The trace did not implicate Firefox job-object teardown.
Several threads remained under `RtlWaitOnAddress` / `WrAlertByThreadId`.

That investigation remains unresolved.  The next discriminating work should
trace the CSRSS exception/termination path and the alert-by-thread-ID wakeup
semantics before changing Firefox or broadly adding loader shims.

## Required parity workflow for a real fix

1. Enable one Firefox child-process sandbox at a time and identify the first
   failing process type and exact terminal status.
2. Reduce that failure to the smallest public Windows API, token, job,
   process, IPC, or synchronization contract.
3. Run the same architecture-correct probe on native Windows 11 ARM64 and
   ReactOS ARM64.
4. Implement the ReactOS behavior without weakening assertions or disabling
   security checks.
5. Rebuild the affected binary and image, then rerun the paired probe.
6. Rerun Firefox with that sandbox enabled and require an explicit marker,
   a live rendered window, and clean process-scoped error scanning.
7. Repeat for content, utility, socket, GPU, RDD, and GMP sandboxes.

The Firefox sandbox work is complete only after the intended sandboxed child
processes run successfully and the end-to-end browser workload passes without
the `MOZ_DISABLE_*_SANDBOX` overrides.
