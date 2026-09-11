/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
 */
/* Exercise the production counter core with host atomics/QPC shims. */
#define _DEFAULT_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#undef NDEBUG /* These checks must also run in a Release host build. */
#endif
#include <assert.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <stddef.h>
typedef int32_t LONG, BOOL, HRESULT;
typedef uint32_t ULONG;
typedef int64_t LONG64;
typedef uint64_t ULONGLONG;
typedef struct { int64_t QuadPart; } LARGE_INTEGER;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define InterlockedIncrement(p) (__atomic_add_fetch((p),1,__ATOMIC_SEQ_CST))
#define InterlockedDecrement(p) (__atomic_sub_fetch((p),1,__ATOMIC_SEQ_CST))
#define InterlockedIncrement64 InterlockedIncrement
#define InterlockedExchange(p,v) __atomic_exchange_n((p),(v),__ATOMIC_SEQ_CST)
#define InterlockedExchangeAdd64(p,v) __atomic_fetch_add((p),(v),__ATOMIC_SEQ_CST)
static LONG InterlockedCompareExchange(volatile LONG *p,LONG v,LONG old) { __atomic_compare_exchange_n(p,&old,v,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST); return old; }
static LONG64 InterlockedCompareExchange64(volatile LONG64 *p,LONG64 v,LONG64 old) { __atomic_compare_exchange_n(p,&old,v,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST); return old; }
static uint64_t clocks, fixed_time;
static int fixed_clock;
static uint64_t now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (uint64_t)t.tv_sec*1000000000+t.tv_nsec; }
static void QueryPerformanceCounter(LARGE_INTEGER *t) { __atomic_add_fetch(&clocks,1,__ATOMIC_RELAXED); t->QuadPart=fixed_clock ? fixed_time : now(); }
static void QueryPerformanceFrequency(LARGE_INTEGER *t) { t->QuadPart=1000000000; }
static void Sleep(unsigned ms) { usleep(ms*1000); }
#include "dwmpresenttracecore.h"
static DPT_BANK bank;
static volatile int run;
static void *writer(void *p) {
    unsigned n=(uintptr_t)p;
    while (__atomic_load_n(&run,__ATOMIC_ACQUIRE)) {
        DPT_SCOPE s=DptBegin(&bank,DPT_FRAME);
        if (n&1) sched_yield();
        DptEnd(&bank,s,n&1,16);
        DptCount(&bank,DPT_BLUR_HIT,32);
    }
    return 0;
}
static void check(const DPT_DOMAIN *d) {
    for(unsigned i=0;i<DPT_METRIC_COUNT;i++) {
        const DPT_COUNTER *c=&d->Counter[i];
        assert(c->Completed<=c->Entered);
        assert(c->Failed<=c->Completed);
        assert(c->MaxTicks<=c->Ticks);
        if(c->MinTicks!=UINT64_MAX) {
            assert(c->Completed && c->MinTicks<=c->MaxTicks);
            assert(c->MinTicks<=c->Ticks/c->Completed);
        }
    }
    assert(d->Stop>=d->Start);
    assert(d->Counter[DPT_FRAME].Bytes==d->Counter[DPT_FRAME].Completed*16);
}
int main(void) {
    DPT_DOMAIN out,frozen;
    DPT_REQUEST req={sizeof(req),DPT_VERSION,DPT_START,1};
    _Static_assert(sizeof(DPT_REQUEST)==16,"request ABI");
    _Static_assert(sizeof(DPT_COUNTER)==56,"counter ABI");
    _Static_assert(offsetof(DPT_DOMAIN,Counter)==40,"domain ABI");
    _Static_assert(offsetof(DPT_SNAPSHOT,Domain)==48,"snapshot ABI");
    uint64_t t=now();
    for(int i=0;i<1000000;i++) { DPT_SCOPE s=DptBegin(&bank,DPT_FRAME); DptEnd(&bank,s,TRUE,0); }
    uint64_t disabled=now()-t;
    assert(!clocks && !bank.Data.Counter[0].Entered);
    req.Version++;
    assert(DptControl(&bank,&req,&out,sizeof(out),123)<0);
    req.Version--;
    assert(DptControl(&bank,&req,&out,sizeof(out)-1,123)<0);
    assert(DptControl(&bank,&req,&out,sizeof(out),123)==0);
    assert(DptControl(&bank,&req,&out,sizeof(out),123)<0);
    DPT_SCOPE old=DptBegin(&bank,DPT_FRAME);
    req.Operation=DPT_QUERY;
    assert(DptControl(&bank,&req,&out,sizeof(out),123)<0);
    req.Operation=DPT_STOP;req.Session++;
    assert(DptControl(&bank,&req,&out,sizeof(out),123)<0);
    req.Session--;
    t=now();
    assert(DptControl(&bank,&req,&out,sizeof(out),123)==0);
    /* The CTest timeout catches a wait for the still unfinished scope. */
    assert(out.Counter[0].Entered==1 && out.Counter[0].Completed==0);
    frozen=out;
    DptEnd(&bank,old,TRUE,999);
    req.Operation=DPT_QUERY;
    assert(DptControl(&bank,&req,&out,sizeof(out),123)==0 && !memcmp(&out,&frozen,sizeof(out)));
    req.Operation=DPT_START;req.Session++;
    assert(DptControl(&bank,&req,&out,sizeof(out),123)==0);
    DptEnd(&bank,old,FALSE,999);
    t=now();
    for(int i=0;i<100000;i++) { DPT_SCOPE s=DptBegin(&bank,DPT_FRAME); DptEnd(&bank,s,TRUE,16); }
    uint64_t active=now()-t;
    req.Operation=DPT_STOP;
    assert(DptControl(&bank,&req,&out,sizeof(out),123)==0);
    assert(out.Counter[0].Entered==100000 && out.Counter[0].Completed==100000 && !out.Counter[0].Failed);
    req.Operation=DPT_START;req.Session++;
    assert(DptControl(&bank,&req,&out,sizeof(out),123)==0);
    fixed_clock=1;fixed_time=100;
    DPT_SCOPE first=DptBegin(&bank,DPT_FRAME);
    fixed_time=107;DptEnd(&bank,first,TRUE,16);
    fixed_time=110;
    DPT_SCOPE second=DptBegin(&bank,DPT_FRAME);
    DptEnd(&bank,second,TRUE,16); /* a zero-tick sample is a real minimum */
    DptCount(&bank,DPT_BLUR_HIT,32);
    fixed_clock=0;req.Operation=DPT_STOP;
    assert(DptControl(&bank,&req,&out,sizeof(out),123)==0);
    assert(out.Counter[DPT_FRAME].MinTicks==0);
    assert(out.Counter[DPT_FRAME].MaxTicks==7 && out.Counter[DPT_FRAME].Ticks==7);
    assert(out.Counter[DPT_BLUR_HIT].MinTicks==UINT64_MAX);
    check(&out);
    pthread_t threads[4];run=1;
    for(unsigned i=0;i<4;i++) assert(!pthread_create(&threads[i],0,writer,(void *)(uintptr_t)i));
    for(unsigned i=0;i<500;i++) {
        req.Session++;req.Operation=DPT_START;
        assert(DptControl(&bank,&req,&out,sizeof(out),123)==0);
        usleep(50);
        req.Operation=DPT_STOP;
        assert(DptControl(&bank,&req,&out,sizeof(out),123)==0);
        check(&out);frozen=out;
        req.Operation=DPT_QUERY;
        assert(DptControl(&bank,&req,&out,sizeof(out),123)==0);
        assert(!memcmp(&out,&frozen,sizeof(out)));
    }
    __atomic_store_n(&run,0,__ATOMIC_RELEASE);
    for(int i=0;i<4;i++) pthread_join(threads[i],0);
    printf("PASS: disabled zero clocks, version/size/session rejection, unfinished stop, stale-token isolation, 500 concurrent reset/stop snapshots\n");
    printf("HOST_ONLY scope overhead disabled=%.2f ns active=%.2f ns (not Pi timing)\n",disabled/1e6,active/1e5);
    printf("ABI request=%zu counter=%zu domain=%zu snapshot=%zu\n",sizeof(req),sizeof(DPT_COUNTER),sizeof(out),sizeof(DPT_SNAPSHOT));
}
