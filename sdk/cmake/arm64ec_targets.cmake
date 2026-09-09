# PROJECT:     ReactOS Build System
# PURPOSE:     Define the ARM64EC runtime target manifest
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

# Both compatibility runtimes share the portable DLL set. Only their
# architecture-specific loader and guest executables remain separate.
include("${CMAKE_CURRENT_LIST_DIR}/compat_runtime_targets.cmake")

set(ARM64EC_RUNTIME_MODULES ${COMPAT_RUNTIME_MODULES} ntdll_chpe)
set(ARM64EC_RUNTIME_AUXILIARY_MODULES ${COMPAT_RUNTIME_AUXILIARY_MODULES})
set(ARM64EC_RUNTIME_ALIASES ${COMPAT_RUNTIME_ALIASES})
