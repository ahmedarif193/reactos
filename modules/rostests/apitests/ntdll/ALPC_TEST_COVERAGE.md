# ALPC API-test coverage and evidence matrix

License: GPL-3.0-or-later
Copyright 2026 Ahmed ARIF

This file inventories the ALPC tests currently wired under
modules/rostests/apitests/ntdll. It is a coverage map, not a native-parity
claim. No Win11 result log has been collected for this suite yet, so every
row below is explicitly native-unknown even where the source contains exact
assertions.

## Evidence rules

- Asserted means the test contains an explicit ok, ok_hex, or
  alpc_expect_status check for the described result.
- Observation means the result is emitted with alpc_observe_status,
  alpc_observe_scalar_output, ALPC_OBSERVE, or a trace-only probe. An
  anti-stub check is not a native behavioral assertion.
- Mixed means stable success paths are asserted while boundary, mutation, or
  validation-order results remain observations.
- Every observation row remains Win11-unknown until the same binary has run
  on the target Win11 build and the exact status and output log is archived.
- Riskier output-fault and misalignment probes use the same binary and are
  enabled at run time with ALPC_TEST_NATIVE_OBSERVE=1. Output-fault and helper
  fault probes additionally run in bounded child processes so an access
  violation or user-mode hang cannot terminate the remaining suite. The
  default run skips the environment-gated probes; no separate native-only
  source or weakened assertion set is intended.
- No test should accept STATUS_NOT_IMPLEMENTED as an expected result.

## Clean-room contracts already encoded

The following exact assertions come from permitted clean-room inspection of
Windows 11 build 26100 ARM64 binaries. They are stronger than guesses, but they
are still not a substitute for the pending same-binary native run:

- Send/wait/receive low 16-bit flags are masked;
  ALPC_MSGFLG_REPLY_MESSAGE is 0x00010000 and
  ALPC_MSGFLG_SYNC_REQUEST is 0x00020000. Reply plus sync and sync without a
  send message return STATUS_INVALID_PARAMETER_2; sync without a receive
  buffer returns STATUS_LPC_RECEIVE_BUFFER_EXPECTED.
- An undersized receive returns STATUS_BUFFER_TOO_SMALL, writes the required
  length, copies no truncated message, and leaves the message queued for a
  sufficient retry.
- Connect input TotalLength must equal sizeof(PORT_MESSAGE) plus DataLength.
  An accepted reply that exceeds client capacity returns
  STATUS_BUFFER_TOO_SMALL, writes the required length, preserves the output
  port-handle sentinel, and copies no truncated reply. The NULL-message plus
  non-NULL-length capacity cases are covered with a cooperating server.
- On Win11 26100 ARM64, ALPC_HANDLE_ATTR is 0x18 bytes, indirect input entries
  are 0x10-byte ALPC_HANDLE_ATTR32 records with a 0x200 count maximum, and
  ALPC_MESSAGE_HANDLE_INFORMATION is 0x14 bytes. Native-width 32-bit layout
  assertions are selected separately when building a 32-bit runner.
- Data-view flags use the exact 0x00070000 mask: UNMAP_EXISTING 0x00010000,
  AUTO_RELEASE 0x00020000, and NOT_SECURE 0x00040000.
- Resource-reserve IDs are ULONG, not pointer-width handles. Native probes and
  stores exactly four bytes, preserves the adjacent guard word, tags bit 31,
  and accepts a writable misaligned PULONG output.
- Cancellation rejects flags outside the low nibble with
  STATUS_INVALID_PARAMETER; MessageId zero returns STATUS_MESSAGE_NOT_FOUND;
  repeat lookup after cancellation returns STATUS_REQUEST_CANCELED. The
  returned canceled PORT_MESSAGE type and zero-length header rewrite are
  asserted, while the waiting caller's terminal NTSTATUS remains observed.
- NtQueryPortInformationProcess has no parameters and returns STATUS_WAIT_1
  directly on Windows 11 build 26100 ARM64.

## Test-file inventory

| Test | Primary scope | Current proof style | Win11 status |
|---|---|---|---|
| AlpcMessageAttribute.c | Attribute sizes, combinations, lookup, short/exact buffers, unknown flags, alignment | Mixed; documented sizes and ordinary buffers asserted, unknown behavior observed; misalignment and fault probes are child-isolated | Unknown; not run |
| AlpcCompletionList.c | Local completion-list ABI, dequeue/free/worker behavior, kernel registration, rundown, concurrency | Mixed; normal list transitions and successful registration asserted; corrupt/null/worker/kernel-list groups are independently child-isolated | Unknown; not run |
| NtAlpcConnectMatrix.c | Connect and ConnectEx accept/refuse round trips, required SID, malformed connection messages | Mixed; accept/refuse outcomes asserted, SID and malformed-message details observed | Unknown; not run |
| NtAlpcInformation.c | Query information classes, length matrix, SID/server information, set-information classes | Mixed; basic exact success and selected setters asserted, variable/native details observed | Unknown; not run |
| NtAlpcLifecycle.c | Cancellation, timeout forms, AUTO_RELEASE view cleanup, blocked close, callbacks, pending-reply disconnect | Mixed; setup, timeout/reply, cancellation header rewrite and prompt terminal semantics asserted; hazardous view cleanup paths run in bounded child processes; terminal race statuses observed | Unknown; not run |
| NtAlpcMessageAttributes.c | Context/token/direct/handle/view/security attributes, indirect 32-bit handle entries, sender query and impersonation | Mixed; transport and selected count limits asserted, returned metadata and many failures observed | Unknown; not run |
| NtAlpcMessageBoundary.c | Send/receive total/data lengths, receive capacities, malformed and misaligned messages | Mixed; successful sizes asserted, boundary statuses observed; misalignment is environment-gated | Unknown; not run |
| NtAlpcPort.c | Basic create/connect/accept, synchronous request/reply, async delivery, query and disconnect | Asserted end-to-end happy path | Unknown; not run |
| NtAlpcResources.c | Reserve/section/view/security lifetimes, size and flag boundaries, output-fault rollback | Mixed; reserve width/guard/tag, successful setup/deletion and safe mapped access asserted; boundary and repeat-delete results observed; environment-gated output faults are child-isolated | Unknown; not run |
| NtAlpcValidation.c | Validation smoke coverage for every NtAlpc syscall plus clean-room flag/status precedence rows | Mixed; proven send/cancel literals asserted, unmeasured status precedence remains observed; the invalid-input matrix is child-isolated | Unknown; not run |
| TpAlpc.c | All Tp ALPC exports, basic and extended completion lifetimes, callbacks and invalid inputs | Mixed; valid lifecycle/callback paths asserted; invalid/null, basic, and extended groups run as separate bounded child cases | Unknown; not run |
| alpc_test_utils.h | Shared setup, mutation hashing, asserted/observed status helpers and observation environment gate | Harness support, not an independent test | Unknown; not run |
| alpc_testlist.c | Minimal ALPC-only runner list for native payloads; deliberately separate from the legacy ntdll_apitest import surface | Harness support, not an independent test | Unknown; not run |

## NtAlpc syscall matrix: 23 of 23 referenced

All 23 exported syscalls have at least a validation-smoke call in
NtAlpcValidation.c. The stronger files named below add the described coverage.

| Export | Stronger coverage outside validation smoke | Assertion classification | Win11 status |
|---|---|---|---|
| NtAlpcAcceptConnectPort | NtAlpcPort, NtAlpcConnectMatrix, NtAlpcMessageBoundary: accept/refuse, request header and boundary handling | Mixed; normal accept/refuse asserted, validation boundaries observed | Unknown; not run |
| NtAlpcCancelMessage | NtAlpcLifecycle and NtAlpcValidation: context match/mismatch, all valid flag combinations, invalid high flags, zero ID, repeat cancel and late reply | Mixed; proven validation/canceled-header results asserted, caller terminal status observed | Unknown; not run |
| NtAlpcConnectPort | NtAlpcPort, NtAlpcConnectMatrix: successful round trip, refusal, SID and malformed messages | Mixed; success/refusal asserted, SID/malformed details observed | Unknown; not run |
| NtAlpcConnectPortEx | NtAlpcConnectMatrix: successful and refused extended-name connection | Mixed; client success/refusal asserted, returned details observed | Unknown; not run |
| NtAlpcCreatePort | Port, connect, lifecycle, boundary and helper setup in most syscall tests | Asserted STATUS_SUCCESS on ordinary named ports; invalid inputs observed | Unknown; not run |
| NtAlpcCreatePortSection | NtAlpcResources, NtAlpcMessageAttributes: internal/external sections, size/flag boundaries and rollback | Mixed; setup success where required, boundary/output behavior observed | Unknown; not run |
| NtAlpcCreateResourceReserve | NtAlpcResources: ULONG output ABI, adjacent guard, misalignment, size boundaries, lifetime and output-fault rollback | Mixed; four-byte write boundary and tag asserted, create status boundaries observed | Unknown; not run |
| NtAlpcCreateSectionView | NtAlpcResources, NtAlpcMessageAttributes: flag matrix, mapping, data access and message view attribute | Mixed; mapped data path asserted, flag and returned-layout details observed | Unknown; not run |
| NtAlpcCreateSecurityContext | NtAlpcResources, NtAlpcMessageAttributes: QoS matrix and security message attribute | Mixed; cleanup after success asserted, QoS and returned context observed | Unknown; not run |
| NtAlpcDeletePortSection | NtAlpcResources, NtAlpcMessageAttributes: normal cleanup, repeated deletion and in-flight use | Mixed; first deletion asserted in boundary loops, repeat/in-flight behavior observed | Unknown; not run |
| NtAlpcDeleteResourceReserve | NtAlpcResources: normal cleanup and repeated deletion | Mixed; first deletion asserted in boundary loops, repeat deletion observed | Unknown; not run |
| NtAlpcDeleteSectionView | NtAlpcResources, NtAlpcMessageAttributes: normal, repeated and in-flight deletion | Mixed; first deletion asserted in flag loops, repeat/in-flight behavior observed | Unknown; not run |
| NtAlpcDeleteSecurityContext | NtAlpcResources, NtAlpcMessageAttributes: normal, repeated and in-flight deletion | Mixed; selected first deletion asserted, repeat/in-flight behavior observed | Unknown; not run |
| NtAlpcDisconnectPort | NtAlpcPort, NtAlpcConnectMatrix, NtAlpcLifecycle: normal teardown and pending-reply disconnect | Mixed; ordinary disconnect asserted, pending/race behavior observed | Unknown; not run |
| NtAlpcImpersonateClientOfPort | NtAlpcMessageAttributes: valid received-message impersonation | Observation only for syscall result | Unknown; not run |
| NtAlpcImpersonateClientContainerOfPort | NtAlpcMessageAttributes: valid received-message container impersonation | Observation only for syscall result | Unknown; not run |
| NtAlpcOpenSenderProcess | NtAlpcMessageAttributes: open process from a live received message and log PID | Observation only for status and returned handle identity | Unknown; not run |
| NtAlpcOpenSenderThread | NtAlpcMessageAttributes: open thread from a live received message and log TID | Observation only for status and returned handle identity | Unknown; not run |
| NtAlpcQueryInformation | NtAlpcPort, NtAlpcInformation: basic short/exact/oversized/null-return-length, SID, server and session classes | Mixed; basic exact success/length asserted, remaining class and boundary results observed | Unknown; not run |
| NtAlpcQueryInformationMessage | NtAlpcMessageAttributes: SID, token modified ID, return-valued direct status before/after completion, invalid direct-output form and handle-index queries | Observation only for query statuses and returned metadata | Unknown; not run |
| NtAlpcRevokeSecurityContext | NtAlpcResources: revoke and repeated revoke during security-context lifetime | Observation only for revoke statuses | Unknown; not run |
| NtAlpcSendWaitReceivePort | Port, lifecycle, message attributes, message boundary, completion list and TpAlpc: request/reply, datagram, callback, timeout, attributes and capacities | Mixed; many happy paths and timeouts asserted, malformed/race/metadata results observed | Unknown; not run |
| NtAlpcSetInformation | NtAlpcInformation and completion-list wrappers: port attributes, completion association/list operations and invalid lengths | Mixed; selected stable setters asserted, class/length details observed | Unknown; not run |

## Legacy LPC compatibility row

| Export | Coverage | Assertion classification | Win11 status |
|---|---|---|---|
| NtQueryPortInformationProcess | NtAlpcInformation: parameterless legacy compatibility status | Exact STATUS_WAIT_1 asserted from Windows 11 build 26100 ARM64 body | Unknown; not run |

## User-mode Alpc export matrix: 15 of 15 referenced

| Export | Test | Assertion classification | Win11 status |
|---|---|---|---|
| AlpcAdjustCompletionListConcurrencyCount | AlpcCompletionList | Mixed; valid kernel-list operation asserted, invalid registration states observed | Unknown; not run |
| AlpcFreeCompletionListMessage | AlpcCompletionList, TpAlpc | Mixed; bitmap/return-count transitions asserted, repeat/null/short-message behavior observed | Unknown; not run |
| AlpcGetCompletionListLastMessageInformation | AlpcCompletionList | Asserted for ordinary values; null inputs observed | Unknown; not run |
| AlpcGetCompletionListMessageAttributes | AlpcCompletionList | Mixed; ordinary aligned result asserted, malformed offsets/nulls observed | Unknown; not run |
| AlpcGetHeaderSize | AlpcMessageAttribute | Asserted for every known attribute and combinations; unknown bits also logged | Unknown; not run |
| AlpcGetMessageAttribute | AlpcMessageAttribute | Asserted for zero, multiple, absent and present flags; unknown offsets logged | Unknown; not run |
| AlpcGetMessageFromCompletionList | AlpcCompletionList, TpAlpc | Mixed; empty/single/wrapped queues asserted, corrupt indexes/offsets and nulls observed | Unknown; not run |
| AlpcGetOutstandingCompletionListMessageCount | AlpcCompletionList, TpAlpc | Asserted for ordinary counts; underflow/null behavior observed | Unknown; not run |
| AlpcInitializeMessageAttribute | AlpcMessageAttribute | Asserted for short/exact/oversized buffers and output initialization; misalignment observed | Unknown; not run |
| AlpcMaxAllowedMessageLength | AlpcMessageAttribute | Exact constant asserted | Unknown; not run |
| AlpcRegisterCompletionList | AlpcCompletionList, TpAlpc | Mixed; valid registration asserted, size/alignment/repeat states observed | Unknown; not run |
| AlpcRegisterCompletionListWorkerThread | AlpcCompletionList | Asserted state transitions and concurrency; corrupt/null cases observed | Unknown; not run |
| AlpcRundownCompletionList | AlpcCompletionList, TpAlpc | Asserted for valid list rundown; invalid states observed | Unknown; not run |
| AlpcUnregisterCompletionList | AlpcCompletionList, TpAlpc | Asserted for valid and repeated extended lifecycle; invalid states observed | Unknown; not run |
| AlpcUnregisterCompletionListWorkerThread | AlpcCompletionList | Asserted state transitions and concurrency; corrupt/null cases observed | Unknown; not run |

## Thread-pool ALPC export matrix: 8 of 8 resolved and called

All eight functions are resolved dynamically in TpAlpc.c so the ALPC-only
runner does not acquire unrelated static imports.

| Export | Coverage | Assertion classification | Win11 status |
|---|---|---|---|
| TpAllocAlpcCompletion | Basic callback object allocation and callback delivery | Valid allocation/callback asserted; invalid inputs observed | Unknown; not run |
| TpAllocAlpcCompletionEx | Completion-list-backed allocation and callback delivery | Valid allocation/callback asserted; invalid inputs observed | Unknown; not run |
| TpAlpcRegisterCompletionList | Extended lifecycle, including repeated registration | Explicit success assertions; null-object exception behavior observed | Unknown; not run |
| TpAlpcUnregisterCompletionList | Extended lifecycle, including repeated unregister | Explicit success assertions; null-object exception behavior observed | Unknown; not run |
| TpCallbackSendAlpcMessageOnCompletion | Callback action sends a reply/message on completion | Success asserted when exercised; null/empty callback cases observed | Unknown; not run |
| TpCallbackSendPendingAlpcMessage | Callback action flushes pending send | Success asserted when exercised; empty callback case observed | Unknown; not run |
| TpReleaseAlpcCompletion | Basic and extended object cleanup | Valid lifecycle exercised; null-object exception behavior observed | Unknown; not run |
| TpWaitForAlpcCompletion | Callback completion synchronization | Valid lifecycle exercised; null-object exception behavior observed | Unknown; not run |

## Validation dimensions and explicit gaps

| Dimension | Present coverage | Remaining gap |
|---|---|---|
| Null pointers | Validation smoke plus completion-list and Tp null probes | Not every pointer of every syscall is independently varied |
| Lengths and sizes | Query short/exact/oversized, message total/data/capacity matrices, resource sizes, completion-list sizes | Several class-specific Set/Query structures still lack a full short/exact/oversized matrix |
| Misalignment | Message attributes and completion-list observations; message syscall probe is environment-gated | No systematic per-pointer syscall alignment matrix |
| Flags | Port, section, view, cancellation, handles, message attributes and callback modes | Reserved/unknown flags are not exhaustively crossed with every syscall |
| Handle classes | Invalid handle smoke, valid ports/resources, invalid indirect sender handle | Pseudo, wrong-object-type, freshly closed and stale ALPC resource handles are not systematically covered per syscall |
| Output mutation on failure | Shared buffer mutation hashes, scalar output traces and gated resource rollback probes | Not every output pointer has sentinel-before/after evidence |
| Validation precedence | Some malformed message/length/flag combinations exercise ordering | No explicit two-invalid-input precedence table for every syscall |
| 32-bit and WOW64 | Native-width 32-bit/64-bit handle-layout assertions plus ALPC_HANDLE_ATTR32 indirect-send arrays and receive-capacity tests | No true x86/WOW64 process run, no ALPC_MSGFLG_WOW64_CALL matrix and no cross-architecture structure-thunk proof |
| Concurrency | Completion-list worker stress, bounded TP waits, blocked close, callback and cancellation flows; hung helper contexts are rundown/terminated or quarantined | Disconnect/cancel/close races need repeated stress and process-isolated hang watchdog evidence |
| Security | Required SID mismatch, QoS levels, sender open and impersonation probes | AppContainer/container-token, access-denied ACL, low-integrity and cross-session cases remain unproven |
| Process boundary | Same-process client/server communication ports and sender PID/TID queries | No separate-process client, peer crash/exit, cross-token handle/view transfer, or cross-process impersonation run yet |
| Information classes | Basic, connected SID, server/session, message zone, completion-list wrapper classes and access checks | No valid IOCP association E2E, AlpcRegisterCallbackInformation contract, or asserted AlpcPortInformation query result yet |
| Malformed helper isolation | Faulting message-attribute, completion-list, TP ALPC, resource-output and syscall-validation groups run in bounded child processes; list buffers are validated before payload dereference | Child isolation cannot contain a kernel bugcheck; Win11 and ReactOS runs must still use disposable test systems |

## Runtime and native evidence still required

1. Build the minimal ALPC runner and run the identical binary on a recorded
   Win11 build, first without and then with ALPC_TEST_NATIVE_OBSERVE=1.
2. Archive OS build, architecture, binary hash, command line, full test log,
   exact NTSTATUS values, output handles/lengths and mutation records.
3. Convert observation rows to exact assertions only after the Win11 result is
   stable and understood; never derive an expected result from the ReactOS
   implementation under test.
4. Run the same binary and assertions on the staged ALPC-enabled ReactOS build
   and require an explicit terminal marker. A source or compile pass is not
   runtime proof.
5. Run true x86/WOW64, AMD64 and ARM64 variants. ALPC_HANDLE_ATTR32 use in a
   native-width process does not prove the WOW64 syscall thunk.
6. Exercise the remaining null, misalignment, invalid/pseudo/wrong-type/closed
   handle, output-mutation and two-invalid-input precedence combinations listed
   above.
7. Add AppContainer/container impersonation, completion-port concurrency,
   cancellation/disconnect races and long-running leak/reference stress.
8. Add a separate-process peer mode and disposable child-process isolation for
   malformed completion-list and thread-pool helper probes before calling the
   adversarial suite crash-contained.
9. Fill the valid IOCP association, callback-registration and complete
   information-class matrices; a 23-of-23 export-name count is not exhaustive
   per-class or per-pointer proof.
10. When ALPC support is compiled out, the fallback syscall file remains an
   intentional STATUS_NOT_IMPLEMENTED surface. That configuration is a known
   runtime gap and must be reported separately, not treated as a passing ALPC
   implementation result.
