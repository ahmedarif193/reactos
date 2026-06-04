# RPi5 ARM64 phase-1 KdDriver timing finding

Current temporary boot-survival marker:

- `ntoskrnl/ex/init.c` has a single `DbgPrint("5")` in `Phase1InitializationDiscard()`, currently before `PoInitSystem(1)`.
- Keep this marker while finishing the RPi5VC4 display path. It is not the final fix; it is a timing perturbation that keeps the board past the old silent boot boundary.

Observed boundary without the timing perturbation:

```text
KDB: Executing KDBinit file...
KDB: KDBinit executed
(\ntoskrnl\io\iomgr\driver.c:309) Deleting driver object '\Driver\KdDriver'
```

After that point the board can become silent and the hardware test reports a stall. The same image can sometimes continue, so this is timing-sensitive rather than a deterministic missing-driver failure.

Marker bisection summary:

- A single marker in `PspInitializeSystemDll()` before the first export lookup let boot continue.
- A single marker in `PspInitPhase1()` before `PspInitializeSystemDll()` let boot continue.
- A single marker in the phase-1 branch of `PsInitSystem()` before `PspInitPhase1()` let boot continue.
- A single marker in `Phase1InitializationDiscard()` before `PsInitSystem(LoaderBlock)` let boot continue.
- A single marker in `Phase1InitializationDiscard()` before `PoInitSystem(1)` let boot continue and is the current kept marker.

Interpretation:

- The evidence does not identify `PoInitSystem`, `PsInitSystem`, `PspInitializeSystemDll`, win32k, `uefifb`, or `rpi5vc4` as the root cause.
- The marker is acting as a delay/synchronization disturbance around the phase-1 transition after system-driver loading and final KD reinitialization.
- The real fix should be investigated later in the phase-1/KD teardown/scheduler-timer-interrupt path around the final `KdDriver` delete and the first stable user-mode progress.
- Do not upstream the marker. Keep it only as temporary scaffolding while the RPi5VC4 miniport is completed and tested.
