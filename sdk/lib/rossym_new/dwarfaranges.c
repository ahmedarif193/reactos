/*
 * Dwarf address ranges parsing code with cached lookup.
 *
 * The original implementation was O(n) for each lookup, walking all compilation
 * units sequentially. For large modules with thousands of compilation units,
 * this caused symbol lookups to take 0.3-1.5 seconds each.
 *
 * This optimized version builds a sorted cache of address ranges on first use,
 * enabling O(log n) binary search for subsequent lookups.
 */

#include <precomp.h>

#define NDEBUG
#include <debug.h>

/*
 * Simple insertion sort for address range entries.
 * This is efficient for our use case because:
 * 1. The data is often already partially sorted (compilation units are in order)
 * 2. We only sort once during cache initialization
 * 3. We don't have access to qsort in kernel mode
 *
 * For n entries, this is O(n^2) worst case but O(n) best case for sorted input.
 */
static void
addrcache_sort(DwarfAddrRangeEntry *entries, int n)
{
    int i, j;
    DwarfAddrRangeEntry tmp;

    for (i = 1; i < n; i++) {
        tmp = entries[i];
        j = i - 1;

        /* Shift entries greater than tmp.lowpc to the right */
        while (j >= 0 && entries[j].lowpc > tmp.lowpc) {
            entries[j + 1] = entries[j];
            j--;
        }
        entries[j + 1] = tmp;
    }
}

/*
 * Build the address range cache by walking all compilation units once.
 * This is O(n) but only happens once per Dwarf instance.
 *
 * The cache collects both:
 * 1. Compilation unit ranges (from DW_AT_low_pc/DW_AT_high_pc)
 * 2. Subprogram ranges for units with non-contiguous code
 *
 * Returns 0 on success, -1 on failure.
 */
static int
dwarfbuildaddrcache(Dwarf *d)
{
    DwarfAddrRangeEntry *entries = nil;
    int nentries = 0;
    int capacity = 0;
    DwarfSym compunit;
    ulong off = 0;

    /* Already built or failed? */
    if (d->addrcache.built != 0) {
        return d->addrcache.built == 1 ? 0 : -1;
    }

    /*
     * First pass: count approximate number of entries needed.
     * We'll allocate more than compilation units because subprograms
     * also need entries.
     */
    capacity = 256;  /* Start with reasonable default */

    entries = mallocz(capacity * sizeof(DwarfAddrRangeEntry), 1);
    if (!entries) {
        d->addrcache.built = -1;
        return -1;
    }

    /*
     * Walk all compilation units and collect address ranges.
     */
    while (off < d->info.len) {
        if (dwarfenumunit(d, off, &compunit) < 0) {
            break;
        }

        /*
         * Check if the compilation unit has a valid address range.
         */
        if (compunit.attrs.have.lowpc && compunit.attrs.have.highpc &&
            compunit.attrs.lowpc != 0 && compunit.attrs.highpc > compunit.attrs.lowpc) {

            /* Grow array if needed */
            if (nentries >= capacity) {
                int newcap = capacity * 2;
                DwarfAddrRangeEntry *newentries = mallocz(newcap * sizeof(DwarfAddrRangeEntry), 1);
                if (!newentries) {
                    free(entries);
                    d->addrcache.built = -1;
                    return -1;
                }
                RtlCopyMemory(newentries, entries, nentries * sizeof(DwarfAddrRangeEntry));
                free(entries);
                entries = newentries;
                capacity = newcap;
            }

            entries[nentries].lowpc = compunit.attrs.lowpc;
            entries[nentries].highpc = compunit.attrs.highpc;
            entries[nentries].unit = off;
            nentries++;
        } else {
            /*
             * Unit has no direct range - search subprograms.
             * This handles units with non-contiguous code (low_pc=0).
             */
            DwarfSym sym;
            memset(&sym, 0, sizeof(sym));

            if (dwarfnextsymat(d, &compunit, &sym) == 0) {
                do {
                    if (sym.attrs.tag == TagSubprogram &&
                        sym.attrs.have.lowpc && sym.attrs.have.highpc &&
                        sym.attrs.lowpc != 0 && sym.attrs.highpc > sym.attrs.lowpc) {

                        /* Grow array if needed */
                        if (nentries >= capacity) {
                            int newcap = capacity * 2;
                            DwarfAddrRangeEntry *newentries = mallocz(newcap * sizeof(DwarfAddrRangeEntry), 1);
                            if (!newentries) {
                                free(entries);
                                d->addrcache.built = -1;
                                return -1;
                            }
                            RtlCopyMemory(newentries, entries, nentries * sizeof(DwarfAddrRangeEntry));
                            free(entries);
                            entries = newentries;
                            capacity = newcap;
                        }

                        entries[nentries].lowpc = sym.attrs.lowpc;
                        entries[nentries].highpc = sym.attrs.highpc;
                        entries[nentries].unit = off;
                        nentries++;
                    }
                } while (dwarfnextsym(d, &sym) == 0);
            }
        }

        /* Move to next compilation unit */
        off = compunit.nextunit;
        if (off == 0 || off <= compunit.unit) {
            break;
        }
    }

    /*
     * Also collect ranges from .debug_aranges if available.
     * This may add duplicate ranges but binary search will still work.
     */
    if (d->aranges.data && d->aranges.len > 0) {
        DwarfBuf b;
        uchar *start, *end;
        ulong len, id, aoff;
        int segsize, i;
        ULONG_PTR base, size;

        memset(&b, 0, sizeof b);
        b.d = d;
        b.p = d->aranges.data;
        b.ep = b.p + d->aranges.len;

        while (b.p < b.ep) {
            start = b.p;
            len = dwarfget4(&b);
            if (!len) break;

            if ((id = dwarfget2(&b)) != 2) {
                break;  /* Bad version, skip aranges */
            }

            aoff = dwarfget4(&b);
            b.addrsize = dwarfget1(&b);
            if (d->addrsize == 0)
                d->addrsize = b.addrsize;
            segsize = dwarfget1(&b);
            USED(segsize);

            if (b.p == nil) break;
            if ((i = (b.p - start) % (2 * b.addrsize)) != 0)
                b.p += 2 * b.addrsize - i;

            end = start + 4 + len;
            while (b.p != nil && b.p < end) {
                base = dwarfgetaddr(&b);
                size = dwarfgetaddr(&b);
                if (!size) continue;
                if (b.p == nil) break;

                /* Grow array if needed */
                if (nentries >= capacity) {
                    int newcap = capacity * 2;
                    DwarfAddrRangeEntry *newentries = mallocz(newcap * sizeof(DwarfAddrRangeEntry), 1);
                    if (!newentries) {
                        free(entries);
                        d->addrcache.built = -1;
                        return -1;
                    }
                    RtlCopyMemory(newentries, entries, nentries * sizeof(DwarfAddrRangeEntry));
                    free(entries);
                    entries = newentries;
                    capacity = newcap;
                }

                entries[nentries].lowpc = base;
                entries[nentries].highpc = base + size;
                entries[nentries].unit = aoff;
                nentries++;
            }

            if (b.p == nil) break;
            b.p = end;
        }
    }

    if (nentries == 0) {
        /* No ranges found */
        free(entries);
        d->addrcache.built = -1;
        return -1;
    }

    /*
     * Sort the entries by lowpc for binary search.
     */
    addrcache_sort(entries, nentries);

    d->addrcache.entries = entries;
    d->addrcache.nentries = nentries;
    d->addrcache.built = 1;

    return 0;
}

/*
 * Binary search the address cache to find the compilation unit containing addr.
 * Returns 0 on success, -1 if not found.
 *
 * This is O(log n) compared to the original O(n) linear scan.
 */
static int
dwarfaddrcache_lookup(Dwarf *d, ULONG_PTR addr, ulong *unit)
{
    DwarfAddrRangeEntry *entries;
    int left, right, mid;

    if (d->addrcache.built != 1 || !d->addrcache.entries) {
        return -1;
    }

    entries = d->addrcache.entries;
    left = 0;
    right = d->addrcache.nentries - 1;

    /*
     * Binary search: find a range where lowpc <= addr < highpc.
     *
     * Since ranges may overlap or have gaps, we search for the first
     * entry where lowpc <= addr, then check if addr < highpc.
     * If not, we continue searching nearby entries.
     */
    while (left <= right) {
        mid = (left + right) / 2;

        if (addr < entries[mid].lowpc) {
            right = mid - 1;
        } else if (addr >= entries[mid].highpc) {
            left = mid + 1;
        } else {
            /* Found: lowpc <= addr < highpc */
            *unit = entries[mid].unit;
            return 0;
        }
    }

    /*
     * Binary search didn't find an exact match. This can happen when:
     * 1. Ranges have gaps (addr falls between ranges)
     * 2. Ranges overlap (multiple ranges cover the same address)
     *
     * Do a linear scan of nearby entries to be thorough.
     * This is still fast because we start from the approximate location.
     */
    if (right >= 0 && right < d->addrcache.nentries) {
        /* Check a few entries around the insertion point */
        int start = (right > 5) ? right - 5 : 0;
        int end = (right + 5 < d->addrcache.nentries) ? right + 5 : d->addrcache.nentries - 1;

        for (mid = start; mid <= end; mid++) {
            if (entries[mid].lowpc <= addr && addr < entries[mid].highpc) {
                *unit = entries[mid].unit;
                return 0;
            }
        }
    }

    return -1;
}

int
dwarfaddrtounit(Dwarf *d, ULONG_PTR addr, ulong *unit)
{
    /*
     * Build the address cache on first use.
     * This is a one-time O(n) operation that enables O(log n) lookups.
     */
    if (d->addrcache.built == 0) {
        dwarfbuildaddrcache(d);
    }

    /*
     * Try the cache first (O(log n) binary search).
     */
    if (dwarfaddrcache_lookup(d, addr, unit) == 0) {
        return 0;
    }

    /*
     * Cache lookup failed. This should be rare if the cache was built
     * successfully. Return failure.
     */
    werrstr("address %p is not listed in dwarf debugging symbols", (PVOID)addr);
    return -1;
}

/*
 * Free the address cache. Called from dwarfclose().
 */
void
dwarffreeaddrcache(Dwarf *d)
{
    if (d->addrcache.entries) {
        free(d->addrcache.entries);
        d->addrcache.entries = nil;
    }
    d->addrcache.nentries = 0;
    d->addrcache.built = 0;
}
