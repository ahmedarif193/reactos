/*
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

/* Local regression for the formatting used by FreeLdr's shared TUI. */
#include <stdio.h>
#include <string.h>

int main(void)
{
    char buffer[128];
    int length;

    length = _snprintf(buffer, sizeof(buffer), "%*s%s%*s", 4, "", "ReactOS", 2, "");
    if (length != 13 || strcmp(buffer, "    ReactOS  ")) return 1;
    length = _snprintf(buffer, sizeof(buffer), "%.*s%d%s", 4, "Time remaining: ", 12, " seconds");
    if (length != 14 || strcmp(buffer, "Time12 seconds")) return 2;
    length = _snprintf(buffer, sizeof(buffer), "%s %d%s %d", "September", 12, "th", 2026);
    if (strcmp(buffer, "September 12th 2026")) return 3;
    length = _snprintf(buffer, sizeof(buffer), "%I64u", 123456789012345ULL);
    if (length != 15 || strcmp(buffer, "123456789012345")) return 4;
    memset(buffer, 'X', sizeof(buffer));
    length = _snprintf(buffer, 5, "%.*s", 4, "ABCDE");
    if (length != 4 || memcmp(buffer, "ABCD\0X", 6)) return 5;
    if (_snprintf(buffer, sizeof(buffer), "%f", 1.25) != -1) return 6;
    return 0;
}
