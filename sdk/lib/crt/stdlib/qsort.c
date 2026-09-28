/*
 * Copyright 2000 Juergen Schmied
 * Copyright 2010 Marcus Meissner
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdlib.h>
#include <search.h>

static __inline void
swap(char *l, char *r, size_t size)
{
    char tmp;

    while (size--)
    {
        tmp = *l;
        *l++ = *r;
        *r++ = tmp;
    }
}

static void
small_sort(void *base, size_t nmemb, size_t size,
           int (__cdecl *compar)(const void *, const void *))
{
    size_t e, i;
    char *max, *p = NULL;

    for (e = nmemb; e > 1; e--)
    {
        max = base;
        for (i = 1; i < e; i++)
        {
            p = (char *)base + i * size;
            if (compar(p, max) > 0)
                max = p;
        }

        if (p != max)
            swap(p, max, size);
    }
}

static void
quick_sort(void *base, size_t nmemb, size_t size,
           int (__cdecl *compar)(const void *, const void *))
{
    size_t stack_lo[8 * sizeof(size_t)], stack_hi[8 * sizeof(size_t)];
    size_t beg, end, lo, hi, med;
    int stack_pos;

    stack_pos = 0;
    stack_lo[stack_pos] = 0;
    stack_hi[stack_pos] = nmemb - 1;

#define X(i) ((char *)base + size * (i))
    while (stack_pos >= 0)
    {
        beg = stack_lo[stack_pos];
        end = stack_hi[stack_pos--];

        if (end - beg < 8)
        {
            small_sort(X(beg), end - beg + 1, size, compar);
            continue;
        }

        lo = beg;
        hi = end;
        med = lo + (hi - lo + 1) / 2;
        if (compar(X(lo), X(med)) > 0)
            swap(X(lo), X(med), size);
        if (compar(X(lo), X(hi)) > 0)
            swap(X(lo), X(hi), size);
        if (compar(X(med), X(hi)) > 0)
            swap(X(med), X(hi), size);

        lo++;
        hi--;
        while (1)
        {
            while (lo <= hi)
            {
                if (lo != med && compar(X(lo), X(med)) > 0)
                    break;
                lo++;
            }

            while (med != hi)
            {
                if (compar(X(hi), X(med)) <= 0)
                    break;
                hi--;
            }

            if (hi < lo)
                break;

            swap(X(lo), X(hi), size);
            if (hi == med)
                med = lo;
            lo++;
            hi--;
        }

        while (hi > beg)
        {
            if (hi != med && compar(X(hi), X(med)) != 0)
                break;
            hi--;
        }

        if (hi - beg >= end - lo)
        {
            stack_lo[++stack_pos] = beg;
            stack_hi[stack_pos] = hi;
            stack_lo[++stack_pos] = lo;
            stack_hi[stack_pos] = end;
        }
        else
        {
            stack_lo[++stack_pos] = lo;
            stack_hi[stack_pos] = end;
            stack_lo[++stack_pos] = beg;
            stack_hi[stack_pos] = hi;
        }
    }
#undef X
}

void
__cdecl
qsort(void *base, size_t nmemb, size_t size,
      int (__cdecl *compar)(const void *, const void *))
{
    const size_t total_size = nmemb * size;

    if (!base && nmemb) return;
    if (!size) return;
    if (!compar) return;
    if (total_size / size != nmemb) return;
    if (nmemb < 2) return;
    quick_sort(base, nmemb, size, compar);
}
