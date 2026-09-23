// SPDX-License-Identifier: MIT

#include <FEXCore/Utils/LogManager.h>
#include <FEXCore/Utils/TypeDefines.h>
#include <FEXCore/Utils/SignalScopeGuards.h>
#include <FEXCore/Core/Context.h>
#include <FEXCore/Config/Config.h>
#include <FEXCore/Debug/InternalThreadState.h>
#include "InvalidationTracker.h"
#include <algorithm>
#include <windef.h>
#include <winternl.h>

namespace FEX::Windows {
#ifdef __REACTOS__
namespace {
constexpr ULONG ProcessManageWritesToExecutableMemory = 83;
constexpr ULONG ThreadManageWritesToExecutableMemory = 48;
constexpr ULONG VmPageDirtyStateInformation = 3;

struct ManageWritesToExecutableMemory {
  ULONG Version : 8;
  ULONG ProcessEnableWriteExceptions : 1;
  ULONG ThreadAllowWrites : 1;
  ULONG Spare : 22;
  PVOID KernelWriteToExecutableSignal;
};

struct MemoryRangeEntry {
  PVOID VirtualAddress;
  SIZE_T NumberOfBytes;
};

using NtSetInformationVirtualMemoryFn = NTSTATUS(WINAPI*)(HANDLE, ULONG, ULONG_PTR, MemoryRangeEntry*, PVOID, ULONG);
NtSetInformationVirtualMemoryFn NtSetInformationVirtualMemoryPtr;

NTSTATUS SetProcessExecutableWriteExceptions(bool Enable) {
  ManageWritesToExecutableMemory Information {};
  Information.Version = 2;
  Information.ProcessEnableWriteExceptions = Enable;
  return NtSetInformationProcess(NtCurrentProcess(), static_cast<PROCESSINFOCLASS>(ProcessManageWritesToExecutableMemory), &Information, sizeof(Information));
}
} // namespace

#endif
InvalidationTracker::InvalidationTracker(FEXCore::Context::Context& CTX, const std::unordered_map<DWORD, FEXCore::Core::InternalThreadState*>& Threads)
  : CTX {CTX}
  , Threads {Threads} {
  FEX_CONFIG_OPT(SMCChecks, SMCCHECKS);
  SMCDetectionDisabled = (SMCChecks == FEXCore::Config::CONFIG_SMC_NONE);
#ifdef __REACTOS__

  if (!SMCDetectionDisabled) {
    const auto Ntdll = GetModuleHandleW(L"ntdll.dll");
    NtSetInformationVirtualMemoryPtr = Ntdll ? reinterpret_cast<NtSetInformationVirtualMemoryFn>(GetProcAddress(Ntdll, "NtSetInformationVirtualMemory")) : nullptr;
  }
#endif

  MEMORY_BASIC_INFORMATION Info;
  uint64_t Address = 0;

  while (VirtualQuery(reinterpret_cast<LPCVOID>(Address), &Info, sizeof(Info))) {
    uint64_t BaseAddress = reinterpret_cast<uint64_t>(Info.BaseAddress);
    if (Info.State == MEM_COMMIT) {
      HandleMemoryProtectionNotification(BaseAddress, Info.RegionSize, Info.Protect);
    }

    Address = BaseAddress + Info.RegionSize;
  }
#if defined(__REACTOS__) && defined(_M_ARM64EC)

  // FEX's dispatcher and initial host code buffer exist before the tracker. Leave those native EC mappings writable;
  // managed dirty-state tracking is for guest executable memory registered below and after process initialization.
  ManagedExecutableWrites = NtSetInformationVirtualMemoryPtr && SetProcessExecutableWriteExceptions(true) == STATUS_SUCCESS;
#endif
}

static bool ProtHasExec(ULONG Prot) {
  return (Prot & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

static bool ProtIsReadable(ULONG Prot) {
  return (Prot & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                  PAGE_EXECUTE_WRITECOPY)) != 0;
}

static bool ProtIsWritable(ULONG Prot) {
  return (Prot & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

void InvalidationTracker::HandleMemoryProtectionNotification(uint64_t Address, uint64_t Size, ULONG Prot) {
  const auto AlignedBase = Address & FEXCore::Utils::FEX_PAGE_MASK;
  const auto AlignedSize = (Address - AlignedBase + Size + FEXCore::Utils::FEX_PAGE_SIZE - 1) & FEXCore::Utils::FEX_PAGE_MASK;

#ifdef __REACTOS__
  bool NeedsReprotect {};
#endif
  const bool NeedsInvalidate = [&]() {
    std::unique_lock Lock(IntervalsLock);

    FEXCore::IntervalList<uint64_t>::Interval ProtInterval {AlignedBase, AlignedBase + AlignedSize};

    const bool HasExec = ProtHasExec(Prot);
    const bool EffectiveExec = HasExec || (DEPDisabled && ProtIsReadable(Prot));
    const bool EffectiveRWX = EffectiveExec && ProtIsWritable(Prot);

    if (EffectiveExec) {
      XIntervals.Insert(ProtInterval);
      if (EffectiveRWX) {
        LogMan::Msg::DFmt("Add SMC interval: {:X} - {:X}", AlignedBase, AlignedBase + AlignedSize);
        RWXIntervals.Insert(ProtInterval);
#ifdef __REACTOS__
        // Kernel write tracking only covers pages that are executable at the OS level, not DEP-promoted RW regions.
        NeedsReprotect = HasExec;
#endif
      }
#ifdef __REACTOS__
      else {
        RWXIntervals.Remove(ProtInterval);
      }
#endif
      if (DEPDisabled && !HasExec) {
        DEPPromotedIntervals.Insert(ProtInterval);
      }
      return true;
    } else if (XIntervals.Intersect(ProtInterval)) {
      XIntervals.Remove(ProtInterval);
      RWXIntervals.Remove(ProtInterval);
      if (DEPDisabled) {
        DEPPromotedIntervals.Remove(ProtInterval);
      }
      return true;
    }

    return false;
  }();

  if (NeedsInvalidate) {
    // IntervalsLock cannot be held during invalidation
    InvalidateIntervalInternal(AlignedBase, AlignedSize);
  }
#ifdef __REACTOS__
  if (NeedsReprotect && ManagedExecutableWrites) {
    const auto Status = ResetExecutableWriteTracking(AlignedBase, AlignedSize);
    if (Status != STATUS_SUCCESS) {
      LogMan::Msg::EFmt("Failed to track executable writes for {:X}-{:X}: {:X}", AlignedBase, AlignedBase + AlignedSize, static_cast<uint32_t>(Status));
    }
  }
#endif
}

void InvalidationTracker::HandleProcessExecuteFlagsChange(ULONG Flags) {
  const bool DisableDEP = (Flags & MEM_EXECUTE_OPTION_ENABLE) != 0;

  std::scoped_lock CodeLock(CTX.GetCodeInvalidationMutex());
  std::unique_lock Lock(IntervalsLock);

  if (DisableDEP == DEPDisabled) {
    return;
  }

  DEPDisabled = DisableDEP;

  if (DisableDEP) {
    DEPPromotedIntervals.Clear();

    MEMORY_BASIC_INFORMATION Info;
    uint64_t Address = 0;

    while (VirtualQuery(reinterpret_cast<LPCVOID>(Address), &Info, sizeof(Info))) {
      uint64_t BaseAddress = reinterpret_cast<uint64_t>(Info.BaseAddress);
      if (Info.State == MEM_COMMIT && ProtIsReadable(Info.Protect) && !ProtHasExec(Info.Protect)) {
        const auto AlignedBase = BaseAddress & FEXCore::Utils::FEX_PAGE_MASK;
        const auto AlignedSize = (BaseAddress - AlignedBase + Info.RegionSize + FEXCore::Utils::FEX_PAGE_SIZE - 1) & FEXCore::Utils::FEX_PAGE_MASK;
        FEXCore::IntervalList<uint64_t>::Interval ProtInterval {AlignedBase, AlignedBase + AlignedSize};

        XIntervals.Insert(ProtInterval);
        if (ProtIsWritable(Info.Protect)) {
          RWXIntervals.Insert(ProtInterval);
        }
        DEPPromotedIntervals.Insert(ProtInterval);
      }

      Address = BaseAddress + Info.RegionSize;
    }
  } else {
    for (const auto& Interval : DEPPromotedIntervals) {
      XIntervals.Remove(Interval);
      RWXIntervals.Remove(Interval);
    }
    DEPPromotedIntervals.Clear();
  }

  // Invalidate all cached code: previously-compiled blocks may contain NoExec stubs for addresses
  // that are now executable (or reference regions whose executability just changed).
  InvalidateIntervalInternalLocked(0, std::numeric_limits<uint64_t>::max());
}

void InvalidationTracker::HandleImageMap(std::string_view Name, uint64_t Address) {
  auto* Nt = RtlImageNtHeader(reinterpret_cast<HMODULE>(Address));
  auto* SectionsBegin = IMAGE_FIRST_SECTION(Nt);
  auto* SectionsEnd = SectionsBegin + Nt->FileHeader.NumberOfSections;
  uint64_t LastExecutableSectionEnd = 0;

  for (auto* Section = SectionsBegin; Section != SectionsEnd; Section++) {
    if (Section->Characteristics & IMAGE_SCN_MEM_EXECUTE) {
#ifdef __REACTOS__
      const uint64_t SectionBase = Address + Section->VirtualAddress;
      const uint64_t SectionSize = Section->Misc.VirtualSize;
      const uint64_t SectionEnd = SectionBase + SectionSize;
      const bool Writable = Section->Characteristics & IMAGE_SCN_MEM_WRITE;
      {
        std::unique_lock Lock(IntervalsLock);
        XIntervals.Insert({SectionBase, SectionEnd});
        LastExecutableSectionEnd = std::max(LastExecutableSectionEnd, SectionEnd);
        if (Writable) {
          LogMan::Msg::DFmt("Add image SMC interval: {:X} - {:X}", SectionBase, SectionEnd);
          RWXIntervals.Insert({SectionBase, SectionEnd});
        }
      }
      // Re-arm outside IntervalsLock; the kernel call must not be made while blocking interval readers.
      if (Writable && ManagedExecutableWrites) {
        const auto Status = ResetExecutableWriteTracking(SectionBase, SectionSize);
        if (Status != STATUS_SUCCESS) {
          LogMan::Msg::EFmt("Failed to track executable image writes for {:X}-{:X}: {:X}", SectionBase, SectionEnd, static_cast<uint32_t>(Status));
        }
      }
#else
      std::unique_lock Lock(IntervalsLock);

      uint64_t SectionBase = Address + Section->VirtualAddress;
      uint64_t SectionEnd = SectionBase + Section->Misc.VirtualSize;
      XIntervals.Insert({SectionBase, SectionEnd});
      LastExecutableSectionEnd = std::max(LastExecutableSectionEnd, SectionEnd);
      if (Section->Characteristics & IMAGE_SCN_MEM_WRITE) {
        LogMan::Msg::DFmt("Add image SMC interval: {:X} - {:X}", SectionBase, SectionBase + Section->Misc.VirtualSize);
        RWXIntervals.Insert({SectionBase, SectionBase + Section->Misc.VirtualSize});
      }
#endif
    }
  }

  FEX_CONFIG_OPT(MonoHacks, MONOHACKS);
  if (MonoHacks && (Name == "mono-2.0-bdwgc.dll" || Name == "mono.dll")) {
    FEX_CONFIG_OPT(MaxInst, MAXINST);
    FEX_CONFIG_OPT(Multiblock, MULTIBLOCK);
    if (Multiblock && MaxInst() >= 500) {
      // Require these settings to ensure we can safely hook all SMC sites in a single block
      CTX.MarkMonoDetected();
      MonoBackpatcherDetectionPending = true;
      MonoBase = Address;
      MonoEnd = LastExecutableSectionEnd;
    } else {
      LogMan::Msg::IFmt("Not applying mono hacks, Multiblock with MaxInst >= 500 required");
    }
  }
}

InvalidationTracker::InvalidateContainingSectionResult InvalidationTracker::InvalidateContainingSection(uint64_t Address, bool Free) {
  MEMORY_BASIC_INFORMATION Info;
  if (NtQueryVirtualMemory(NtCurrentProcess(), reinterpret_cast<void*>(Address), MemoryBasicInformation, &Info, sizeof(Info), nullptr)) {
    return {Address, 0};
  }

  const auto SectionBase = reinterpret_cast<uint64_t>(Info.AllocationBase);
  auto SectionSize = reinterpret_cast<uint64_t>(Info.BaseAddress) + Info.RegionSize - SectionBase;

  while (!NtQueryVirtualMemory(NtCurrentProcess(), reinterpret_cast<void*>(SectionBase + SectionSize), MemoryBasicInformation, &Info,
                               sizeof(Info), nullptr) &&
         reinterpret_cast<uint64_t>(Info.AllocationBase) == SectionBase) {
    SectionSize += Info.RegionSize;
  }

  InvalidateIntervalInternal(SectionBase, SectionSize);

  if (Free) {
    std::unique_lock Lock(IntervalsLock);
    XIntervals.Remove({SectionBase, SectionBase + SectionSize});
    RWXIntervals.Remove({SectionBase, SectionBase + SectionSize});
  }

  return {SectionBase, SectionSize};
}

void InvalidationTracker::InvalidateAlignedInterval(uint64_t Address, uint64_t Size, bool Free) {
  if (!Address) {
    // Match the Windows behaviour when passed a NULL base address.
    Size = std::numeric_limits<uint64_t>::max();
  }

  const auto AlignedBase = Address & FEXCore::Utils::FEX_PAGE_MASK;
  const auto AlignedSize = std::max(Size, (Address - AlignedBase + Size + FEXCore::Utils::FEX_PAGE_SIZE - 1) & FEXCore::Utils::FEX_PAGE_MASK);

  InvalidateIntervalInternal(AlignedBase, AlignedSize);

  if (Free) {
    std::unique_lock Lock(IntervalsLock);
    XIntervals.Remove({AlignedBase, AlignedBase + AlignedSize});
    RWXIntervals.Remove({AlignedBase, AlignedBase + AlignedSize});
  }
}

void InvalidationTracker::ReprotectRWXIntervals(uint64_t Address, uint64_t Size) {
  ProtectRWXIntervalsInternal(Address, Size, false);
}

bool InvalidationTracker::HandleRWXAccessViolation(FEXCore::Core::InternalThreadState* Thread, uint64_t HostPc, uint64_t FaultAddress) {
  const auto [NeedsInvalidate, UntrapProt] = [&](uint64_t Address) -> std::pair<bool, ULONG> {
    std::shared_lock Lock(IntervalsLock);
    if (!RWXIntervals.Query(Address).Enclosed) {
      return {false, 0};
    }
    return {true, GetUntrapProt(Address)};
  }(FaultAddress);

  if (NeedsInvalidate) {
    // IntervalsLock cannot be held during invalidation
    {
      std::scoped_lock Lock(CTX.GetCodeInvalidationMutex());

      InvalidateIntervalInternalLocked(FaultAddress & FEXCore::Utils::FEX_PAGE_MASK, FEXCore::Utils::FEX_PAGE_SIZE);

      // Invalidate, then unprotect the faulting page with the compilation lock held to ensure that any racing invalidations are not dropped.
#ifdef __REACTOS__
      // Managed executable writes: let this thread perform the write once so the kernel marks the page dirty (writable) again.
      // DEP-promoted pages are not executable at the OS level and keep using protection changes.
      if (ManagedExecutableWrites && UntrapProt == PAGE_EXECUTE_READWRITE) {
        if (!AllowExecutablePageWrite(FaultAddress)) {
          return false;
        }
      } else {
        ULONG TmpProt;
        void* TmpAddress = reinterpret_cast<void*>(FaultAddress);
        SIZE_T TmpSize = 1;
        if (NtProtectVirtualMemory(NtCurrentProcess(), &TmpAddress, &TmpSize, UntrapProt, &TmpProt) < 0) {
          return false;
        }
      }
#else
      ULONG TmpProt;
      void* TmpAddress = reinterpret_cast<void*>(FaultAddress);
      SIZE_T TmpSize = 1;
      if (NtProtectVirtualMemory(NtCurrentProcess(), &TmpAddress, &TmpSize, UntrapProt, &TmpProt) < 0) {
        return false;
      }
#endif
    }
    DetectMonoBackpatcherBlock(Thread, HostPc);
    return true;
  }
  return false;
}

bool InvalidationTracker::BeginUntrackedWriteLocked(uint64_t Address, uint64_t Size) {
  if (!Size || !ProtectRWXIntervalsInternal(Address, Size, true)) {
    return false;
  }

  std::unique_lock Lock(IntervalsLock);
  PendingWrites.push_back({Address, Address + Size});
  return true;
}

void InvalidationTracker::EndUntrackedWriteLocked(uint64_t Address, uint64_t Size) {
  // Compilation was allowed while I/O was blocked. Discard any translations
  // made from the old bytes before the caller can observe read completion.
  InvalidateIntervalInternalLocked(Address, Size);
  {
    std::unique_lock Lock(IntervalsLock);
    const auto End = Address + Size;
    const auto It = std::find_if(PendingWrites.begin(), PendingWrites.end(), [Address, End](const auto& Range) {
      return Range.Offset == Address && Range.End == End;
    });
    if (It != PendingWrites.end()) {
      PendingWrites.erase(It);
    }
  }
#ifdef __REACTOS__
  if (ManagedExecutableWrites) {
    ProtectRWXIntervalsInternal(Address, Size, false);
  }
#endif
}
#ifdef __REACTOS__

bool InvalidationTracker::AllowExecutablePageWrite(uint64_t Address) {
  auto Status = SetThreadExecutableWrites(true);
  if (Status != STATUS_SUCCESS) {
    LogMan::Msg::EFmt("Failed to allow an executable write at {:X}: {:X}", Address, static_cast<uint32_t>(Status));
    return false;
  }
  auto* FaultByte = reinterpret_cast<volatile uint8_t*>(Address);
  *FaultByte = *FaultByte;
  Status = SetThreadExecutableWrites(false);
  if (Status != STATUS_SUCCESS) {
    LogMan::Msg::EFmt("Failed to restore executable-write tracking at {:X}: {:X}", Address, static_cast<uint32_t>(Status));
    return false;
  }
  return true;
}

bool InvalidationTracker::HandleJitCodeWrite(FEXCore::Core::InternalThreadState* Thread, uint64_t HostPC, uint64_t FaultAddress) {
  // WoW64's native code buffers are not EC mappings. A fresh buffer can fault
  // while CompileCode holds the code-cache lock; it is not guest SMC and must
  // not recursively invalidate the cache. Guest writes still take the normal
  // invalidation path, and the code-buffer guard page remains excluded.
  if (!ManagedExecutableWrites || !Thread || CTX.IsAddressInCodeBuffer(Thread, HostPC) ||
      !CTX.IsAddressInCodeBuffer(Thread, FaultAddress)) {
    return false;
  }
  return AllowExecutablePageWrite(FaultAddress);
}

NTSTATUS InvalidationTracker::ResetExecutableWriteTracking(uint64_t Address, uint64_t Size) {
  if (!NtSetInformationVirtualMemoryPtr || !Size) {
    return STATUS_INVALID_PARAMETER;
  }
  MemoryRangeEntry Range {reinterpret_cast<PVOID>(Address), static_cast<SIZE_T>(Size)};
  ULONG Flag {};
  return NtSetInformationVirtualMemoryPtr(NtCurrentProcess(), VmPageDirtyStateInformation, 1, &Range, &Flag, sizeof(Flag));
}

NTSTATUS InvalidationTracker::SetThreadExecutableWrites(bool AllowWrites) {
  ManageWritesToExecutableMemory Information {};
  Information.Version = 2;
  Information.ThreadAllowWrites = AllowWrites;
  return NtSetInformationThread(NtCurrentThread(), static_cast<THREADINFOCLASS>(ThreadManageWritesToExecutableMemory), &Information, sizeof(Information));
}
#endif

FEXCore::HLE::ExecutableRangeInfo InvalidationTracker::QueryExecutableRange(uint64_t Address) {
  std::shared_lock Lock(IntervalsLock);
  const auto XResult = XIntervals.Query(Address);
  if (!XResult.Enclosed) {
    return {};
  }
  const auto RWXResult = RWXIntervals.Query(Address);
  if (RWXResult.Enclosed) {
    return {RWXResult.Interval.Offset, RWXResult.Interval.End - RWXResult.Interval.Offset, true};
  } else if (RWXResult.Size && RWXResult.Size < XResult.Size) {
    return {XResult.Interval.Offset, RWXResult.Interval.Offset - XResult.Interval.Offset, false};
  }
  return {XResult.Interval.Offset, XResult.Interval.End - XResult.Interval.Offset, false};
}

void InvalidationTracker::DetectMonoBackpatcherBlock(FEXCore::Core::InternalThreadState* Thread, uint64_t HostPc) {
  if (!MonoBackpatcherDetectionPending) {
    return;
  }

  if (!CTX.IsAddressInCodeBuffer(Thread, HostPc)) {
    return;
  }

  uint64_t RIP = CTX.RestoreRIPFromHostPC(Thread, HostPc);
  if (!RIP || RIP < MonoBase || RIP >= MonoEnd) {
    return;
  }

  static constexpr uint8_t XChgOp = 0x87;
  if (*reinterpret_cast<uint8_t*>(RIP) != XChgOp && *reinterpret_cast<uint8_t*>(RIP + 1) != XChgOp) {
    return;
  }

  uint64_t BlockEntry = CTX.GetGuestBlockEntry(Thread);
  LogMan::Msg::DFmt("Detected mono backpatcher at: {:X}", BlockEntry);
  DisableSMCDetection();
  {
    std::scoped_lock CodeLock(CTX.GetCodeInvalidationMutex());
    CTX.MarkMonoBackpatcherBlock(BlockEntry);
  }
  InvalidateAlignedInterval(BlockEntry, FEXCore::Utils::FEX_PAGE_SIZE, false);
}

void InvalidationTracker::DisableSMCDetection() {
  std::unique_lock Lock(IntervalsLock);
  SMCDetectionDisabled = true;
#ifdef __REACTOS__
  if (ManagedExecutableWrites) {
    if (SetProcessExecutableWriteExceptions(false) != STATUS_SUCCESS) {
      LogMan::Msg::EFmt("Failed to disable managed executable writes");
    }
    ManagedExecutableWrites = false;
  }
#endif
  uint64_t Address = 0;

  // Reprotect all RWX intervals as writable
  FEXCore::IntervalList<uint64_t>::QueryResult Query;
  do {
    Query = RWXIntervals.Query(Address);
    if (Query.Enclosed) {
      void* TmpAddress = reinterpret_cast<void*>(Address);
      SIZE_T TmpSize = static_cast<SIZE_T>(Query.Size);
      ULONG TmpProt;
      NtProtectVirtualMemory(NtCurrentProcess(), &TmpAddress, &TmpSize, GetUntrapProt(Address), &TmpProt);
    }
    Address += Query.Size;
  } while (Query.Size);
}

ULONG InvalidationTracker::GetTrapProt(uint64_t Address) const {
  if (DEPDisabled && DEPPromotedIntervals.Query(Address).Enclosed) {
    return PAGE_READONLY;
  }
  return PAGE_EXECUTE_READ;
}

ULONG InvalidationTracker::GetUntrapProt(uint64_t Address) const {
  if (DEPDisabled && DEPPromotedIntervals.Query(Address).Enclosed) {
    return PAGE_READWRITE;
  }
  return PAGE_EXECUTE_READWRITE;
}

void InvalidationTracker::InvalidateIntervalInternal(uint64_t Address, uint64_t Size) {
  std::scoped_lock CodeLock(CTX.GetCodeInvalidationMutex());
  InvalidateIntervalInternalLocked(Address, Size);
}

void InvalidationTracker::InvalidateIntervalInternalLocked(uint64_t Address, uint64_t Size) {
  // NOTE: This assumes CodeInvalidationMutex is locked by the caller
  CTX.InvalidateCodeBuffersCodeRange(Address, Size);
  for (auto Thread : Threads) {
    CTX.InvalidateThreadCachedCodeRange(Thread.second, Address, Size);
  }
}

bool InvalidationTracker::ProtectRWXIntervalsInternal(uint64_t Address, uint64_t Size, bool ForWriteLocked) {
  const auto End = Address + Size;
  std::shared_lock Lock(IntervalsLock);

  if (SMCDetectionDisabled) {
    return false;
  }

  bool HitRWXInterval = false;
  while (Address < End) {
    const auto Query = RWXIntervals.Query(Address);
    if (!Query.Size) {
      break;
    }
    auto RangeEnd = std::min(End, Address + Query.Size);
    if (Query.Enclosed) {
      if (!ForWriteLocked) {
        bool Pending = false;
        for (const auto& Write : PendingWrites) {
          const auto WriteStart = Write.Offset & FEXCore::Utils::FEX_PAGE_MASK;
          const auto WriteEnd = (Write.End + FEXCore::Utils::FEX_PAGE_SIZE - 1) & FEXCore::Utils::FEX_PAGE_MASK;
          if (Address >= WriteStart && Address < WriteEnd) {
            Address = std::min(End, WriteEnd);
            Pending = true;
            break;
          }
          if (WriteStart > Address) {
            RangeEnd = std::min(RangeEnd, WriteStart);
          }
        }
        if (Pending) {
          continue;
        }
      }
      if (!HitRWXInterval) {
        if (ForWriteLocked) {
          // If we are protecting as writable, then the entire range must be invalidated before any protections are
          // applied and the invalidation mutex must be locked throughout.
          // Do this lazily only when an RWX region is actually hit.
          // NOTE: This assumes CodeInvalidationMutex is locked by the caller
          InvalidateIntervalInternalLocked(Address, Size);
        }
        HitRWXInterval = true;
      }
      void* TmpAddress = reinterpret_cast<void*>(Address);
      SIZE_T TmpSize = static_cast<SIZE_T>(RangeEnd - Address);
#ifdef __REACTOS__
      // Managed executable writes: trapping is done by re-arming kernel dirty-state tracking instead of removing write
      // access, and the kernel restores write access itself once the fault handler has performed the write.
      // DEP-promoted intervals are not executable at the OS level and keep using protection changes.
      if (ManagedExecutableWrites && GetTrapProt(Address) == PAGE_EXECUTE_READ) {
        if (!ForWriteLocked) {
          const auto Status = ResetExecutableWriteTracking(Address, TmpSize);
          if (Status != STATUS_SUCCESS) {
            LogMan::Msg::EFmt("Failed to rearm executable-write tracking for {:X}-{:X}: {:X}", Address, Address + TmpSize, static_cast<uint32_t>(Status));
          }
        }
      } else {
        ULONG TmpProt;
        NtProtectVirtualMemory(NtCurrentProcess(), &TmpAddress, &TmpSize, ForWriteLocked ? GetUntrapProt(Address) : GetTrapProt(Address), &TmpProt);
      }
#else
      ULONG TmpProt;
      NtProtectVirtualMemory(NtCurrentProcess(), &TmpAddress, &TmpSize, ForWriteLocked ? GetUntrapProt(Address) : GetTrapProt(Address), &TmpProt);
#endif
    }

    Address = RangeEnd;
  }

  return HitRWXInterval;
}

} // namespace FEX::Windows
