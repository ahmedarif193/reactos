// SPDX-License-Identifier: MIT
#pragma once

#include <FEXCore/Utils/IntervalList.h>
#include <thread>
#include <shared_mutex>


namespace FEX::Windows {
/**
 * @brief Emulates memory overcommit of reserved regions with exceptions
 */
class OvercommitTracker {
private:
  bool IsWine;
  FEXCore::IntervalList<uint64_t> OvercommitIntervals;
  FEXCore::IntervalList<uint64_t> OvercommitExecIntervals;
  std::shared_mutex OvercommitIntervalsMutex;

public:
  OvercommitTracker(bool IsWine)
    : IsWine {IsWine} {}

  void MarkRange(uint64_t Start, uint64_t Length, bool Exec = false) {
    std::unique_lock Lock {OvercommitIntervalsMutex};
    (Exec ? OvercommitExecIntervals : OvercommitIntervals).Insert({Start, Start + Length});
  }

  void UnmarkRange(uint64_t Start, uint64_t Length) {
    std::unique_lock Lock {OvercommitIntervalsMutex};
    OvercommitIntervals.Remove({Start, Start + Length});
    OvercommitExecIntervals.Remove({Start, Start + Length});
  }

  bool HandleAccessViolation(uint64_t FaultAddress, uint64_t AccessType) {
    std::shared_lock Lock {OvercommitIntervalsMutex};
    bool Exec = false;
    auto Query = OvercommitIntervals.Query(FaultAddress);
    if (!Query.Enclosed) {
      Query = OvercommitExecIntervals.Query(FaultAddress);
      Exec = true;
    }

    if (!Query.Enclosed || (AccessType != 0 && AccessType != 1 && (AccessType != 8 || !Exec))) {
      return false;
    }

    MEMORY_BASIC_INFORMATION Info {};
    if (NtQueryVirtualMemory(NtCurrentProcess(), reinterpret_cast<void*>(FaultAddress), MemoryBasicInformation, &Info, sizeof(Info), nullptr) < 0 || Info.Type != MEM_PRIVATE) {
      return false;
    }

    const DWORD Protection = Exec ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE;
    if (Info.State == MEM_COMMIT) {
      return Info.Protect == Protection;
    }
    if (Info.State != MEM_RESERVE) {
      return false;
    }

    const auto RegionStart = reinterpret_cast<uint64_t>(Info.BaseAddress);
    const auto CommitStart = IsWine ? std::max(RegionStart, Query.Interval.Offset) : FaultAddress & FEXCore::Utils::FEX_PAGE_MASK;
    const auto CommitEnd = std::min(RegionStart + Info.RegionSize, Query.Interval.End);
    static constexpr uint64_t MaxFaultCommitSize = 1024 * 64;
    const auto CommitSize = IsWine ? CommitEnd - CommitStart : std::min(CommitEnd - CommitStart, MaxFaultCommitSize);
    return VirtualAlloc(reinterpret_cast<void*>(CommitStart), CommitSize, MEM_COMMIT, Protection) != nullptr;
  }
};
} // namespace FEX::Windows
