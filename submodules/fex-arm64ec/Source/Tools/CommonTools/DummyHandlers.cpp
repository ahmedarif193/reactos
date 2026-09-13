// SPDX-License-Identifier: MIT
#include "DummyHandlers.h"

#include <FEXCore/Core/Context.h>
#include <FEXCore/Debug/InternalThreadState.h>
#include <mutex>

namespace FEX::DummyHandlers {
thread_local FEXCore::Core::InternalThreadState* TLSThread;

void DummySyscallHandler::InvalidateGuestCodeRange(FEXCore::Core::InternalThreadState* Thread, uint64_t Start, uint64_t Length) {
  // The standalone harness has one guest thread. Full SMC checks must discard
  // its stale translations before retrying the modified instruction.
  auto* CTX = Thread->CTX;
  std::lock_guard Lock {CTX->GetCodeInvalidationMutex()};
  CTX->InvalidateCodeBuffersCodeRange(Start, Length);
  CTX->InvalidateThreadCachedCodeRange(Thread, Start, Length);
}

void DummySignalDelegator::RegisterTLSState(FEXCore::Core::InternalThreadState* Thread) {
  TLSThread = Thread;
}

void DummySignalDelegator::UninstallTLSState(FEXCore::Core::InternalThreadState* Thread) {
  TLSThread = nullptr;
}

FEXCore::Core::InternalThreadState* DummySignalDelegator::GetTLSThread() {
  return TLSThread;
}

fextl::unique_ptr<FEXCore::HLE::SyscallHandler> CreateSyscallHandler() {
  return fextl::make_unique<DummySyscallHandler>();
}

fextl::unique_ptr<FEX::DummyHandlers::DummySignalDelegator> CreateSignalDelegator() {
  return fextl::make_unique<DummySignalDelegator>();
}
} // namespace FEX::DummyHandlers
