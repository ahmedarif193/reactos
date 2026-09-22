#include "../win32nt.h"
#include <ntgdibad.h>

START_TEST(NtGdiGetFontFamilyInfo)
{
    FONTFAMILYINFO Info[4];
    LOGFONTW LogFont;
    LONG Count, Returned, Index;

    ZeroMemory(&LogFont, sizeof(LogFont));
    LogFont.lfCharSet = DEFAULT_CHARSET;

    Count = RTL_NUMBER_OF(Info);
    Returned = NtGdiGetFontFamilyInfo(NULL, &LogFont, Info, &Count);
    ok(Returned > 0 && Returned <= (LONG)RTL_NUMBER_OF(Info), "Returned %ld families\n", Returned);
    ok(Count >= Returned, "Available count %ld is below %ld\n", Count, Returned);
    for (Index = 0; Index < Returned; ++Index)
        ok(Info[Index].EnumLogFontEx.elfLogFont.lfFaceName[0] != UNICODE_NULL, "Family %ld has no face name\n", Index);

    Count = RTL_NUMBER_OF(Info);
    ok_long(NtGdiGetFontFamilyInfo(NULL, &LogFont, NULL, &Count), -1);

    Count = 0;
    ok_long(NtGdiGetFontFamilyInfo(NULL, &LogFont, Info, &Count), -1);
}
