# Copyright 2026 Ahmed Arif
# SPDX-License-Identifier: GPL-3.0-or-later

@ stdcall -private DllCanUnloadNow()
@ stdcall -private DllGetClassObject(ptr ptr ptr)
@ stdcall -private DllRegisterServer()
@ stdcall -private DllUnregisterServer()
