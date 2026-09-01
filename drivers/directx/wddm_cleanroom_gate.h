/*
 * PROJECT:     ReactOS WDDM clean-room policy
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Compile-time guard for quarantined scheduler mechanisms
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#ifndef _REACTOS_WDDM_CLEANROOM_GATE_H_
#define _REACTOS_WDDM_CLEANROOM_GATE_H_

/*
 * These values describe intentional absences, not Microsoft behavior and not
 * a legal conclusion. A change requires a fresh claim/status review,
 * implementation-neutral public-contract notes, and paired native/ReactOS
 * black-box evidence maintained outside the shipped source tree.
 */
#define REACTOS_WDDM_AUTONOMOUS_GPU_CONTEXT_RUNLIST       0
#define REACTOS_WDDM_MULTI_APP_PRIORITY_TIME_ENTITLEMENT  0
#define REACTOS_WDDM_USER_MODE_WORK_SUBMISSION            0
#define REACTOS_WDDM_PRIVATE_TDR_ABI                      0

#if REACTOS_WDDM_AUTONOMOUS_GPU_CONTEXT_RUNLIST != 0
#error Autonomous GPU context run lists are quarantined pending claim review
#endif

#if REACTOS_WDDM_MULTI_APP_PRIORITY_TIME_ENTITLEMENT != 0
#error Multi-application priority and time-entitlement scheduling is quarantined pending claim review
#endif

#if REACTOS_WDDM_USER_MODE_WORK_SUBMISSION != 0
#error WDDM 3.2 user-mode work submission is not enabled in Windows 11 24H2 and is outside the audited contract
#endif

#if REACTOS_WDDM_PRIVATE_TDR_ABI != 0
#error Private TDR recovery-context exports and layouts require independent clean-room ABI clearance
#endif

#endif /* _REACTOS_WDDM_CLEANROOM_GATE_H_ */
