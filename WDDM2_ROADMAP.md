# WDDM 2 clean-room parity roadmap

This file records compatibility work that was deliberately removed or kept out
of the build because it exposed an export without implementing the Windows
contract behind it. An import resolving is not evidence that the API works.
Each item below may return only with a focused native-Windows behavior probe,
the same ReactOS test, and a real subsystem owner for all returned objects and
state.

## Removed provisional providers

### IOMMU interface (`ntoskrnl/io/iomgr/iommu.c`)

The provisional provider returned success and exposed versioned IOMMU
interfaces whose callbacks mostly returned `STATUS_NOT_SUPPORTED`. Keep
`IoGetIommuInterface` and `IoGetIommuInterfaceEx` as explicit unresolved/stub
exports until ReactOS has a platform IOMMU backend.

Completion requires:

- domain creation and destruction owned by the platform IOMMU implementation;
- map, unmap, flush, device attach, and device detach behavior with enforced
  lifetime and IRQL rules;
- no-provider, invalid-version, invalid-flags, and output-buffer behavior
  measured on native Windows; and
- matching AMD64 and ARM64 kmtests with a provider-unavailable skip path.

### DIF class-driver plug-in (`ntoskrnl/io/pnpmgr/dif.c`)

The removed function only validated parameters and returned a plug-in mismatch.
It did not register a class-driver plug-in or connect one to PnP.

Completion requires a real PnP-owned registration table, duplicate and
unregister/lifetime semantics, driver-unload cleanup, concurrency rules, and
paired native/ReactOS tests for success and failure paths.

### Memory partitions (`ntoskrnl/ex/partition.c`)

The removed code manufactured a `MemoryPartitionGraphics` object and wrote
values at guessed structure offsets. That was test-shaped state, not a memory
manager partition implementation. `PsPartitionType`, `ZwOpenPartition`, and
`ZwManagePartition` must remain explicit placeholders until the memory manager
owns the objects and their accounting.

Completion requires a documented clean-room object model, access checks,
namespace and handle lifetime, query/set information classes derived from
native probes, resource accounting, teardown, and stress tests. No hard-coded
private structure offsets are acceptable.

### TTM device notifications (`ntoskrnl/po/ttm.c`)

The removed implementation stored synthetic arrival/input/departure records but
had no topology or power provider consuming them.

Completion requires a real owning subsystem, observable arrival-to-departure
lifetime, duplicate and malformed notification behavior, synchronization, and
paired native/ReactOS tests for each notification class.

### Shared device address spaces (`ntoskrnl/ex/wddm.c`)

The removed `ExShareAddressSpaceWithDevice` implementation generated an
arbitrary identifier without creating or sharing an address space.

Completion requires DMA/IOMMU-backed address-space ownership, process and
device lifetime references, access checks, idempotence, stop-sharing/teardown,
and native parity tests. If the platform has no provider, the API must report
that fact without manufacturing success.

### ARM64 hypervisor vendor (`ntoskrnl/ke/arm64/hypervisor.c`)

The removed `HvlGetHypervisorVendorId` always returned `NULL`; it performed no
ARM64 hypervisor discovery.

Completion requires architecture-correct detection, stable vendor-string
ownership, bare-metal behavior, supported-hypervisor probes, and ARM64 runtime
coverage.

## Removed loader and export shortcuts

### KSR API-set thunk synthesis (`ntoskrnl/mm/ARM3/sysldr.c`)

The loader no longer replaces every import from an empty KSR API-set host with
generic success or `STATUS_NOT_SUPPORTED` functions. An API-set entry with no
host now fails resolution instead of allowing a driver to load against invented
behavior.

Completion requires the real KSR host/provider, exact per-export signatures,
and native/ReactOS behavior tests. Loader API-set resolution itself remains
generic and may resolve only contracts with a real host.

### NTDLL resource and atom promotion

The premature promotion of `LdrResFindResource`,
`LdrResFindResourceDirectory`, and `RtlGetIntegerAtom` was reverted because the
clean ARM64 NTDLL link proved that the implementations were not part of the
commit.

Completion requires landing implementation, declarations, spec exports, and
tests atomically. The clean AMD64 and ARM64 NTDLL targets must link, followed by
paired native/ReactOS tests for valid lookups, malformed inputs, misses, and
integer-atom boundary cases.

## Rules for promotion

An item leaves this roadmap only when all of the following are present in one
reviewable semantic series:

1. Native evidence establishes signatures, architecture visibility, return
   values, output mutation, state transitions, and error paths.
2. The implementation is owned by the correct ReactOS subsystem and does not
   use guessed private offsets or payload binaries.
3. One focused test runs unchanged on native Windows and ReactOS; an unavailable
   hardware provider is a skip, not a guessed success.
4. Clean AMD64 and ARM64 builds pass, plus runtime validation on every relevant
   architecture.
5. The export is promoted from an explicit stub only in the commit that adds
   the tested behavior.
