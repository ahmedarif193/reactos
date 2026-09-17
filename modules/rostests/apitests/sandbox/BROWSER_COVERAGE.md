# Chrome/Firefox Windows sandbox parity checklist

This directory tests the Windows OS contracts used by the Chromium and Firefox
sandbox implementations.  Passing unit-level API calls is not sufficient: each
row must also have a browser-style child-process test, a Windows 11 oracle run,
and a ReactOS run.  Tests intentionally use the same combinations used by the
brokers (restricted primary token, initial impersonation token, job, desktop,
startup attributes, mitigations, and AppContainer/LPAC).

The source audit was refreshed against Chromium `main` at
`7c6967c9f191d62d8591aecefbe12719319e7272` and Mozilla `gecko-dev` at
`5836a062726f715fda621338a17b51aff30d0a8c`.  Principal upstream files are
`sandbox/win/src/{restricted_token_utils,restricted_token,job,window,
startup_information_helper,process_mitigations,broker_services,
app_container_base}.cc` and Mozilla's
`security/sandbox/win/src/sandboxbroker/sandboxBroker.cpp` plus its Chromium
fork.

Status meanings:

- `covered`: the browser call path has an end-to-end test, a Windows 11 oracle
  result, and a passing ReactOS result.

| Browser-required contract | Test coverage | Status |
| --- | --- | --- |
| Restricted primary and impersonation tokens; deny-only groups; restricted SIDs; privilege deletion; lockdown default DACL | `RestrictedToken`, `Broker` | covered |
| Initial thread token followed by `RevertToSelf`; lockdown primary token used by `CreateProcessAsUserW` | `Broker` | covered |
| Mandatory integrity labels on tokens, files, registry keys, processes, threads, desktops, and named kernel objects | `Integrity`, `Broker`, `AppContainer`, `Desktop` | covered |
| Token object label hardening with `NO_READ_UP` and `NO_EXECUTE_UP` | `Integrity`, `RestrictedToken` | covered |
| Token security attributes (`TSA://ProcUnique`) and the conditional ACE used for Chromium process isolation | `RestrictedToken` | covered; verifies the two-value claim, duplicate/child lifetime, callback ACE, and cross-process access denial |
| Alternate window station/desktop creation, DACL cloning, restricted-SID deny ACE, integrity label, and target attachment | `Desktop`, `Broker` | covered |
| Job extended limits: active-process, process/job memory, die-on-exception, kill-on-close, breakaway | `JobLifecycle`, `JobMemory`, `Broker` | covered |
| Job UI restrictions and `UserHandleGrantAccess` | `JobUi` | covered |
| Job completion-port notifications and accounting, including new/exit/active-zero | `JobLifecycle`, `JobMemory` | covered |
| `PROC_THREAD_ATTRIBUTE_JOB_LIST` launch-time assignment | `JobMemory`, `JobUi`, `Broker` | covered |
| `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`, including proof that an inheritable but unlisted handle is excluded | `Broker` | covered |
| Mitigation attribute: DEP/SEHOP/ASLR/heap termination | `KernelBase`, `Mitigations` | covered; DEP is permanent on 64-bit, heap termination covers Chromium's runtime API and creation flag, and Chromium does not apply DEP/SEHOP creation bits on 64-bit |
| Strict-handle checks and invalid-handle exception behavior | `KernelBase` | covered |
| Win32k system-call lockdown, including runtime enablement before User32 loads | minimal `sandbox_helper` via `Mitigations` | covered |
| Dynamic-code prohibition, thread opt-out, and executable allocation/protection denial | `KernelBase`, minimal `sandbox_helper` | covered |
| Child-process restriction and exact `ERROR_CHILD_PROCESS_BLOCKED` behavior | minimal `sandbox_helper` via `Mitigations` | covered |
| Extension-point disable/AppInit, non-system font block, and image-load policies | `KernelBase` | covered |
| Microsoft-signed binary policy, including unsigned DLL rejection and signed system DLL loading | minimal `sandbox_helper` plus `sandbox_unsigned.dll` via `Mitigations` | covered |
| Delayed DLL-search-order lockdown used by Chromium and Firefox (`SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)`) | minimal `sandbox_helper` via `Mitigations` | covered; proves a current-directory DLL loads before lockdown and a different bare-name current-directory DLL is rejected afterward, including concurrent torture launches |
| KTM component filter (`PROC_THREAD_ATTRIBUTE_COMPONENT_FILTER` with `COMPONENT_KTM`) | minimal `sandbox_helper` via `Mitigations` | covered; `CreateTransactionManager` is denied with exact `ERROR_ACCESS_DENIED` behavior in direct and concurrent torture launches |
| Remaining policy2 flags used by current Chromium (CET, module tampering, indirect-branch prediction, FSCTL, core sharing) | minimal `sandbox_helper` via `Mitigations` | covered; CET behavior is exercised when the parent image has CET enabled, matching Chromium's own gate |
| Security-capabilities and all-application-packages startup attributes; AppContainer and LPAC access behavior | `AppContainer`, `KernelBase` | covered |
| `NtCreateLowBoxToken` package/capability state, duplication, access checks, named-object redirection, and saved-handle lifetime | `AppContainer`, `KernelBase` | covered |
| AppContainer profile create/open/delete, folder and registry locations, including effective lowbox-thread-token lookup | `AppContainer`, `SecurityApi` | covered |
| Current Chromium profile registration (`AppContainerRegisterSid`, `LookupMoniker`, `UnregisterSid`, `FreeMemory`) | `KernelBase` | covered |
| Named and well-known capability SID derivation and classification | `KernelBase`, `SecurityApi` | covered |
| Package identity and AppContainer named-object path helpers | `SecurityApi`, `KernelBase` | covered |
| AppContainer/resource/scoped-policy ACE construction and lookup | `SecurityApi` | covered |
| BNO isolation process attribute, token state, namespace redirection, and private boundary namespaces | `PrivateNamespace` | covered |
| UIPI message filtering from lower-integrity targets | `Uipi`, `Broker` | covered; expectations follow native `EnableLUA` policy state and still enforce filter transitions |
| Browser-realistic composite launch under concurrent/torture load | `Broker`, `Desktop`, `AppContainer`, `Mitigations` | covered; includes concurrent restricted-token, alternate-desktop, AppContainer/LPAC, job, and mitigation variants |

## Acceptance results

- Windows 11 oracle: 13 suites, 863 assertions, zero failures, and one expected
  CET gate skip.
- ReactOS ARM64: 13 suites, 864 assertions, zero failures, and one expected CET
  gate skip in two consecutive complete runs.
- The one-assertion count difference is intentional: the ReactOS image exercises
  the enabled-UIPI branch, while the Windows 11 oracle image reports `EnableLUA`
  disabled. All common branches and their outcomes match.
- Exact final payload hashes are
  `5fb599ad063cb264faf052970f6a6868d06f5a7228a3af3a659a17accee56a79`
  (`sandbox_apitest.exe`),
  `5e1c77bad72d1d694a68e7cf07095e01c4c73a372dadddfde30903945572b6a1`
  (`sandbox_helper.exe`), and
  `5d95c4096d1a1bedfdc9c6ff2d6cf9f2341982086bdc27c32aa211c4cfa9cec6`
  (`sandbox_unsigned.dll`).
- Final evidence is retained in
  `sandbox-win11-results/win11-final-20260917/latest/` and
  `sandbox-reactos-results/reactos-final-expanded-r{2,3}.log` under the build
  directory.
- The final composite gate includes the browser token, initial-token transition,
  job, desktop, handle allowlist, mitigations, BNO/AppContainer namespace, and
  sequential plus concurrent torture launches, including KTM filtering and DLL
  search lockdown. No audited browser OS contract is left with a known coverage
  gap.
