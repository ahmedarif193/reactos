# Memory-manager development

Use only the `nvs/` (NT Virtual Memory Subsystem) and `cc/` implementations. The user retired the old
`mm/`, `vmm_backup/`, `cc_backup/`, pool backing bridge, and legacy debugger
extension from this worktree on 2026-09-20.

Do not seek, read, restore, or use the retired implementations as references,
including copies in temporary storage or Git history. They are not a fallback.
Implement missing behavior in NVS and validate it with host-native
regression tests first. Keep public NT ABI compatibility without importing the
old internal implementation.

Do not spawn subagents, commit, or push unless the user explicitly asks.
Do not add code comments. Preserve unrelated worktree changes.
