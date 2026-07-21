/* ReactOS compatibility definitions missing from sdk/include/psdk/winuser.h. */

#ifndef __WOW64WIN_WINUSER_COMPAT_H
#define __WOW64WIN_WINUSER_COMPAT_H

enum SCROLL_HITTEST
{
    SCROLL_NOWHERE,
    SCROLL_TOP_ARROW,
    SCROLL_TOP_RECT,
    SCROLL_THUMB,
    SCROLL_BOTTOM_RECT,
    SCROLL_BOTTOM_ARROW
};

struct SCROLL_TRACKING_INFO
{
    HWND win;
    INT bar;
    INT thumb_pos;
    INT thumb_val;
    BOOL vertical;
    enum SCROLL_HITTEST hit_test;
};

enum NONCLIENT_BUTTON_TYPE
{
    MENU_CLOSE_BUTTON,
    MENU_MIN_BUTTON,
    MENU_MAX_BUTTON,
    MENU_RESTORE_BUTTON,
    MENU_HELP_BUTTON
};

#endif /* __WOW64WIN_WINUSER_COMPAT_H */
