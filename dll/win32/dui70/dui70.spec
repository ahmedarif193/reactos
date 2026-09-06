# DirectUI runtime entry points

@ stdcall InitProcessPriv(ptr ptr ptr ptr ptr)
@ stdcall InitThread(ptr)
@ stdcall SkipDLLUnloadInitChecks()
@ stdcall UnInitProcessPriv(ptr)
@ stdcall UnInitThread()
@ stdcall -private DllInitialize(long long ptr) DllMain
