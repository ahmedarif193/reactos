# Windows NT PowerPC ABI reference probes

These tests run binaries from the port's compiler on Windows NT 4 PowerPC,
using the installed Windows kernel32, ntdll and MSVCRT. They link no ReactOS
startup, CRT, exception dispatcher or other runtime implementation. The probes
are PE32 PowerPC images with OS/subsystem version 4.0. They do not patch NT4
system binaries or use private runtime shims. The port's PPC public declarations
are corrected to match the NT4 contract without renumbering existing exports.

The same source is built at `-O0` and `-O2`. Eleven cases cover:

- `raise`: exception parameters, NT4 CONTEXT, filter captures, handler entry;
- `access`: a hardware write fault inside a compiler-generated SEH scope;
- `finally`: normal/abnormal termination, `__leave`, return, nested `goto`
  exits and nested unwinding, including returning to the caller afterward;
  64-byte-aligned frames and funclet register saves at four incoming alignments;
  nonvolatile floating-point registers across software and hardware exceptions;
- `continue`: continuing execution and searching an enclosing handler;
- `ntcontinue`: direct `NtContinue` system-call context restoration, including
  the saved PPC control context, resumption at the captured instruction and
  stack pointer, cross-DLL debug-output preservation of the caller TOC, and
  the caller TOC reload after the call. NT4's
  `RtlCaptureContext` records its own ntdll TOC in `Gpr2`, as seen in the
  supplied NT4 binary; the probe checks that separately from the caller TOC;
- `stack`: a 64 KiB frame, guard-page probing and exception unwinding;
- `thread`: Windows calling a compiler-generated callback and its SEH;
- `crt`: qsort callbacks, unwinding through the OS CRT, aggregate returns,
  mixed FP/integer arguments and variadic register/stack arguments;
- `ntdll`: native large-integer arguments and aggregate/scalar return behavior;
  NT4's one-argument function lookup, absolute function-table addresses and
  caller-PC `RtlVirtualUnwind` outputs, including the leaf-function path.
- `jump`: ordinary named `setjmp`/`longjmp` imports from the system MSVCRT,
  including a zero argument, a nonzero return value and a volatile local.
- `jumpseh`: extended `_setjmpex` and `longjmp` imports through a collided
  hardware-exception unwind, verifying both termination handlers and the
  resumed return value.

The ordered case list is shared by the probe, boot service, batch generator and
report, so a new case cannot silently disappear from one of those paths.
Each case runs in a separate process. A service runs both builds before logon,
records process exit codes, and applies a two-minute timeout per case. Individual
logs flush each check so that a crash retains the last reached test. A successful
result is `COMPLETE` with zero failures in **every** case log and zero exit codes
in the service log. Build success or ReactOS-only results do not establish NT4
compatibility. These probes also do not establish exhaustive ABI parity, C++
exception compatibility, or driver/DDI compatibility.

## Build and prepare

From the ReactOS source directory, after configuring the PPC build:

```sh
python3 modules/rostests/tests/ppcabi/build.py \
  --reactos-build output-Clang-ppc-debug \
  --output output-Clang-ppc-debug/bringup/nt4-probes

python3 modules/rostests/tests/ppcabi/prepare.py \
  --image /path/to/nt4-ppc-disk.img \
  --tests output-Clang-ppc-debug/bringup/nt4-probes \
  --output output-Clang-ppc-debug/bringup/nt4-validation
```

Preparation requires `mtools` and `reged`. It supports the supplied image's
FAT16 Windows volume in MBR partition 2 and its ControlSet001/ControlSet003.
It creates a separate disk, adds `C:\ppcabi` and the automatic LocalSystem
service `PpcAbiProbe`, then exports the new registry key to verify it. Existing
logon settings and OS binaries remain unchanged. The source image is hashed
before and after copying. Preparation refuses to overwrite an existing copy.
`build.json` and `preparation.json` record commands and binary/image hashes.

## Boot the supplied Gossamer installation

This particular disk contains the maciNTosh Gossamer HAL, so the ReactOS
`40p` QEMU command is unsuitable. The [port maintainer recommends
dingusppc-nt](https://github.com/Wack0/maciNTosh/issues/30) for this installation.
It needs a compatible G3 boot ROM supplied locally and the maciNTosh Old World
ARC firmware CD from the [firmware releases](https://github.com/Wack0/maciNTosh/releases).

```sh
python3 modules/rostests/tests/ppcabi/launch.py \
  --emulator /path/to/dingusppc-nt/build/bin/dingusppc \
  --rom /path/to/compatible-g3-bootrom.bin \
  --arc-iso /path/to/nt_arcfw_grackle_ow.iso \
  --prepared output-Clang-ppc-debug/bringup/nt4-validation
```

The launcher defaults to the beige G3 desktop machine `pmg3dt`; select a machine
matching the ROM with `--machine` when necessary. Firmware boot selection may
be needed on the first launch. The service starts automatically when NT boots.
No user password or automatic logon is needed.

After the emulator has stopped, extract logs from the copied disk:

```sh
mkdir -p output-Clang-ppc-debug/bringup/nt4-validation/results
mcopy -i output-Clang-ppc-debug/bringup/nt4-validation/disk01.img@@1048576 \
  '::/ppcabi/*.log' output-Clang-ppc-debug/bringup/nt4-validation/results/
```

The expected files are `runner.log` and twenty-two `abi-o[02].<case>.log` files.
Validate completeness and the recorded Windows 4.0 version with:

```sh
python3 modules/rostests/tests/ppcabi/report.py \
  output-Clang-ppc-debug/bringup/nt4-validation/results
```

The report also requires all twenty-two process exit codes to be zero and the
runner to finish. It reads `runner.log`, or `batch.log` for the manual batch.
It rejects missing or incomplete cases, any failed checks, and results
from a different OS version. For ReactOS 10.0 comparison logs, explicitly pass
`--os-major 10`; those results do not satisfy the default NT4 check.

For a manual run inside Windows, use `C:\ppcabi\run.cmd`, or run one case such
as `abi-o2.exe finally` from a writable directory. Keep the exact same executable
hashes when comparing Windows and ReactOS results.
