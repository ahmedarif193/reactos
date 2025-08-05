//
// gcccompat.c
//
//      Copyright (c) 2024 ReactOS Team
//
// GCC compatibility functions to avoid linking issues with libgcc
//
// SPDX-License-Identifier: MIT
//

// GCC's __main function expects atexit to be available for registering destructors
// We provide a simple version that doesn't use atexit to avoid circular dependencies
void __main(void)
{
    // This function is called by GCC to initialize global constructors
    // For now, we provide a no-op implementation to avoid linking issues
    // Global constructors will be handled by the CRT startup code
}