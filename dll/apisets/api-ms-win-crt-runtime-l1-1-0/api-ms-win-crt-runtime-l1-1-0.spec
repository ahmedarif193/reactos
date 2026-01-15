# api-ms-win-crt-runtime-l1-1-0 - Auto-generated forwarders to ucrtbase.dll
# Generated from ucrtbase.spec exports matching category patterns

@ cdecl _Exit(long) ucrtbase._Exit
@ cdecl __p___argc() ucrtbase.__p___argc
@ cdecl __p___argv() ucrtbase.__p___argv
@ cdecl __p___wargv() ucrtbase.__p___wargv
@ cdecl __p__acmdln() ucrtbase.__p__acmdln
@ cdecl __p__wcmdln() ucrtbase.__p__wcmdln
@ cdecl __std_terminate() ucrtbase.__std_terminate
@ cdecl _beginthread(ptr long ptr) ucrtbase._beginthread
@ cdecl _beginthreadex(ptr long ptr ptr long ptr) ucrtbase._beginthreadex
@ cdecl _c_exit() ucrtbase._c_exit
@ cdecl _cexit() ucrtbase._cexit
@ cdecl _clearfp() ucrtbase._clearfp
@ cdecl _control87(long long) ucrtbase._control87
@ cdecl _controlfp(long long) ucrtbase._controlfp
@ cdecl _fpreset() ucrtbase._fpreset
@ cdecl _controlfp_s(ptr long long) ucrtbase._controlfp_s
@ cdecl _crt_at_quick_exit(ptr) ucrtbase._crt_at_quick_exit
@ cdecl _crt_atexit(ptr) ucrtbase._crt_atexit
@ cdecl _endthread() ucrtbase._endthread
@ cdecl _endthreadex(long) ucrtbase._endthreadex
@ cdecl _execute_onexit_table(ptr) ucrtbase._execute_onexit_table
@ cdecl _exit(long) ucrtbase._exit
@ cdecl _get_pgmptr(ptr) ucrtbase._get_pgmptr
@ cdecl _get_terminate() ucrtbase._get_terminate
@ cdecl _get_wpgmptr(ptr) ucrtbase._get_wpgmptr
@ cdecl _initialize_onexit_table(ptr) ucrtbase._initialize_onexit_table
@ cdecl _o___p___argc() ucrtbase._o___p___argc
@ cdecl _o___p___argv() ucrtbase._o___p___argv
@ cdecl _o___p___wargv() ucrtbase._o___p___wargv
@ cdecl _o___p__acmdln() ucrtbase._o___p__acmdln
@ cdecl _o___p__wcmdln() ucrtbase._o___p__wcmdln
@ cdecl _o__beginthread(ptr long ptr) ucrtbase._o__beginthread
@ cdecl _o__beginthreadex(ptr long ptr ptr long ptr) ucrtbase._o__beginthreadex
@ cdecl _o__cexit() ucrtbase._o__cexit
@ cdecl _o__controlfp_s(ptr long long) ucrtbase._o__controlfp_s
@ cdecl _o__crt_atexit(ptr) ucrtbase._o__crt_atexit
@ cdecl _o__endthread() ucrtbase._o__endthread
@ cdecl _o__endthreadex(long) ucrtbase._o__endthreadex
@ cdecl _o__execute_onexit_table(ptr) ucrtbase._o__execute_onexit_table
@ cdecl _o__exit(long) ucrtbase._o__exit
@ cdecl _o__get_pgmptr(ptr) ucrtbase._o__get_pgmptr
@ cdecl _o__get_terminate() ucrtbase._o__get_terminate
@ cdecl _o__get_wpgmptr(ptr) ucrtbase._o__get_wpgmptr
@ cdecl _o__initialize_onexit_table(ptr) ucrtbase._o__initialize_onexit_table
@ cdecl _o__register_onexit_function(ptr ptr) ucrtbase._o__register_onexit_function
@ cdecl _o__set_abort_behavior(long long) ucrtbase._o__set_abort_behavior
@ cdecl _o__set_app_type(long) ucrtbase._o__set_app_type
@ cdecl _o__set_fmode(long) ucrtbase._o__set_fmode
@ cdecl _o__set_invalid_parameter_handler(ptr) ucrtbase._o__set_invalid_parameter_handler
@ cdecl _o_abort() ucrtbase._o_abort
@ cdecl _o_exit(long) ucrtbase._o_exit
@ cdecl _o_set_terminate(ptr) ucrtbase._o_set_terminate
@ cdecl _o_terminate() ucrtbase._o_terminate
@ cdecl _register_onexit_function(ptr ptr) ucrtbase._register_onexit_function
@ cdecl _register_thread_local_exe_atexit_callback(ptr) ucrtbase._register_thread_local_exe_atexit_callback
@ cdecl _get_fmode(ptr) ucrtbase._get_fmode
@ cdecl _set_abort_behavior(long long) ucrtbase._set_abort_behavior
@ cdecl _set_app_type(long) ucrtbase._set_app_type
@ cdecl _set_controlfp(long long) ucrtbase._set_controlfp
@ cdecl _set_fmode(long) ucrtbase._set_fmode
@ cdecl _set_error_mode(long) ucrtbase._set_error_mode
@ cdecl _set_invalid_parameter_handler(ptr) ucrtbase._set_invalid_parameter_handler
@ cdecl _statusfp() ucrtbase._statusfp
@ cdecl abort() ucrtbase.abort
@ cdecl exit(long) ucrtbase.exit
@ cdecl quick_exit(long) ucrtbase.quick_exit
@ cdecl set_terminate(ptr) ucrtbase.set_terminate
@ cdecl terminate() ucrtbase.terminate

# Additional startup/initialization functions
@ cdecl __p__commode() ucrtbase.__p__commode
@ cdecl __p__fmode() ucrtbase.__p__fmode
@ cdecl __p__pgmptr() ucrtbase.__p__pgmptr
@ cdecl __p__wpgmptr() ucrtbase.__p__wpgmptr
@ cdecl _configure_narrow_argv(long) ucrtbase._configure_narrow_argv
@ cdecl _configure_wide_argv(long) ucrtbase._configure_wide_argv
@ cdecl _get_initial_narrow_environment() ucrtbase._get_initial_narrow_environment
@ cdecl _get_initial_wide_environment() ucrtbase._get_initial_wide_environment
@ cdecl _initialize_narrow_environment() ucrtbase._initialize_narrow_environment
@ cdecl _initialize_wide_environment() ucrtbase._initialize_wide_environment
@ cdecl _initterm(ptr ptr) ucrtbase._initterm
@ cdecl _initterm_e(ptr ptr) ucrtbase._initterm_e
@ cdecl _o___p__commode() ucrtbase._o___p__commode
@ cdecl _o___p__fmode() ucrtbase._o___p__fmode
@ cdecl _o___p__pgmptr() ucrtbase._o___p__pgmptr
@ cdecl _o___p__wpgmptr() ucrtbase._o___p__wpgmptr
@ cdecl _o__configure_narrow_argv(long) ucrtbase._o__configure_narrow_argv
@ cdecl _o__configure_wide_argv(long) ucrtbase._o__configure_wide_argv
@ cdecl _o__get_fmode(ptr) ucrtbase._o__get_fmode
@ cdecl _o__get_initial_narrow_environment() ucrtbase._o__get_initial_narrow_environment
@ cdecl _o__get_initial_wide_environment() ucrtbase._o__get_initial_wide_environment
@ cdecl _o__initialize_narrow_environment() ucrtbase._o__initialize_narrow_environment
@ cdecl _o__initialize_wide_environment() ucrtbase._o__initialize_wide_environment

# Additional runtime/error handling functions
@ cdecl _assert(str str long) ucrtbase._assert
@ cdecl _errno() ucrtbase._errno
@ cdecl _fpieee_flt(long ptr ptr) ucrtbase._fpieee_flt
@ cdecl _get_doserrno(ptr) ucrtbase._get_doserrno
@ cdecl _get_errno(ptr) ucrtbase._get_errno
@ cdecl _set_doserrno(long) ucrtbase._set_doserrno
@ cdecl _set_errno(long) ucrtbase._set_errno
@ cdecl _strerror(long) ucrtbase._strerror
@ cdecl _wassert(wstr wstr long) ucrtbase._wassert
@ cdecl _wperror(wstr) ucrtbase._wperror
@ cdecl perror(str) ucrtbase.perror
@ cdecl raise(long) ucrtbase.raise
@ cdecl signal(long long) ucrtbase.signal
