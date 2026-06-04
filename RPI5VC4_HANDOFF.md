# RPi5 (BCM2712) clean-room vc4/HVS XPDM display driver — session handoff

Goal: a clean-room ReactOS ARM64 XPDM display driver for the Raspberry Pi 5
(BCM2712) that drives the **HVS** (Hardware Video Scaler) hardware compositor —
GPU/HVS-powered, not the firmware GOP fallback — crediting the Linux drm/vc4
authors (Dave Stevenson, Dom Cobley, Maxime Ripard, Tim Gover) in file headers.

Driver lives in `win32ss/drivers/miniport/rpi5vc4/` (renamed from `rpi5fb`).
Service name `rpi5vc4`, LoadOrderGroup "Video Save", Tag 2, Start=1, paired with
the generic `framebuf.dll` display DDI. DbgPrint tag is `RPI5VC4:`.

Files:
- `rpi5vc4.c` / `rpi5vc4.h`   — videoport miniport (FindAdapter/Initialize/StartIO), device extension, private scanout buffer, cursor surface, private latch IOCTL.
- `rpi5vc4_hvs.c` / `.h`       — clean-room HVS: `Rpi5HvsBuildPlane()`, `Rpi5HvsInstallScanout()` (multi-plane dlist gen + install at live head), `Rpi5HvsMoveCursor()` (cursor-plane hot path), `Rpi5HvsFlipScanout()` (scanout address latch).
- `rpi5vc4_crtc.c` / `.h`      — clean-room PixelValve/CRTC: `Rpi5CrtcReportTiming()` (captures live raster timing), `Rpi5CrtcProgramCurrentTiming()` (same-mode PV programming).
- `rpi5vc4.inf`, `rpi5vc4_reg.inf`, `rpi5vc4.rc`, `CMakeLists.txt`.

================================================================================
## STATUS

DONE + hardware-verified:
- **M1 own HVS scanout (the real "black desktop" fix).** `Rpi5HvsInstallScanout`
  generates our own display list and writes it at the live LIST_PTR head. The
  scanout plane is built opaque: `CTL0.ALPHA_MASK = SCALER6D_CTL0_ALPHA_MASK_FIXED
  (3)` + `CTL2` fixed alpha 0xfff. Verified: CTL0 0x6000c007 -> 0x480cc007, edit
  sticks across re-reads, desktop renders **blue/opaque (not black)**. This is
  what fixed the "black until hover" symptom (HVS was honouring the source alpha
  byte; GDI leaves it 0 -> transparent -> black).
- **M5 first safety stage.** `Rpi5CrtcReportTiming` maps both PixelValves and
  captures the enabled firmware timing in the device extension. `Rpi5CrtcProgramCurrentTiming`
  writes the same timing words back to the selected live PV as a no-op ownership
  check. Latest serial run after this change reached shell/setup activity; no
  fatal/assert/DABORT was seen.
- **M3 hardware cursor visibility.** `framebuf.dll` now uses the video miniport
  hardware-pointer IOCTLs when available, and `rpi5vc4` composites the cursor as
  a second HVS plane. The cursor plane must use a distinct SCALER6D UPM slot in
  `PTR0`; without it the active list contains the cursor plane but the final HDMI
  pixels stay desktop blue. Verified on 2026-06-03: final screenshot shows the
  ReactOS cursor at the logged position; the sampled 32x32 cursor box changed
  from flat desktop blue to mixed cursor pixels.
- **M3 cursor fetch geometry.** The cursor backing surface is always 64 pixels
  wide even when the visible pointer shape is smaller, so the HVS cursor plane
  must use the fixed 64-pixel pitch. The HVS install path also clips cursors
  crossing the screen edge by advancing the source pointer instead of clamping
  only the destination. Verified on 2026-06-03: desktop still reaches shell/setup
  and the final HDMI frame shows a correctly composited cursor.
- **M3 cursor movement hot path.** `Rpi5HvsMoveCursor()` updates only the live
  cursor plane position/size/address for normal in-bounds cursor moves. Full HVS
  list generation remains the fallback for shape changes, enable/disable, and
  fully off-screen cursor positions. Partially clipped cursor moves now update
  the live cursor plane in place by advancing the source pointer and shrinking
  the visible plane.
- **M4 first stage.** The miniport allocates a private high-DRAM write-combined
  scanout buffer, copies the firmware GOP framebuffer into it, maps that buffer
  to `framebuf.dll`, and points the HVS scanout plane at the private physical
  address. Verified on 2026-06-03: trace shows `private framebuffer phys=...`
  and the final HDMI frame renders the ReactOS desktop/taskbar from the private
  scanout path.
- **M4 DDI latch path.** `framebuf.dll` now exposes `DrvSynchronize` and
  `DrvSynchronizeSurface`; they probe/cache an optional private
  `IOCTL_VIDEO_RPI5VC4_LATCH_SCANOUT`. `rpi5vc4` handles it by relatching the
  current scanout through `Rpi5HvsFlipScanout()`. This is the bridge needed for
  later double-buffer work; it does not yet allocate a second visible page.
- **M4 vblank-safe flip primitive.** `Rpi5HvsFlipScanout()` now fast-returns for
  same-buffer syncs, and waits for PixelValve VFP start before changing the HVS
  scanout address for a real future page flip.
- **M4 synchronization hardening.** `framebuf.dll` drains ARM64 framebuffer
  stores in `DrvSynchronize` / `DrvSynchronizeSurface` before asking the miniport
  to relatch scanout, so explicit GDI sync points are useful even when no draw
  hook runs immediately before them.
- **Framebuffer refresh hardening.** `framebuf.dll` now associates reserved-byte
  32bpp surfaces with its drawing hooks. Each hooked draw delegates to Eng, marks
  the touched reserved/alpha byte opaque, and drains ARM64 write-combined stores.
  Verified on 2026-06-03: boot reached desktop/setup activity through 32.6s with
  no `Fatal`, `DABORT`, assertion, or debugger entry, and late HDMI frames show a
  stable desktop/taskbar/cursor.
- **BOOTPROF cleanup.** Heartbeat thread + dump funcs removed from
  `ntoskrnl/ex/init.c` (lines were ~116-332 and ~2409-2428); all other `[BOOTPROF
  ...]` traces gated to `DPRINT` (silenced) EXCEPT the three kept benchmarks:
  `[BOOTPROF SYSMAP]` (sysldr.c), `[BOOTPROF XHCI]` (usbxhci.c), `[BOOTPROF
  USBSTOR]` (usbstor scsi.c). The 3 `[BOOTPROF 01/02/03]` in halarm64.c were
  deleted. `scripts/flash_test.py` stale-image guard (EXPECTED_BUILD_MARKER /
  seen_build_marker) was removed at the user's request.

IN PROGRESS — **visual validation / cleanup**:
- The basic hardware cursor is visible now and the in-bounds move path no longer
  rebuilds the full HVS list. Partially clipped cursor moves also stay on the
  hot path. Remaining work is to validate live mouse movement over real windows
  with a working HDMI capture and remove any remaining bring-up traces that are
  not useful production diagnostics.
- The multi-plane dlist installs correctly. Both planes are processed: the
  desktop scanout is visible everywhere and the HVS cursor plane contributes
  pixels over it.
- Fresh 2026-06-03 run: with the kept phase-1 `DbgPrint("5")`, the board passes
  the old `KdDriver` silence and reaches desktop/setup around 19-30s. `flash_test`
  still reports "stall" after 20s of serial quiet; validate visually and from
  traces, not the final result line.
- Refresh-hook run on 2026-06-03: `framebuf.dll` loaded at 19.49s, system cursors
  at 19.75s, setup/device install continued through 32.68s. The harness still
  reported serial-quiet "stall", but `flash_test_hdmi.png` shows a clean desktop
  with taskbar and cursor; frames 114-133 stayed stable.
- M3/M4/M5 run on 2026-06-03 after `Rpi5HvsMoveCursor`, latch IOCTL, and
  same-mode PV programming: serial reached `framebuf.dll` load at 19.24s, system
  cursors at 19.49s, device install/shell/setup work through 32.58s. No
  `Fatal`, `Assertion`, `DABORT`, or debugger entry. A repeat run with HDMI
  capture active produced a clean desktop/taskbar/cursor screenshot; the helper's
  final "stall" was serial quiet after desktop activity, not a crash boundary.
- M3/M4/M5 hardening run on 2026-06-03 after the vblank-safe flip primitive and
  PixelValve timing readback: serial reached DirectX active at 15.71s,
  `framebuf.dll` load at 19.21s, system cursors at 19.45s, and setup/shell work
  past 31.8s. No `Fatal`, `Assertion`, `DABORT`, or debugger entry appeared in
  `flash_test.log`; `flash_test_hdmi.png` shows a clean desktop/taskbar/cursor.
- M3 clipped-cursor + M4 sync hardening run on 2026-06-03: serial reached DirectX
  active at 15.66s, `framebuf.dll` load at 19.19s, system cursors at 19.44s,
  device install/setup activity through 32.39s, and no `Fatal`, `Assertion`,
  `DABORT`, debugger entry, or KDB prompt. The final HDMI screenshot again shows
  a clean desktop/taskbar/cursor; the helper's "stall" line is serial quiet.
- High-DRAM allocation alone was not the whole cursor bug. The functional change
  that made the cursor visible was assigning the cursor plane a separate UPM
  base/handle in `PTR0`.
- `rpi5vc4` advertises a 64x64 async colour pointer and handles
  `SET_POINTER_ATTR`, `SET_POINTER_POSITION`, `ENABLE_POINTER`, and
  `DISABLE_POINTER`; `framebuf.dll` queries those caps and uses the hardware path
  when available, with fallback to the existing Eng software pointer path.

NOT STARTED: M5 new HDMI mode-set/PHY/PLL ownership, M6 (V3D 2D accel), M7 (finalize/cleanup).

HISTORICAL TIMING BUG (not the display driver): earlier runs hit a ~22.6s
COM/theme page fault on a bad user-range pointer 0x41A3FC60, PC/LR in ntoskrnl
object-lock code. The latest refresh-hook run did not reproduce it; keep the note
as prior evidence only, not the current boundary.

================================================================================
## BUILD + VALIDATE WORKFLOW

From `output-Clang-arm64-debug/`:
- Build driver:   `ninja rpi5vc4`        (LSP "ntddk.h not found" diagnostics are
  spurious — the linter lacks kernel include paths; ninja compiles/links fine.)
- Build image:    `ninja livecd reactosimg`
- Hardware run:   `../scripts/flash_test.py > flash_test_run_console.log 2>&1`
  - ~3-4 min: builds image, flashes USB via HID relay, boots Pi5, captures serial
    + HDMI, powers off. **Run it in the background; never run two at once; never
    `pkill` it mid-flash** (corrupts the USB image -> the Pi boots a STALE image).
  - `MAX_TIMEOUT=80`, `STALL_TIMEOUT=20` are user-fixed constants — do NOT change.
  - **"Boot result: stall" is now a benign false-positive** (we removed the
    heartbeat that fed the stall detector; serial goes quiet at the desktop).
    Validate by traces + captured frames, NOT the success marker.
- Traces:   `grep -aE "RPI5VC4:" flash_test_run_console.log`
- Frames:   `flash_test_hdmi_frames/frame_%05d.png` (2fps; frame index/2 ≈ wall s;
  firmware adds ~7s before kernel). Helper scripts in the build dir:
  - `analyze_desktop.py <dir> <lo> <hi>`  full-frame + bottom-right watermark.
  - `analyze_cursor.py  <dir> <lo> <hi>`  samples the (300,300) 64x64 cursor region
    (top half / bottom half) — expects top=GREEN bot=BLUE when the cursor works.
  - `analyze_fill.py    <dir> <lo> <hi>`  top/bottom half colour (alpha experiment).
  Real ReactOS desktop = blue (mean RGB ~ (6,6,97) early, ~(42,117,192) later).
  SMPTE colour bars = NO HDMI signal (Pi off), not a render.
- Capture technique used on 2026-06-03: start HDMI capture before powering the Pi,
  let `flash_test.py` keep frames until shutdown, then sample late frames after
  setup/shell is visible. For cursor bring-up use `analyze_cursor.py
  flash_test_hdmi_frames 90 132`; if top and bottom both stay desktop blue while
  serial says `PTR1=0x40000000`, the HVS list is installed but the cursor plane is
  not contributing pixels.
- Symbolize a kernel crash PC: `llvm-addr2line-XX -fe symbols/ntoskrnl.exe <VMA>`
  where `VMA = 0x400000 + (PC - ntoskrnl_runtime_base)`. ntoskrnl ImageBase is
  0x400000 (relocated at runtime to ~0xFFFFF80002400000; re-derive per boot — the
  mminit log prints "Boot Loaded Image" region, and X18 in the crash dump = KPCR).
  GNU addr2line/objdump can't parse the aarch64 PE; use the llvm-* tools.

================================================================================
## HARDWARE FACTS (clean-room, from vc4 register docs + on-silicon dumps)

HVS (SCALER6 / D-step SCALER6D), base `0x107C580000`, len 0x1A000:
- CONTROL `+0x20` (bit31 HVS_EN). CXM_SIZE `+0x04` (dlist RAM dwords, =0x1000).
- dlist RAM at `+0x4000`. Active head = per-channel LIST_PTR HEADE (bits 11:0):
  C-step DISP0_LPTRS `+0x3c`, D-step DISP0_LPTRS `+0x110`. This board is D-STEP
  (C reads 0xffffffff); head = 0x2a0. Commit/re-latch by re-writing LPTRS.
- Emitted unscaled plane element = 9 dwords, order:
  `CTL0 POS0 CTL2 POS2 CTX PTR0 PTR1 PTR2 END`. `CTL0.NEXT` (bits 29:24) =
  dword offset to the next list element (=9 for the current back-to-back planes).
  - CTL0: END b31, VALID b30, NEXT b29:24, ADDR_MODE b22:20 (0=linear),
    ALPHA_MASK b19:18 (0=use per-pixel src alpha; **3 = D-step fixed alpha**),
    UNITY b15, ORDERRGBA b14:13, PIXEL_FORMAT b4:0.
  - POS0: START_Y b28:16, START_X b12:0.
  - CTL2: ALPHA_MODE b31:30 (0=fixed, 1=pipeline/per-pixel), ALPHA value b15:4
    (0xfff=opaque).
  - POS2: SRC_LINES(h-1) b28:16, SRC_WIDTH(w-1) b12:0.
  - CTX: init 0xc0c0c0c0 (HVS overwrites with per-frame state at runtime — this
    is why an old marker-scan for 0xC0C0C0C0 found nothing).
  - PTR0: VFLIP b31, UPPER_ADDR (addr[39:32]) b7:0.  PTR1: addr[31:0].
    PTR2: PITCH (bytes) b16:0.
  - Pixel format 7 = RGBA8888; ORDERRGBA 2 = BGRA (matches the BGRX GOP fb).
    The alpha byte is the top byte (byte3) of the little-endian pixel.
- Firmware GOP fb: phys `0x3F400000`, 1920x1080, pitch 7680, BGRX. Reserved in
  the loader (`boot/freeldr/.../uefimem.c`) as LoaderFirmwarePermanent (invisible).

PixelValve (timing generator), PV0 `0x107C410000`, PV1 `0x107C411000`, len 0x100:
- CONTROL 0x00 (EN b0), V_CONTROL 0x04 (VIDEN b0, INTERLACE b4),
  HORZA 0x0c (HBP b31:16 | HSYNC b15:0), HORZB 0x10 (HFP | HACTIVE),
  VERTA 0x14 (VBP | VSYNC), VERTB 0x18 (VFP | VACTIVE), HACT_ACT 0x30, STAT 0x2c.
- Live read: PV0 EN/VIDEN, HACTIVE=960 (x2px/clk = 1920), VACTIVE=1080.

Other display nodes (CPU phys, +0x10_00000000 SoC offset already applied):
- hdmi0 ~0x107C701000 (+ regions 0x107C701400/0x107C701d00/0x107C702000/
  0x107C703800/0x107C704000/0x107C700100/0x107D510800/0x107C720000),
  hdmi1 ~0x107C706000+, mop 0x107C500000, dvp 0x107C700000.
- V3D DTB node is `/axi/v3d@2000000`:
  - compatible `brcm,2712-v3d`, status `disabled`.
  - `hub` phys 0x1002000000 len 0x4000, `core0` phys 0x1002008000 len
    0x6000, `sms` phys 0x1002030800 len 0x700.
  - interrupts `<0 250 4>` and `<0 249 4>`.
  - power/reset/clock refs exist (`power-domains`, `resets`, `clocks`), so M6
    must first own V3D power/reset/clock enablement before touching CLs.

================================================================================
## REMAINING MINI-TASKS (end to end) + VALIDATION

### M3 — validate live hardware cursor movement (HVS 2nd plane).
1. VALIDATE: boot, move the mouse on hardware, and inspect HDMI frames over
   windows/text. The cursor must track through `DrvMovePointer` ->
   `IOCTL_VIDEO_SET_POINTER_POSITION` and composite via the HVS without leaving
   black/redraw artifacts.
2. If the cursor flickers or stale pixels remain, instrument only the failing
   path: position updates, HVS relatch, and whether `Rpi5HvsInstallScanout()` is
   being called for each visible move. Do not re-add broad boot diagnostics.

### M4 — page-flip/vsync follow-up.
- The private framebuffer first stage is validated, and framebuf now has a
  DDI-side synchronization path to request an RPi5VC4 scanout latch. Full
  double-buffering still needs an alternate buffer and a vblank-safe latch point.
- `Rpi5HvsFlipScanout()` now verifies that the active head still points at the
  current scanout buffer before updating it, and preserves non-address `PTR0`
  fields while changing only the DMA address bits.

### M5 — own PixelValve + HDMI mode-set (clean-room). [large + HW-risky]
- Same-mode PV timing programming is implemented and reached shell/setup with
  clean HDMI output. The programming path now reads back the written timing words
  and exposes a bounded VFP-start wait for vblank-safe HVS address latching. Only
  after this should a new mode be attempted.
- For a new target mode, extend `rpi5vc4_crtc.c`: write HORZA/HORZB/VERTA/VERTB,
  V_CONTROL (VIDEN/INTERLACE), CONTROL (EN, FIFO, CLK_SELECT_DPI_SMI_HDMI=1) from
  the requested timing.
- HDMI controller + PHY + PLL bring-up is the hard part (regions under
  0x107C701000 for hdmi0). Study vc4_hdmi.c / vc4_hdmi_phy.c register facts.
  KEEP the firmware mode as a fallback; a wrong PHY/PLL sequence blanks HDMI.
- VALIDATE: after a self-programmed mode-set, HDMI must stay lit at the requested
  resolution (capture a frame). Start by re-confirming the existing 1920x1080.

### M6 — V3D 2D acceleration for GDI. [very large, multi-session]
- V3D is the tiled 3D GPU. 2D-accelerating GDI (DrvBitBlt/CopyBits/fills) means
  building V3D control lists (binner + renderer), tile state, and simple shaders,
  plus a paired accelerated DDI. Read the bcm2712.dtsi `v3d@` base + the Linux
  v3d/vc4 driver register facts. This is realistically its own project; scaffold
  the register block first, then a single accelerated fill as proof.
- VALIDATE: an accelerated solid fill / blit produces correct output and is faster
  than the software path (instrument with the kept BOOTPROF-style timing).

### M7 — finalize.
- Trim the verbose `RPI5VC4:` HVS/CRTC DbgPrints to concise production logging; remove
  `media/boot/rpi/config.txt` `framebuffer_ignore_alpha=1` if confirmed unused;
  keep the clean-room credit headers. `git add` only (user handles commits;
  the only permitted git op is `git add`).

================================================================================
## GOTCHAS
- Never run two flash_test.py at once; never pkill it mid-flash (stale USB image).
- Don't change flash_test timeouts/VID:PIDs/capture constants.
- Never `make clean`; incremental builds only. Only git op allowed is `git add`.
- HVS edits to the firmware's dlist STICK (firmware doesn't revert them) — but we
  now write our OWN full list at the head, so we own it.
- Cursor and scanout buffers must be HVS-readable DRAM. Current code allocates
  both at/above 0x40000000 and verifies the HVS list points at the expected
  scanout before updating it.
