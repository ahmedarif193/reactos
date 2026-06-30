# Canonical Display Driver (cdd.dll) — Cleanroom Design Report

**Scope.** This report describes, from *public* WDDM / GDI-DDI documentation and general
display-architecture knowledge (no disassembly, no proprietary source), what the Windows
"Canonical Display Driver" does and how ReactOS can implement an equivalent — possibly
cleaner — driver. All internal names proposed here are **ours**, not Microsoft's; the module
is referred to as `cdd.dll` only because that is the value an INF may place in
`InstalledDisplayDrivers` (a free-form string), not because any Microsoft symbol is reused.

---

## 1. What the canonical display driver is (public architecture)

Before WDDM (i.e. under XDDM), every GPU vendor shipped a kernel-mode **GDI display driver**
that exported the `Drv*` DDI (`DrvEnableDriver`, `DrvEnablePDEV`, `DrvEnableSurface`,
`DrvBitBlt`, `DrvTextOut`, …). GDI in win32k called those entry points to paint the screen,
and the driver owned the framebuffer.

WDDM split a display driver into a **user-mode display driver (UMD)** and a **kernel-mode
miniport (KMD)** that both speak the `DXGKDDI_*` contract to **dxgkrnl**. GDI itself was *not*
rewritten for WDDM. Instead Microsoft shipped **one vendor-independent GDI display driver**
that sits on top of the WDDM stack and presents through it. That driver is the canonical
display driver. Its job is to be the `Drv*`-DDI surface GDI/win32k still expects, while doing
all of its actual scan-out through the **D3DKMT** client API into **dxgkrnl** → the vendor
miniport. It is "canonical" because it is the same for every adapter — the vendor-specific
part lives entirely below it (miniport + UMD).

So the canonical display driver is a **bridge**: legacy GDI `Drv*` on top, WDDM `D3DKMT` on
the bottom. The Desktop Window Manager (DWM) composes the desktop and uses this driver's
primary surface as the thing it scans out / textures.

**Key consequence for ReactOS:** we already have *both* sides of the bridge —
- the GDI `Drv*` side is exactly what `win32ss/drivers/displays/framebuf` already implements;
- the `D3DKMT`/WDDM side is exactly what our `dxgkrnl` + `softgpu` already implement.

cdd is the missing connective tissue, not a new subsystem.

---

## 2. The two interfaces cdd straddles

### 2a. Upward: the GDI display DDI (to win32k/GDI) — public DDK
win32k loads the driver named in `…\Services\<gpu>\Device0\InstalledDisplayDrivers`, calls
its `DrvEnableDriver`, and gets a `DRVENABLEDATA` (a `DRVFN[]` table of index→function). The
indices and prototypes are **public** (`winddi.h`). The minimum a *non-accelerated* driver
must provide (framebuf already does exactly this):

| DDI | Purpose | ReactOS strategy |
|-----|---------|------------------|
| `DrvEnableDriver` | hand GDI the DDI table + driver version | static `DRVFN[]`, our names |
| `DrvEnablePDEV` / `DrvCompletePDEV` / `DrvDisablePDEV` | describe the mode (w/h/bpp, `GDIINFO`/`DEVINFO`) | fill from the committed WDDM mode |
| `DrvEnableSurface` / `DrvDisableSurface` | create the primary drawing surface | **the one real difference** — see §3 |
| `DrvAssertMode` | enable/disable on mode switch / power | flush + re-acquire the WDDM allocation |
| `DrvGetModes` | enumerate modes | from the WDDM VidPN mode list |
| `DrvSetPointerShape` / `DrvMovePointer` | HW/SW cursor | SW cursor on the surface (software GPU) |
| `DrvSetPalette` | palette (≤8bpp) | trivial; 32bpp desktop ignores |
| `DrvEscape` / `DrvDrawEscape` | private control channel | **the composition contract** — see §4 |

Crucially, a non-accelerated canonical driver does **not** implement `DrvBitBlt`,
`DrvTextOut`, `DrvCopyBits`, `DrvStrokePath`, … itself. It hands GDI a plain bitmap surface
(`EngCreateBitmap`/`EngCreateDeviceSurface` over the primary) and lets the **GDI engine's
software rasterizer (`Eng*`) draw into it**. That is why "implement cdd" is not "write a
2-D accelerator" — it is "stand up a surface and let GDI draw, then present it."

### 2b. Downward: the D3DKMT client API (to dxgkrnl) — public (`d3dkmthk.h`)
To own a scan-out surface the WDDM way, the driver acts as a D3DKMT client:
`D3DKMTOpenAdapterFromHdc/…`, `D3DKMTCreateDevice`, `D3DKMTCreateAllocation` (the primary),
`D3DKMTSetVidPnSourceAddress`/present, `D3DKMTPresent`, plus mode/VidPN management. These are
the exact entry points the apitest suite exercises and that we are wiring through
gdi32→win32k→dxgkrnl. From a *kernel* GDI driver the same operations are reachable through
the in-kernel WDDM interface (our `wddm_bridge` / RXGK interface) rather than the gdi32
thunks.

---

## 3. The single real design decision: where the primary surface lives

Everything else is framebuf boilerplate. The interesting choice is what `DrvEnableSurface`
hands to GDI. Four options, increasing in fidelity:

**Option A — Mapped-shadow surface (what framebuf-via-dxgkrnl already is).**
`DrvEnableSurface` does `EngDeviceIoControl(IOCTL_VIDEO_MAP_VIDEO_MEMORY)` on `\Device\Video0`,
gets dxgkrnl's shadow framebuffer VA, wraps it in a GDI DIB. dxgkrnl's present timer scans the
shadow to the GOP.
*Pros:* zero new kernel plumbing; works today; reuses the verified path.
*Cons:* it is XDDM-shaped (IOCTL_VIDEO, not D3DKMT); no per-window surfaces; not really "the
WDDM canonical driver," just framebuf wearing the cdd name.

**Option B — D3DKMT-allocation primary (the honest canonical driver).**
`DrvEnableSurface` opens a D3DKMT device, `CreateAllocation`s a primary, locks/maps it, wraps
it in a GDI DIB; `DrvAssertMode`/present call `D3DKMTPresent` (or `SetVidPnSourceAddress`).
dxgkrnl owns the allocation and the scan-out.
*Pros:* genuinely the WDDM path; the same allocation can later be shared with a compositor;
exercises CreateAllocation/Present (the WDDM2 surface tests).
*Cons:* needs the D3DKMT create/present path solid (which the export-wiring work is landing).

**Option C — Compositor-shared primary (enables a real DWM).**
The primary is a *shareable* allocation; a user-mode compositor opens it, textures each
top-level window's redirection surface, composes, and the driver presents the composed
result with a vsync/flush handshake.
*Pros:* the actual DWM model.
*Cons:* needs a working 3-D/blit-capable miniport and a compositor process; softgpu is a null
GPU, so composition would be CPU blits initially.

**Recommendation:** ship **B**, structured so **C** is an additive mode, and keep **A** as a
compile-time/registry fallback for "no D3DKMT present yet." This is cleaner than Windows'
historical layering because we collapse the "is DWM on?" branch into one surface object with a
mode flag, instead of two driver personalities.

---

## 4. The composition contract (DrvEscape)

A compositor and the display driver need a private channel that GDI knows nothing about. The
public mechanism is `DrvEscape`/`ExtEscape` with a private escape code. Our suite already
fixes the contract with two codes (our values, not Microsoft's): a **cursor-suppression**
escape (the compositor draws the cursor itself, so the driver stops the HW/SW cursor) and a
**composition-sync** escape (present/vblank acknowledge so the compositor can pace frames).
`DrvEscape` recognizes these two, returns success, and forwards everything else to the
default handler. This is the seam a future DWM plugs into; without a DWM the driver just never
receives these escapes and presents directly.

---

## 5. Proposed ReactOS structure (our naming)

Mirror framebuf file-for-file but with our own internal symbol prefix — suggest **`RcddXxx`**
("ReactOS canonical display driver") so nothing collides with or copies Microsoft internals:

```
win32ss/drivers/displays/cdd/        ; module name cdd.dll (registry string only)
  cdd.h            ; PPDEV (our RCDD_PDEV), prototypes
  enable.c         ; RcddEnableDriver -> DRVFN[]; RcddEnablePDEV/Complete/Disable
  surface.c        ; RcddEnableSurface  <-- the §3 Option-B body lives here
  present.c        ; RcddPresent: D3DKMTPresent / SetVidPnSourceAddress + the blit ack
  screen.c         ; RcddGetModes / RcddAssertMode (mode-set via WDDM VidPN)
  pointer.c        ; RcddSetPointerShape / RcddMovePointer (SW cursor)
  palette.c        ; RcddSetPalette (≤8bpp only)
  escape.c         ; RcddEscape: cursor-suppress + composition-sync codes (§4)
  cdd.rc / CMakeLists.txt / cdd.spec(DrvEnableDriver only)
```

Differences from framebuf (the *only* new code):
1. `surface.c`: D3DKMT allocation primary instead of `IOCTL_VIDEO_MAP_VIDEO_MEMORY` (Option B).
2. `present.c`: an explicit present step (framebuf has none — it writes straight to the FB).
3. `escape.c`: the composition contract (framebuf has no compositor seam).

Everything else (`enable.c`, `screen.c`, `pointer.c`, `palette.c`, `GDIINFO`/`DEVINFO` fill)
is a near-verbatim adaptation of framebuf with renamed symbols. **No rasterizer is written**
— GDI's `Eng*` draws into our surface.

### Why this is cleaner than the historical design
- **One surface object, one optional compositor flag** instead of a DWM-on vs DWM-off driver
  split.
- **No vendor matrix**: softgpu/dxgkrnl already abstract the GPU, so cdd has zero
  hardware-specific code (the historical canonical driver still carries some).
- **Present is explicit and testable** (a `present.c` entry point), so the kmtest can drive it
  directly rather than inferring it from framebuffer writes.

---

## 6. Validation (kmtest, public-DDI based)

A kernel kmtest (no GUI needed) that:
1. Loads/enables the driver: call `RcddEnableDriver(DDI_DRIVER_VERSION,…)`, assert a valid
   `DRVENABLEDATA` with the expected non-NULL hooks + version.
2. PDEV/surface: `RcddEnablePDEV` with a known mode, `RcddEnableSurface`, assert a valid
   `HSURF` whose dimensions/stride match the committed WDDM mode.
3. Draw: `EngBitBlt`/`EngTextOut` a known pattern into the surface, read back pixels, assert.
4. Present: call the present path, assert success + (for the software GPU) that the bytes
   reached the dxgkrnl shadow/GOP.
5. Escape: send the cursor-suppress + composition-sync escapes, assert success; send a bogus
   escape, assert default/`-1`.

Cross-check the *contract* (return codes, surface geometry) against the Win11 reference where
the same observable behavior is available, but the implementation stays cleanroom.

---

## 7. Recommendation summary
- Implement **Option B** (D3DKMT-backed primary) as a framebuf-derived, `Rcdd`-prefixed driver
  — minimal new code (surface + present + escape), GDI does the drawing.
- Keep **Option A** as a registry/compile fallback until the D3DKMT present path is fully green.
- Leave **Option C** (compositor-shared primary + DWM) as an additive mode behind the escape
  contract — the seam is there, the compositor process is a later deliverable.
- Validate with a public-DDI kmtest; never reuse Microsoft symbol names.
