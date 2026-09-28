#include <precomp.h>

static int
wctoint(wchar_t c)
{
  static const wchar_t zeros[] =
  {
    0x0660, 0x06f0, 0x0966, 0x09e6, 0x0a66, 0x0ae6, 0x0b66, 0x0c66, 0x0ce6,
    0x0d66, 0x0e50, 0x0ed0, 0x0f20, 0x1040, 0x17e0, 0x1810, 0xff10
  };
  unsigned int i;

  if (c >= L'0' && c <= L'9')
    return c - L'0';
  if (c >= L'A' && c <= L'Z')
    return c - L'A' + 10;
  if (c >= L'a' && c <= L'z')
    return c - L'a' + 10;
  for (i = 0; i < sizeof(zeros) / sizeof(zeros[0]) && c >= zeros[i]; i++)
    if (c <= zeros[i] + 9)
      return c - zeros[i];
  return -1;
}

/*
 * Convert a unicode string to an unsigned long integer.
 *
 * Ignores `locale' stuff.  Assumes that the upper and lower case
 * alphabets and digits are each contiguous.
 *
 * @implemented
 */
unsigned long
CDECL
wcstoul(const wchar_t *nptr, wchar_t **endptr, int base)
{
  const wchar_t *s = nptr;
  unsigned long acc;
  int c;
  unsigned long cutoff;
  int neg = 0, any, cutlim;

  /*
   * See strtol for comments as to the logic used.
   */
  do {
    c = *s++;
  } while (iswctype(c, _SPACE));
  if (c == L'-')
  {
    neg = 1;
    c = *s++;
  }
  else if (c == L'+')
    c = *s++;
  if ((base == 0 || base == 16) &&
      wctoint(c) == 0 && (*s == L'x' || *s == L'X'))
  {
    c = s[1];
    s += 2;
    base = 16;
  }
  if (base == 0)
    base = wctoint(c) == 0 ? 8 : 10;
  cutoff = (unsigned long)ULONG_MAX / (unsigned long)base;
  cutlim = (unsigned long)ULONG_MAX % (unsigned long)base;
  for (acc = 0, any = 0;; c = *s++)
  {
    c = wctoint(c);
    if (c < 0)
      break;
    if (c >= base)
      break;
    if (any < 0 || acc > cutoff || (acc == cutoff && c > cutlim))
      any = -1;
    else {
      any = 1;
      acc *= base;
      acc += c;
    }
  }
  if (any < 0)
  {
    acc = ULONG_MAX;
  }
  if (neg)
    acc = 0-acc;
  if (endptr != 0)
    *endptr = any ? (wchar_t *)((size_t)(s - 1)) : (wchar_t *)((size_t)nptr);
  return acc;
}
