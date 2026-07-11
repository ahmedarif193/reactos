#!/usr/bin/env bash
#
# kmwin11new Win11 ARM64 validation loop  (PROVEN recipe, ~90s/run)
# ----------------------------------------------------------------
# Boots Win11 ARM64 headless under QEMU, runs the given kmtest suites, prints
# per-test results (the ok_eq_* macros print expected-vs-actual on mismatch).
#
#   ./run-win11-cycle.sh Win11NewKM [MoreSuite ...]
#
# What was learned the hard way (do NOT regress these):
#   * hdiutil wedges this host -> build the FAT payload with mtools, never hdiutil.
#   * In-guest New-SelfSignedCertificate is slow/flaky -> HOST-sign the driver with
#     osslsigncode + ReactOSKmtest.key; the guest only imports ReactOSKmtest.cer.
#   * A freshly-built kmtest_.exe is subsystem 10.0 and dies on Win11 with
#     0xc000007b -> use the known-good subsystem-5.1 runner (job-verify). It pulls
#     the test list from the driver via IOCTL, so new tests in OUR driver still run.
set -euo pipefail

ROS="${ROS:-/Users/mac/working_dir/reactos-arm64-ABI-Exports}"
OUT="${OUT:-$ROS/output-Clang-arm64-debug}"
REF="${REF:-/Users/mac/working_dir/winarm64-installed-reference}"
DISK="${DISK:-/Users/mac/working_dir/winarm64-raw-dev/out/win11-arm64-full.qcow2}"
FW="${FW:-/opt/homebrew/share/qemu/edk2-aarch64-code.fd}"
SK="${SK:-$REF/work/kmtest-signing}"
GOODRUNNER="${GOODRUNNER:-$REF/work/job-verify/kmtest_.exe}"
WORK="${WORK:-/tmp/kmwin11-cycle}"
TIMEOUT="${TIMEOUT:-420}"
KMDIR="$OUT/modules/rostests/kmtests"
[ "$#" -ge 1 ] || set -- Win11NewKM

echo "[cycle] building kmtest_drv (ENABLE_ROSTESTS=ON required)..."
( cd "$OUT" && ninja ntoskrnl kmtest_drv )

PD="$WORK/payload"; rm -rf "$PD"; mkdir -p "$PD"
cp "$GOODRUNNER" "$PD/kmtest_.exe"                       # known-good subsystem-5.1 runner
cp "$KMDIR/kmtest_drv.sys" "$PD/kmtest_drv.sys"          # OUR driver (has the new tests)
cp "$SK/ReactOSKmtest.cer" "$PD/"
python3 "$REF/scripts/patch-kmtest-pe.py" "$PD/kmtest_drv.sys" >/dev/null    # Win11 PE fixups
osslsigncode sign -certs "$SK/ReactOSKmtest.crt" -key "$SK/ReactOSKmtest.key" -h sha256 \
    -in "$PD/kmtest_drv.sys" -out "$PD/kmtest_drv.signed.sys" >/dev/null 2>&1
mv "$PD/kmtest_drv.signed.sys" "$PD/kmtest_drv.sys"      # host-signed

printf 'kmwin11new\n' > "$PD/jobid.txt"
printf 'job.cmd\n'    > "$PD/launcher.txt"
if [ -n "${TESTS_FILE:-}" ] && [ -f "$TESTS_FILE" ]; then
    tr -d '\r' < "$TESTS_FILE" | sed '/^[[:space:]]*$/d' > "$PD/tests.txt"
    set -- $(cat "$PD/tests.txt")
else
    : > "$PD/tests.txt"; for t in "$@"; do printf '%s\n' "$t" >> "$PD/tests.txt"; done
fi
cat > "$PD/job.cmd" <<'CMD'
@echo off
setlocal EnableExtensions EnableDelayedExpansion
set SERIAL=\\.\Global\org.qemu.rawserial.0
set /p JOBID=<jobid.txt
echo raw-arm64-job[%JOBID%]: BEGIN > %SERIAL% 2>nul
certutil.exe -addstore -f Root ReactOSKmtest.cer >nul 2>nul
certutil.exe -addstore -f TrustedPublisher ReactOSKmtest.cer >nul 2>nul
kmtest_.exe stop >nul 2>nul & kmtest_.exe delete >nul 2>nul
set /a FAILS=0
for /f "usebackq tokens=* delims=" %%T in ("tests.txt") do (
    if not "%%T"=="" (
        echo raw-arm64-job[%JOBID%]: RUN %%T > %SERIAL% 2>nul
        kmtest_.exe %%T > "kmtest-%%T.log" 2>&1
        set RC=!ERRORLEVEL!
        findstr /c:"Test failed:" "kmtest-%%T.log" >nul 2>nul && set RC=1
        if not "!RC!"=="0" set /a FAILS+=1
        echo raw-arm64-job[%JOBID%]: EXIT %%T !RC! > %SERIAL% 2>nul
    )
)
echo raw-arm64-job[%JOBID%]: DONE fails=!FAILS! > %SERIAL% 2>nul
exit /b 0
CMD

# --- MBR + FAT32 payload via mtools (no hdiutil) ---
IMG="$WORK/payload.raw"; OFF=$((2048*512)); rm -f "$IMG"
python3 - "$IMG" <<'PY'
import struct,sys; img=sys.argv[1]; S=512; T=262144; PS=2048; PC=T-PS
open(img,"wb").truncate(T*S)
mbr=bytearray(512); mbr[446:462]=struct.pack("<B3sB3sII",0x80,b"\xfe\xff\xff",0x0C,b"\xfe\xff\xff",PS,PC)
mbr[510]=0x55; mbr[511]=0xAA; open(img,"r+b").write(mbr)
PY
mformat -i "$IMG@@$OFF" -F -H 2048 -v RAWPAYLOAD ::
mcopy -i "$IMG@@$OFF" "$PD"/* ::

# --- boot headless; poll virtio-serial for DONE ---
RUN="$WORK/run"; rm -rf "$RUN"; mkdir -p "$RUN"; VS="$RUN/virtio-serial.log"; MON="/tmp/w11c.$$.sock"
pkill -f qemu-system-aarch64 2>/dev/null || true; sleep 1
qemu-system-aarch64 -machine virt,highmem=on -accel hvf -cpu host -smp 4 -m 4096 -bios "$FW" \
  -device ramfb -device virtio-gpu-pci,disable-legacy=on,xres=1280,yres=800 \
  -device qemu-xhci -device usb-kbd,port=1 -device usb-tablet,port=2 \
  -drive if=none,id=payload,format=raw,cache=writeback,aio=threads,file="$IMG" -device usb-storage,drive=payload,port=4 \
  -drive if=none,id=osdisk,format=qcow2,cache=writeback,aio=threads,file="$DISK" \
  -device nvme,drive=osdisk,serial=RAWARM64FULL,bootindex=0 \
  -device virtio-serial-pci-non-transitional,id=vs0 -chardev "file,id=rs,path=$VS" \
  -device virtserialport,bus=vs0.0,chardev=rs,name=org.qemu.rawserial.0 \
  -serial "file:$RUN/serial.log" -monitor "unix:$MON,server,nowait" -display none &
QP=$!; el=0
while kill -0 "$QP" 2>/dev/null && [ "$el" -le "$TIMEOUT" ]; do
  sleep 10; el=$((el+10))
  grep -qi 'raw-arm64-payload: DONE' "$VS" 2>/dev/null && { echo "[cycle] DONE @${el}s"; sleep 2; break; }
done
printf 'system_powerdown\n' | nc -U "$MON" >/dev/null 2>&1 || true
for _ in $(seq 1 20); do kill -0 "$QP" 2>/dev/null || break; sleep 1; done
printf 'quit\n' | nc -U "$MON" >/dev/null 2>&1 || kill "$QP" 2>/dev/null || true; wait "$QP" 2>/dev/null || true

# --- keep logs: archive every run, timestamped ---
LOGDIR="${LOGDIR:-/Users/mac/working_dir/win11-kmtest-logs}"
STAMP="$(date +%Y%m%d-%H%M%S)"
ARCH="$LOGDIR/runs/$STAMP"; mkdir -p "$ARCH"
cp "$VS" "$ARCH/virtio-serial.log" 2>/dev/null || true
cp "$RUN/serial.log" "$ARCH/serial.log" 2>/dev/null || true
for t in "$@"; do mtype -i "$IMG@@$OFF" "::/kmtest-$t.log" 2>/dev/null > "$ARCH/kmtest-$t.log" || true; done

echo "==================== results ($STAMP) ===================="
grep -E 'raw-arm64-job' "$VS" 2>/dev/null | tee "$ARCH/markers.txt"
{
  echo "## $STAMP  suites: $*"
  for t in "$@"; do
    line=$(mtype -i "$IMG@@$OFF" "::/kmtest-$t.log" 2>/dev/null | tr -dc '[:print:]\n' | grep -iE "tests executed|Test failed")
    echo "- $t: ${line:-(no log)}"
  done
} | tee -a "$LOGDIR/run-history.log"
for t in "$@"; do
  echo "---- $t ----"
  mtype -i "$IMG@@$OFF" "::/kmtest-$t.log" 2>/dev/null | tr -dc '[:print:]\n' | grep -iE "tests executed|Test failed" || echo "(no log)"
done
echo "[cycle] logs archived: $ARCH"
