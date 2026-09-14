/* SPDX-License-Identifier: MIT */
#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"
#include <cstdio>
#include <cstdlib>

static bool FailAllocation;
static unsigned Constructors;
static unsigned Checks;
static unsigned Failures;

extern "C" void *
NtfsAllocatePoolWithTag(POOL_TYPE, size_t Size, ULONG)
{
    return FailAllocation ? nullptr : std::malloc(Size);
}

extern "C" void NtfsFreePool(void *Pointer)
{
    std::free(Pointer);
}

struct Probe
{
    Probe() { ++Constructors; }
};

static void Check(bool Result, const char *Message)
{
    ++Checks;
    if (!Result)
    {
        ++Failures;
        std::printf("Test failed: %s\n", Message);
    }
}

int main()
{
    FailAllocation = true;
    Probe *Pointer = new(NonPagedPool) Probe;
    Check(Pointer == nullptr, "failed pool new returned an object");
    Check(Constructors == 0, "failed pool new invoked the constructor");
    Pointer = new(NonPagedPool, TAG_NTFS) Probe;
    Check(Pointer == nullptr, "failed tagged pool new returned an object");
    Check(Constructors == 0, "failed tagged pool new invoked the constructor");
    Probe *Array = new(NonPagedPool) Probe[3];
    Check(Array == nullptr, "failed pool array new returned an object");
    Check(Constructors == 0, "failed pool array new invoked constructors");
    Array = new(NonPagedPool, TAG_NTFS) Probe[3];
    Check(Array == nullptr, "failed tagged pool array new returned an object");
    Check(Constructors == 0, "failed tagged pool array new invoked constructors");

    FailAllocation = false;
    Constructors = 0;
    Pointer = new(NonPagedPool) Probe;
    Check(Pointer != nullptr && Constructors == 1, "successful pool new");
    delete Pointer;
    Pointer = new(NonPagedPool, TAG_NTFS) Probe;
    Check(Pointer != nullptr && Constructors == 2, "successful tagged pool new");
    delete Pointer;
    Array = new(NonPagedPool) Probe[3];
    Check(Array != nullptr && Constructors == 5, "successful pool array new");
    delete[] Array;
    Array = new(NonPagedPool, TAG_NTFS) Probe[3];
    Check(Array != nullptr && Constructors == 8, "successful tagged pool array new");
    delete[] Array;

    std::printf("ntfslib_pool_new: %u tests executed (0 marked as todo, %u failures), 0 skipped.\n", Checks, Failures);
    return Failures ? 1 : 0;
}
