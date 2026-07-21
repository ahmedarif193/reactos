/*
 * PROJECT:     ReactOS build tools
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Common utility helpers for rosconfig
 */

#include "rosconfig.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Small utilities                                                    */
/* ------------------------------------------------------------------ */

void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "rosconfig: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(2);
}

void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p)
        die("out of memory");
    return p;
}

void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n ? n : 1);
    if (!p)
        die("out of memory");
    return p;
}

char *xstrdup(const char *s)
{
    char *p = xmalloc(strlen(s) + 1);
    strcpy(p, s);
    return p;
}

char *xstrndup(const char *s, size_t n)
{
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

void sb_addn(SB *sb, const char *s, size_t n)
{
    if (sb->len + n + 1 > sb->cap) {
        sb->cap = sb->cap ? sb->cap : 256;
        while (sb->len + n + 1 > sb->cap)
            sb->cap *= 2;
        sb->buf = xrealloc(sb->buf, sb->cap);
    }
    memcpy(sb->buf + sb->len, s, n);
    sb->len += n;
    sb->buf[sb->len] = '\0';
}

void sb_str(SB *sb, const char *s)
{
    sb_addn(sb, s, strlen(s));
}

void sb_fmt(SB *sb, const char *fmt, ...)
{
    char tmp[1024];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if ((size_t)n >= sizeof(tmp))
        n = (int)sizeof(tmp) - 1;
    sb_addn(sb, tmp, (size_t)n);
}

void sb_free(SB *sb)
{
    free(sb->buf);
    sb->buf = NULL;
    sb->len = sb->cap = 0;
}

void rtrim(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' ||
                     s[n - 1] == '\r' || s[n - 1] == '\n'))
        s[--n] = '\0';
}

const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t')
        p++;
    return p;
}

/* Column of the first non-blank character; tabs advance to multiples of 8. */
int line_indent(const char *line)
{
    int col = 0;
    for (; *line; line++) {
        if (*line == ' ')
            col++;
        else if (*line == '\t')
            col = (col / 8 + 1) * 8;
        else
            break;
    }
    return col;
}

int line_blank(const char *line)
{
    return *skip_ws(line) == '\0';
}

/* Next whitespace-delimited word (malloc'd); advances *pp. NULL at EOL. */
char *tok_word(const char **pp)
{
    const char *p = skip_ws(*pp), *start;
    if (*p == '\0')
        return NULL;
    start = p;
    while (*p && *p != ' ' && *p != '\t')
        p++;
    *pp = p;
    return xstrndup(start, (size_t)(p - start));
}

/* Rest of the line: either a "quoted string" or the trimmed remainder. */
char *tok_rest(const char **pp)
{
    const char *p = skip_ws(*pp);
    char *out;
    if (*p == '"') {
        const char *end = strchr(p + 1, '"');
        if (!end)
            return NULL;
        out = xstrndup(p + 1, (size_t)(end - p - 1));
        *pp = end + 1;
        return out;
    }
    out = xstrdup(p);
    rtrim(out);
    *pp = p + strlen(p);
    return out;
}

/* Whole file into a NUL-terminated malloc'd buffer, or NULL if unreadable. */
char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    long size;
    char *buf;
    size_t got;
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    buf = xmalloc((size_t)size + 1);
    got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[got] = '\0';
    return buf;
}

/* Split a buffer into lines in place (strips \n and \r). */
char **split_lines(char *buf, int *count)
{
    char **lines = NULL;
    int n = 0, cap = 0;
    char *p = buf;
    while (*p) {
        char *nl;
        *GROW(lines, n, cap) = p;
        nl = strchr(p, '\n');
        if (!nl)
            break;
        *nl = '\0';
        if (nl > p && nl[-1] == '\r')
            nl[-1] = '\0';
        p = nl + 1;
    }
    if (n > 0) {
        char *last = lines[n - 1];
        size_t l = strlen(last);
        if (l > 0 && last[l - 1] == '\r')
            last[l - 1] = '\0';
    }
    *count = n;
    return lines;
}
