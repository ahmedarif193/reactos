# PROJECT:     ReactOS Raspberry Pi 3 video support
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     VideoCore H.264 Media Foundation encoder exports
# COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>

@ stdcall -private DllCanUnloadNow()
@ stdcall -private DllGetClassObject(ptr ptr ptr)
@ stdcall -private DllRegisterServer()
@ stdcall -private DllUnregisterServer()
