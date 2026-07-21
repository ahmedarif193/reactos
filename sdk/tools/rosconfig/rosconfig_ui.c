/*
 * PROJECT:     ReactOS build tools
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Cross-platform terminal UI for rosconfig
 */

#include "rosconfig.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#include <io.h>
#define ISATTY _isatty
#define INPUT_FD _fileno(stdin)
#define OUTPUT_FD _fileno(stdout)
#else
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>
#define ISATTY isatty
#define INPUT_FD STDIN_FILENO
#define OUTPUT_FD STDOUT_FILENO
#endif

/* ------------------------------------------------------------------ */
/* Terminal platform layer                                            */
/* ------------------------------------------------------------------ */

enum {
    KEY_NONE = 0,
    KEY_UP = 1000,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_HOME,
    KEY_END,
    KEY_PGUP,
    KEY_PGDN,
    KEY_ENTER,
    KEY_ESC,
    KEY_BACKSPACE,
    KEY_DELETE,
    KEY_F1,
    KEY_ABORT
};

static int g_term_active;
static int g_abort_requested;

#ifdef _WIN32

static HANDLE g_hout = INVALID_HANDLE_VALUE;
static HANDLE g_hin = INVALID_HANDLE_VALUE;
static DWORD g_out_mode;
static DWORD g_in_mode;

static int term_setup(void)
{
    DWORD mode, input_mode;
    g_hout = GetStdHandle(STD_OUTPUT_HANDLE);
    g_hin = GetStdHandle(STD_INPUT_HANDLE);
    if (g_hout == INVALID_HANDLE_VALUE || g_hin == INVALID_HANDLE_VALUE ||
        !GetConsoleMode(g_hout, &mode) || !GetConsoleMode(g_hin, &input_mode)) {
        fprintf(stderr, "rosconfig: not running on a console\n");
        return -1;
    }
    g_out_mode = mode;
    g_in_mode = input_mode;
    mode |= ENABLE_PROCESSED_OUTPUT;
#ifdef ENABLE_VIRTUAL_TERMINAL_PROCESSING
    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
#else
    mode |= 0x0004;
#endif
    if (!SetConsoleMode(g_hout, mode)) {
        fprintf(stderr, "rosconfig: this console does not support VT output" " (Windows 10 or newer is required)\n");
        return -1;
    }
    input_mode &= ~ENABLE_PROCESSED_INPUT;
    if (!SetConsoleMode(g_hin, input_mode)) {
        SetConsoleMode(g_hout, g_out_mode);
        fprintf(stderr, "rosconfig: cannot switch the console to raw input mode\n");
        return -1;
    }
    g_term_active = 1;
    fputs("\x1b[?1049h\x1b[?25l\x1b[2J", stdout);
    fflush(stdout);
    return 0;
}

static void term_restore(void)
{
    if (!g_term_active)
        return;
    g_term_active = 0;
    fputs("\x1b[0m\x1b[2J\x1b[H\x1b[?25h\x1b[?1049l", stdout);
    fflush(stdout);
    SetConsoleMode(g_hin, g_in_mode);
    SetConsoleMode(g_hout, g_out_mode);
}

static void term_size(int *rows, int *cols)
{
    CONSOLE_SCREEN_BUFFER_INFO info;
    *rows = 24;
    *cols = 80;
    if (g_hout != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(g_hout, &info)) {
        *cols = info.srWindow.Right - info.srWindow.Left + 1;
        *rows = info.srWindow.Bottom - info.srWindow.Top + 1;
    }
}

static int term_getkey(void)
{
    int c = _getch();
    if (c == 0 || c == 0xE0) {
        int c2 = _getch();
        switch (c2) {
            case 'H': return KEY_UP;
            case 'P': return KEY_DOWN;
            case 'K': return KEY_LEFT;
            case 'M': return KEY_RIGHT;
            case 'G': return KEY_HOME;
            case 'O': return KEY_END;
            case 'I': return KEY_PGUP;
            case 'Q': return KEY_PGDN;
            case 'S': return KEY_DELETE;
            case 59:  return KEY_F1;
            default:  return KEY_NONE;
        }
    }
    if (c == '\r' || c == '\n')
        return KEY_ENTER;
    if (c == 27)
        return KEY_ESC;
    if (c == 8 || c == 127)
        return KEY_BACKSPACE;
    if (c == 3) {
        g_abort_requested = 1;
        return KEY_ABORT;
    }
    return c;
}

#else /* !_WIN32 */

static struct termios g_old_tio;

static void term_restore(void)
{
    if (!g_term_active)
        return;
    g_term_active = 0;
    fputs("\x1b[0m\x1b[2J\x1b[H\x1b[?25h\x1b[?1049l", stdout);
    fflush(stdout);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_old_tio);
}

static void term_on_signal(int sig)
{
    (void)sig;
    term_restore();
    _exit(130);
}

static int term_setup(void)
{
    struct termios raw;
    if (tcgetattr(STDIN_FILENO, &g_old_tio) != 0) {
        fprintf(stderr, "rosconfig: cannot query terminal attributes\n");
        return -1;
    }
    raw = g_old_tio;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO | ISIG | IEXTEN);
    raw.c_iflag &= (tcflag_t)~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
        fprintf(stderr, "rosconfig: cannot switch terminal to raw mode\n");
        return -1;
    }
    g_term_active = 1;
    signal(SIGTERM, term_on_signal);
    signal(SIGHUP, term_on_signal);
    fputs("\x1b[?1049h\x1b[?25l\x1b[2J", stdout);
    fflush(stdout);
    return 0;
}

static void term_size(int *rows, int *cols)
{
    struct winsize ws;
    *rows = 24;
    *cols = 80;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
    }
}

/* Read one byte; timeout_ms < 0 blocks. Returns -1 on timeout/EOF. */
static int read_byte(int timeout_ms)
{
    unsigned char c;
    if (timeout_ms >= 0) {
        fd_set fds;
        struct timeval tv;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) <= 0)
            return -1;
    }
    if (read(STDIN_FILENO, &c, 1) != 1)
        return -1;
    return c;
}

static int term_getkey(void)
{
    int c = read_byte(-1);
    if (c < 0)
        return KEY_ESC;
    if (c == 0x1b) {
        int c2 = read_byte(60);
        int c3;
        if (c2 < 0)
            return KEY_ESC;
        if (c2 != '[' && c2 != 'O')
            return KEY_ESC;
        c3 = read_byte(60);
        if (c3 < 0)
            return KEY_ESC;
        if (c3 >= '0' && c3 <= '9') {
            int num = 0;
            while (c3 >= '0' && c3 <= '9') {
                num = num * 10 + (c3 - '0');
                c3 = read_byte(60);
                if (c3 < 0)
                    return KEY_ESC;
            }
            if (c3 != '~')
                return KEY_NONE;
            switch (num) {
                case 1: case 7: return KEY_HOME;
                case 3:         return KEY_DELETE;
                case 4: case 8: return KEY_END;
                case 5:         return KEY_PGUP;
                case 6:         return KEY_PGDN;
                case 11:        return KEY_F1;
                default:        return KEY_NONE;
            }
        }
        switch (c3) {
            case 'A': return KEY_UP;
            case 'B': return KEY_DOWN;
            case 'C': return KEY_RIGHT;
            case 'D': return KEY_LEFT;
            case 'H': return KEY_HOME;
            case 'F': return KEY_END;
            case 'P': return KEY_F1;
            default:  return KEY_NONE;
        }
    }
    if (c == '\r' || c == '\n')
        return KEY_ENTER;
    if (c == 127 || c == 8)
        return KEY_BACKSPACE;
    if (c == 3) {
        g_abort_requested = 1;
        return KEY_ABORT;
    }
    return c;
}

#endif /* _WIN32 */

/* ------------------------------------------------------------------ */
/* TUI                                                                */
/* ------------------------------------------------------------------ */

#define A_NORM   ""
#define A_SEL    "\x1b[7m"
#define A_HDR    "\x1b[1;36m"
#define A_TITLE  "\x1b[1m"
#define A_DIM    "\x1b[90m"

typedef struct {
    EntryType type;
    int index;
} Item;

static Item *g_items;
static int g_nitems, g_citems;
static int g_menu = -1;
static int g_sel, g_off;
static int g_modified;
static int g_show_hidden;
static const char *g_cache_path;
static char g_flash[128];

typedef struct {
    int menu;
    int sel;
    int off;
} NavFrame;

static NavFrame *g_nav;
static int g_nnav, g_cnav;

static int menu_option_count(int menu, int visible_only)
{
    int i, count = 0;
    for (i = 0; i < g_nopts; i++) {
        int parent;
        if (visible_only && !opt_visible(&g_opts[i]))
            continue;
        parent = g_opts[i].menu;
        while (parent >= 0 && parent != menu)
            parent = g_menus[parent].parent;
        if (parent == menu)
            count++;
    }
    return count;
}

static int menu_visible(int menu)
{
    return menu_option_count(menu, 1) > 0;
}

static void build_items(void)
{
    int i;
    g_nitems = 0;
    for (i = 0; i < g_nentries; i++) {
        const DefEntry *entry = &g_entries[i];
        if (entry->type == ENTRY_MENU) {
            int menu = entry->index;
            if (g_menus[menu].parent == g_menu && (menu_visible(menu) || (g_show_hidden && menu_option_count(menu, 0) > 0))) {
                Item *item = GROW(g_items, g_nitems, g_citems);
                item->type = ENTRY_MENU;
                item->index = menu;
            }
        } else {
            int opt = entry->index;
            if (g_opts[opt].menu == g_menu && (opt_visible(&g_opts[opt]) || g_show_hidden)) {
                Item *item = GROW(g_items, g_nitems, g_citems);
                item->type = ENTRY_OPTION;
                item->index = opt;
            }
        }
    }
}

static int is_option_item(int idx)
{
    return idx >= 0 && idx < g_nitems && g_items[idx].type == ENTRY_OPTION;
}

static int is_menu_item(int idx)
{
    return idx >= 0 && idx < g_nitems && g_items[idx].type == ENTRY_MENU;
}

static int item_available(const Item *item)
{
    if (item->type == ENTRY_MENU)
        return menu_visible(item->index);
    return opt_visible(&g_opts[item->index]);
}

static void clamp_sel(int dir)
{
    (void)dir;
    if (g_nitems == 0) {
        g_sel = 0;
        return;
    }
    if (g_sel >= g_nitems)
        g_sel = g_nitems - 1;
    if (g_sel < 0)
        g_sel = 0;
}

static void move_sel(int delta)
{
    if (g_nitems == 0)
        return;
    g_sel += delta;
    if (g_sel < 0)
        g_sel = 0;
    if (g_sel >= g_nitems)
        g_sel = g_nitems - 1;
}

static void select_opt(const Option *o)
{
    int i;
    for (i = 0; i < g_nitems; i++) {
        if (g_items[i].type == ENTRY_OPTION && &g_opts[g_items[i].index] == o) {
            g_sel = i;
            return;
        }
    }
    clamp_sel(+1);
}

static void enter_menu(int menu)
{
    NavFrame *frame = GROW(g_nav, g_nnav, g_cnav);
    frame->menu = g_menu;
    frame->sel = g_sel;
    frame->off = g_off;
    g_menu = menu;
    g_sel = 0;
    g_off = 0;
    build_items();
    clamp_sel(+1);
}

static int leave_menu(void)
{
    NavFrame *frame;
    if (g_nnav == 0)
        return 0;
    frame = &g_nav[--g_nnav];
    g_menu = frame->menu;
    g_sel = frame->sel;
    g_off = frame->off;
    build_items();
    clamp_sel(+1);
    return 1;
}

/* One padded/truncated line at an absolute row. */
static void draw_line(SB *sb, int row, int cols, const char *attr,
                      const char *text, int pad_full)
{
    int len = (int)strlen(text);
    sb_fmt(sb, "\x1b[%d;1H", row);
    sb_str(sb, attr);
    if (len > cols)
        sb_addn(sb, text, (size_t)cols);
    else {
        sb_str(sb, text);
        if (pad_full) {
            int i;
            for (i = len; i < cols; i++)
                sb_addn(sb, " ", 1);
        }
    }
    sb_str(sb, "\x1b[0m\x1b[K");
}

static void item_text(const Item *it, char *buf, size_t bufsz)
{
    if (it->type == ENTRY_MENU) {
        snprintf(buf, bufsz, "      %s  --->", g_menus[it->index].title);
    } else {
        const Option *o = &g_opts[it->index];
        if (o->type == OPT_BOOL) {
            const char *ind = "[ ]";
            if (strcmp(o->value, "y") == 0)
                ind = "[*]";
            else if (strcmp(o->value, "auto") == 0)
                ind = "[A]";
            snprintf(buf, bufsz, "  %s %s", ind, o->prompt);
        } else if (o->type == OPT_CHOICE) {
            snprintf(buf, bufsz, "  (%s) %s  --->", o->value, o->prompt);
        } else {
            snprintf(buf, bufsz, "  (%s) %s", o->value, o->prompt);
        }
    }
    if (!item_available(it)) {
        size_t used = strlen(buf);
        if (used < bufsz)
            snprintf(buf + used, bufsz - used, "  [hidden]");
    }
}

static int list_height(int rows)
{
    int h = rows - 8;
    return h < 1 ? 1 : h;
}

static void current_path(char *buf, size_t bufsz)
{
    int chain[64];
    int menu = g_menu, depth = 0, i;
    size_t used;

    snprintf(buf, bufsz, " Location: Main Menu");
    used = strlen(buf);
    while (menu >= 0 && depth < (int)(sizeof(chain) / sizeof(chain[0]))) {
        chain[depth++] = menu;
        menu = g_menus[menu].parent;
    }
    for (i = depth - 1; i >= 0 && used < bufsz; i--) {
        int n = snprintf(buf + used, bufsz - used, " > %s", g_menus[chain[i]].title);
        if (n < 0 || (size_t)n >= bufsz - used)
            break;
        used += (size_t)n;
    }
}

static void first_help_line(const Option *o, char *buf, size_t bufsz)
{
    const char *text = o->help;
    const char *end;
    size_t len;

    if (!text || !text[0]) {
        snprintf(buf, bufsz, "No help is available for this option.");
        return;
    }
    while (*text == '\n' || *text == '\r')
        text++;
    end = strpbrk(text, "\r\n");
    len = end ? (size_t)(end - text) : strlen(text);
    if (len >= bufsz)
        len = bufsz - 1;
    memcpy(buf, text, len);
    buf[len] = '\0';
}

static void draw_main(SB *sb, int rows, int cols)
{
    int lh = list_height(rows);
    int i;
    char buf[512];
    char sep[512];
    int seplen = cols < (int)sizeof(sep) - 1 ? cols : (int)sizeof(sep) - 1;

    for (i = 0; i < seplen; i++)
        sep[i] = '-';
    sep[seplen] = '\0';

    if (g_sel < g_off)
        g_off = g_sel;
    if (g_sel >= g_off + lh)
        g_off = g_sel - lh + 1;
    if (g_off > g_nitems - lh)
        g_off = g_nitems - lh;
    if (g_off < 0)
        g_off = 0;

    draw_line(sb, 1, cols, A_TITLE, " ReactOS Build Configuration  -  rosconfig v" ROSCONFIG_VERSION, 0);
    current_path(buf, sizeof(buf));
    draw_line(sb, 2, cols, A_HDR, buf, 0);
    draw_line(sb, 3, cols, A_DIM, " Arrows move | Enter select | Esc back | / search | ? help | S save | Q quit", 0);
    draw_line(sb, 4, cols, A_NORM, sep, 0);

    for (i = 0; i < lh; i++) {
        int idx = g_off + i;
        int row = 5 + i;
        if (idx >= g_nitems) {
            draw_line(sb, row, cols, A_NORM, "", 0);
            continue;
        }
        item_text(&g_items[idx], buf, sizeof(buf));
        if (idx == g_sel)
            draw_line(sb, row, cols, A_SEL, buf, 1);
        else if (!item_available(&g_items[idx]))
            draw_line(sb, row, cols, A_DIM, buf, 0);
        else if (g_items[idx].type == ENTRY_MENU)
            draw_line(sb, row, cols, A_HDR, buf, 0);
        else
            draw_line(sb, row, cols, A_NORM, buf, 0);
    }

    draw_line(sb, rows - 3, cols, A_NORM, sep, 0);
    if (is_option_item(g_sel)) {
        const Option *o = &g_opts[g_items[g_sel].index];
        char help[512];
        if (opt_visible(o)) {
            first_help_line(o, help, sizeof(help));
            snprintf(buf, sizeof(buf), " Help: %.504s", help);
        } else {
            snprintf(buf, sizeof(buf), " Hidden: one or more dependencies are not satisfied; press ? for details.");
        }
        draw_line(sb, rows - 2, cols, A_NORM, buf, 0);
        snprintf(buf, sizeof(buf), " Symbol: %s   Value: %s   Default: %s", o->key, o->value, o->def);
        draw_line(sb, rows - 1, cols, A_DIM, buf, 0);
    } else if (is_menu_item(g_sel)) {
        int menu = g_items[g_sel].index;
        int visible = menu_option_count(menu, 1);
        int total = menu_option_count(menu, 0);
        snprintf(buf, sizeof(buf), " Help: Open the %s submenu.", g_menus[menu].title);
        draw_line(sb, rows - 2, cols, A_NORM, buf, 0);
        snprintf(buf, sizeof(buf), " Submenu: %d visible / %d total option%s", visible, total, total == 1 ? "" : "s");
        draw_line(sb, rows - 1, cols, A_DIM, buf, 0);
    } else {
        draw_line(sb, rows - 2, cols, A_NORM, " This menu has no visible options.", 0);
        draw_line(sb, rows - 1, cols, A_DIM, "", 0);
    }
    {
        const char *state = g_modified ? "[modified]" : "[saved]";
        if (g_flash[0])
            state = g_flash;
        snprintf(buf, sizeof(buf), " %s", state);
        draw_line(sb, rows, cols, A_DIM, buf, 0);
    }
}

static void flush_frame(SB *sb)
{
    fwrite(sb->buf, 1, sb->len, stdout);
    fflush(stdout);
    sb_free(sb);
}

static void redraw(void)
{
    SB sb = {0};
    int rows, cols;
    term_size(&rows, &cols);
    draw_main(&sb, rows, cols);
    flush_frame(&sb);
}

static void mark_modified(void)
{
    g_modified = 1;
    g_flash[0] = '\0';
}

static void popup_choice(Option *o)
{
    int sel = choice_index(o, o->value);
    int off = 0;
    if (sel < 0)
        sel = 0;
    for (;;) {
        SB sb = {0};
        int rows, cols, w, h, page, top, left, i, key;
        char buf[512];

        term_size(&rows, &cols);
        draw_main(&sb, rows, cols);

        w = (int)strlen(o->prompt) + 8;
        for (i = 0; i < o->nvalues; i++) {
            const ChoiceValue *cv = &o->values[i];
            int l = (int)strlen(cv->value) + 10 +
                    (cv->label ? (int)strlen(cv->label) + 2 : 0);
            if (l > w)
                w = l;
        }
        if (w > cols - 4)
            w = cols - 4;
        if (w < 20)
            w = 20;
        h = o->nvalues + 2;
        if (h > rows - 2)
            h = rows - 2;
        page = h - 2;
        if (page < 1)
            page = 1;
        if (sel < off)
            off = sel;
        if (sel >= off + page)
            off = sel - page + 1;
        if (off > o->nvalues - page)
            off = o->nvalues - page;
        if (off < 0)
            off = 0;
        top = (rows - h) / 2 + 1;
        left = (cols - w) / 2 + 1;
        if (top < 1)
            top = 1;
        if (left < 1)
            left = 1;

        snprintf(buf, sizeof(buf), "+- %s ", o->prompt);
        {
            int l = (int)strlen(buf);
            while (l < w - 1 && l < (int)sizeof(buf) - 2)
                buf[l++] = '-';
            buf[l] = '+';
            buf[l + 1] = '\0';
            if (l + 1 > w)
                buf[w] = '\0';
        }
        sb_fmt(&sb, "\x1b[%d;%dH", top, left);
        sb_str(&sb, buf);

        for (i = 0; i < page; i++) {
            int index = off + i;
            const ChoiceValue *cv = &o->values[index];
            char row[512];
            int l;
            snprintf(row, sizeof(row), " (%c) %-10s %s", (choice_index(o, o->value) == index) ? '*' : ' ', cv->value, cv->label ? cv->label : "");
            rtrim(row);
            l = (int)strlen(row);
            if (l > w - 2) {
                row[w - 2] = '\0';
                l = w - 2;
            }
            while (l < w - 2)
                row[l++] = ' ', row[l] = '\0';
            sb_fmt(&sb, "\x1b[%d;%dH|", top + 1 + i, left);
            if (index == sel)
                sb_str(&sb, A_SEL);
            sb_str(&sb, row);
            sb_str(&sb, "\x1b[0m|");
        }

        for (i = 0; i < w && i < (int)sizeof(buf) - 1; i++)
            buf[i] = (i == 0 || i == w - 1) ? '+' : '-';
        buf[i] = '\0';
        sb_fmt(&sb, "\x1b[%d;%dH", top + h - 1, left);
        sb_str(&sb, buf);

        flush_frame(&sb);

        key = term_getkey();
        if (key == KEY_ABORT)
            return;
        switch (key) {
            case KEY_UP:
            case 'k':
                sel = (sel > 0) ? sel - 1 : o->nvalues - 1;
                break;
            case KEY_DOWN:
            case 'j':
                sel = (sel + 1) % o->nvalues;
                break;
            case KEY_HOME:
                sel = 0;
                break;
            case KEY_END:
                sel = o->nvalues - 1;
                break;
            case KEY_PGUP:
                sel -= page;
                if (sel < 0)
                    sel = 0;
                break;
            case KEY_PGDN:
                sel += page;
                if (sel >= o->nvalues)
                    sel = o->nvalues - 1;
                break;
            case KEY_ENTER:
            case ' ':
                if (strcmp(o->value, o->values[sel].value) != 0) {
                    set_value(o, o->values[sel].value);
                    mark_modified();
                    build_items();
                    select_opt(o);
                }
                return;
            case KEY_ESC:
            case 'q':
                return;
            default:
                break;
        }
    }
}

static void edit_string(Option *o)
{
    char buf[256];
    size_t len;
    snprintf(buf, sizeof(buf), "%s", o->value);
    len = strlen(buf);
    for (;;) {
        SB sb = {0};
        int rows, cols, key;
        char line[600];

        term_size(&rows, &cols);
        draw_main(&sb, rows, cols);
        snprintf(line, sizeof(line), " %s = %s_   (Enter accept, ESC cancel)", o->key, buf);
        draw_line(&sb, rows, cols, A_SEL, line, 1);
        flush_frame(&sb);

        key = term_getkey();
        if (key == KEY_ABORT)
            return;
        if (key == KEY_ENTER) {
            if (strcmp(o->value, buf) != 0) {
                set_value(o, buf);
                mark_modified();
                build_items();
                select_opt(o);
            }
            return;
        }
        if (key == KEY_ESC)
            return;
        if (key == KEY_BACKSPACE) {
            if (len > 0)
                buf[--len] = '\0';
        } else if (key >= 32 && key < 127 && len < sizeof(buf) - 1) {
            buf[len++] = (char)key;
            buf[len] = '\0';
        }
    }
}

static void wrap_text(const char *text, int width, SB *out)
{
    const char *p = text;
    if (width < 16)
        width = 16;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t linelen = nl ? (size_t)(nl - p) : strlen(p);
        while ((int)linelen > width) {
            int cut = width;
            while (cut > 0 && p[cut] != ' ')
                cut--;
            if (cut == 0)
                cut = width;
            sb_addn(out, p, (size_t)cut);
            sb_str(out, "\n");
            p += cut;
            while (*p == ' ')
                p++;
            linelen = nl ? (size_t)(nl - p) : strlen(p);
        }
        sb_addn(out, p, linelen);
        sb_str(out, "\n");
        p += linelen;
        if (*p == '\n')
            p++;
    }
}

static void show_help_page(const char *title, const char *text)
{
    int off = 0;

    for (;;) {
        SB sb = {0};
        SB wrapped = {0};
        int rows, cols, page, max_off, row, i, key;
        char buf[512], sep[512];
        char **lines;
        int nlines;
        char *body;
        int seplen;

        term_size(&rows, &cols);
        page = rows - 5;
        if (page < 1)
            page = 1;
        wrap_text(text, cols - 4, &wrapped);
        body = xstrdup(wrapped.buf ? wrapped.buf : "");
        lines = split_lines(body, &nlines);
        max_off = nlines > page ? nlines - page : 0;
        if (off > max_off)
            off = max_off;
        if (off < 0)
            off = 0;

        seplen = cols < (int)sizeof(sep) - 1 ? cols : (int)sizeof(sep) - 1;
        for (i = 0; i < seplen; i++)
            sep[i] = '-';
        sep[seplen] = '\0';

        sb_str(&sb, "\x1b[2J");
        draw_line(&sb, 1, cols, A_TITLE, title, 0);
        draw_line(&sb, 2, cols, A_DIM, " Up/Down scroll | PgUp/PgDn page | Home/End | Esc/Enter return", 0);
        draw_line(&sb, 3, cols, A_NORM, sep, 0);
        row = 4;
        for (i = 0; i < page; i++, row++) {
            int line = off + i;
            if (line < nlines)
                snprintf(buf, sizeof(buf), "  %s", lines[line]);
            else
                buf[0] = '\0';
            draw_line(&sb, row, cols, A_NORM, buf, 0);
        }
        draw_line(&sb, rows - 1, cols, A_NORM, sep, 0);
        if (nlines > 0)
            snprintf(buf, sizeof(buf), " Lines %d-%d of %d", off + 1, off + (page < nlines - off ? page : nlines - off), nlines);
        else
            snprintf(buf, sizeof(buf), " No help text.");
        draw_line(&sb, rows, cols, A_DIM, buf, 0);
        flush_frame(&sb);

        key = term_getkey();
        free(lines);
        free(body);
        sb_free(&wrapped);
        if (key == KEY_ABORT)
            return;
        switch (key) {
            case KEY_UP:   case 'k': if (off > 0) off--; break;
            case KEY_DOWN: case 'j': if (off < max_off) off++; break;
            case KEY_PGUP: off -= page; if (off < 0) off = 0; break;
            case KEY_PGDN: off += page; if (off > max_off) off = max_off; break;
            case KEY_HOME: off = 0; break;
            case KEY_END:  off = max_off; break;
            case KEY_ENTER:
            case KEY_ESC:
            case 'q': case 'Q':
                fputs("\x1b[2J", stdout);
                fflush(stdout);
                return;
            default:
                break;
        }
    }
}

static const char *option_type_name(const Option *o)
{
    if (o->type == OPT_BOOL)
        return o->tristate ? "Boolean with automatic default" : "Boolean";
    if (o->type == OPT_CHOICE)
        return "Choice";
    return "String";
}

static int dependency_met(const Dep *dep)
{
    const char *value = config_value(dep->key);
    int equal = value && strcmp(value, dep->value) == 0;
    return dep->negate ? !equal : equal;
}

static void append_dependency_list(SB *body, const Dep *deps, int ndeps, int *count)
{
    int i;
    for (i = 0; i < ndeps; i++) {
        const Dep *dep = &deps[i];
        const char *actual = config_value(dep->key);
        sb_fmt(body, "  [%s] %s%s%s (current: %s)\n", dependency_met(dep) ? "met" : "unmet", dep->key, dep->negate ? "!=" : "=", dep->value, actual ? actual : "unset");
        (*count)++;
    }
}

static void append_menu_dependencies(SB *body, int menu, int *count)
{
    if (menu < 0)
        return;
    append_menu_dependencies(body, g_menus[menu].parent, count);
    append_dependency_list(body, g_menus[menu].deps, g_menus[menu].ndeps, count);
}

static void show_option_help(const Option *o)
{
    SB body = {0};
    char title[512];
    int i, dependencies = 0;

    sb_fmt(&body, "Symbol: %s\n", o->key);
    sb_fmt(&body, "Type: %s\n", option_type_name(o));
    sb_fmt(&body, "Current value: %s\n", o->value);
    sb_fmt(&body, "Default value: %s\n", o->def);
    sb_fmt(&body, "Visibility: %s\n", opt_visible(o) ? "available" : "hidden by dependencies");
    sb_str(&body, "\nDependencies\n------------\n");
    append_menu_dependencies(&body, o->menu, &dependencies);
    append_dependency_list(&body, o->deps, o->ndeps, &dependencies);
    if (dependencies == 0)
        sb_str(&body, "  None.\n");
    sb_str(&body, "\nDescription\n-----------\n");
    if (o->help && o->help[0])
        sb_str(&body, o->help);
    else
        sb_str(&body, "No help is available for this option.");
    if (o->type == OPT_BOOL && o->tristate)
        sb_str(&body, "\n\nThe automatic value leaves this option unset so sdk/cmake/config.cmake can choose its conditional default.");
    if (o->type == OPT_CHOICE) {
        sb_str(&body, "\n\nAvailable values\n----------------\n");
        for (i = 0; i < o->nvalues; i++) {
            const ChoiceValue *value = &o->values[i];
            sb_fmt(&body, "  %s%s%s\n", value->value, value->label ? " - " : "", value->label ? value->label : "");
        }
    }
    snprintf(title, sizeof(title), " Help: %s  (%s)", o->prompt, o->key);
    show_help_page(title, body.buf ? body.buf : "");
    sb_free(&body);
}

static void show_menu_help(int menu)
{
    SB body = {0};
    char title[512];
    int visible = menu_option_count(menu, 1);
    int total = menu_option_count(menu, 0);
    int dependencies = 0;

    sb_fmt(&body, "The %s submenu contains %d visible / %d total option%s.\n", g_menus[menu].title, visible, total, total == 1 ? "" : "s");
    sb_str(&body, "\nDependencies\n------------\n");
    append_menu_dependencies(&body, menu, &dependencies);
    if (dependencies == 0)
        sb_str(&body, "  None.\n");
    sb_str(&body, "\n");
    sb_str(&body, "Press Enter or Right to open it. Press Esc, Left, or Backspace to return to its parent menu. Visibility follows the dependencies of the options inside the submenu.");
    snprintf(title, sizeof(title), " Submenu: %s", g_menus[menu].title);
    show_help_page(title, body.buf ? body.buf : "");
    sb_free(&body);
}

static void show_global_help(void)
{
    static const char text[] =
        "Navigation\n"
        "----------\n"
        "Up/Down or J/K moves the selection. Enter or Right opens a submenu or edits the selected option. Esc, Left, or Backspace returns to the parent menu. Q quits from any menu.\n\n"
        "Changing values\n"
        "---------------\n"
        "Space or Enter cycles a Boolean or opens a choice/string editor. Y and N set Boolean values directly. A selects the automatic default when supported. D restores one default and R resets all defaults after confirmation.\n\n"
        "Help and files\n"
        "--------------\n"
        "Question mark shows detailed help for the selected option or submenu. Slash searches symbols and prompts, including hidden options. V toggles hidden entries. F1 opens this key reference. S saves, L reloads the saved configuration, and R resets all options. Unsaved changes are confirmed before normal quitting. Configure-integrated runs also ask whether CMake should start when the menu is clean; Ctrl+C cancels immediately.\n\n"
        "Indicators\n"
        "----------\n"
        "[*] enabled, [ ] disabled, [A] automatic default, (--->) editable choice or submenu, [hidden] unavailable until its dependencies are met.";
    show_help_page(" rosconfig navigation help", text);
}

static void show_selected_help(void)
{
    if (is_option_item(g_sel))
        show_option_help(&g_opts[g_items[g_sel].index]);
    else if (is_menu_item(g_sel))
        show_menu_help(g_items[g_sel].index);
    else
        show_global_help();
}

static int prompt_text(const char *prompt, char *value, size_t value_size)
{
    size_t len = strlen(value);
    for (;;) {
        SB sb = {0};
        int rows, cols, key;
        char line[600];

        term_size(&rows, &cols);
        draw_main(&sb, rows, cols);
        snprintf(line, sizeof(line), " %s%s_   (Enter accept, ESC cancel)", prompt, value);
        draw_line(&sb, rows, cols, A_SEL, line, 1);
        flush_frame(&sb);

        key = term_getkey();
        if (key == KEY_ABORT)
            return -1;
        if (key == KEY_ENTER)
            return 1;
        if (key == KEY_ESC)
            return 0;
        if (key == KEY_BACKSPACE) {
            if (len > 0)
                value[--len] = '\0';
        } else if (key == 21) {     /* Ctrl-U */
            len = 0;
            value[0] = '\0';
        } else if (key >= 32 && key < 127 && len < value_size - 1) {
            value[len++] = (char)key;
            value[len] = '\0';
        }
    }
}

static int contains_nocase(const char *text, const char *query)
{
    const char *start;
    if (!query[0])
        return 1;
    for (start = text; *start; start++) {
        const char *a = start;
        const char *b = query;
        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
            a++;
            b++;
        }
        if (!*b)
            return 1;
    }
    return 0;
}

static int option_matches(const Option *o, const char *query)
{
    int i;
    if (contains_nocase(o->key, query) || contains_nocase(o->prompt, query) || (o->help && contains_nocase(o->help, query)))
        return 1;
    for (i = 0; i < o->nvalues; i++) {
        if (contains_nocase(o->values[i].value, query) || (o->values[i].label && contains_nocase(o->values[i].label, query)))
            return 1;
    }
    return 0;
}

static int jump_to_option(Option *option)
{
    int chain[64];
    int menu = option->menu, depth = 0, i, j;

    while (menu >= 0 && depth < (int)(sizeof(chain) / sizeof(chain[0]))) {
        chain[depth++] = menu;
        menu = g_menus[menu].parent;
    }
    g_nnav = 0;
    g_menu = -1;
    g_sel = 0;
    g_off = 0;
    build_items();
    for (i = depth - 1; i >= 0; i--) {
        for (j = 0; j < g_nitems; j++)
            if (g_items[j].type == ENTRY_MENU && g_items[j].index == chain[i])
                break;
        if (j == g_nitems)
            return 0;
        g_sel = j;
        enter_menu(chain[i]);
    }
    select_opt(option);
    return is_option_item(g_sel) && &g_opts[g_items[g_sel].index] == option;
}

static void search_options(void)
{
    char query[128] = "";

    for (;;) {
        int *results;
        int nresults = 0;
        int selected = 0, off = 0;
        int i;
        int accepted = prompt_text("Search: ", query, sizeof(query));
        if (accepted <= 0 || !query[0])
            return;

        results = xmalloc((size_t)g_nopts * sizeof(*results));
        for (i = 0; i < g_nopts; i++)
            if (option_matches(&g_opts[i], query))
                results[nresults++] = i;
        if (nresults == 0) {
            SB text = {0};
            sb_fmt(&text, "No configuration symbols, prompts, values, or help text matched \"%s\".", query);
            show_help_page(" Search results", text.buf ? text.buf : "No matches.");
            sb_free(&text);
            free(results);
            return;
        }

        for (;;) {
            SB sb = {0};
            int rows, cols, page, key, row;
            char buf[512], sep[512];
            int seplen;

            term_size(&rows, &cols);
            page = rows - 5;
            if (page < 1)
                page = 1;
            if (selected < off)
                off = selected;
            if (selected >= off + page)
                off = selected - page + 1;
            if (off > nresults - page)
                off = nresults - page;
            if (off < 0)
                off = 0;
            seplen = cols < (int)sizeof(sep) - 1 ? cols : (int)sizeof(sep) - 1;
            for (i = 0; i < seplen; i++)
                sep[i] = '-';
            sep[seplen] = '\0';

            sb_str(&sb, "\x1b[2J");
            snprintf(buf, sizeof(buf), " Search: %s", query);
            draw_line(&sb, 1, cols, A_TITLE, buf, 0);
            draw_line(&sb, 2, cols, A_DIM, " Up/Down move | Enter jump | ? details | / new search | Esc return", 0);
            draw_line(&sb, 3, cols, A_NORM, sep, 0);
            row = 4;
            for (i = 0; i < page; i++, row++) {
                int result = off + i;
                if (result < nresults) {
                    Option *o = &g_opts[results[result]];
                    snprintf(buf, sizeof(buf), "  %s = %s  -  %s%s", o->key, o->value, o->prompt, opt_visible(o) ? "" : "  [hidden]");
                    draw_line(&sb, row, cols, result == selected ? A_SEL : (opt_visible(o) ? A_NORM : A_DIM), buf, result == selected);
                } else {
                    draw_line(&sb, row, cols, A_NORM, "", 0);
                }
            }
            draw_line(&sb, rows - 1, cols, A_NORM, sep, 0);
            if (opt_visible(&g_opts[results[selected]]))
                snprintf(buf, sizeof(buf), " Result %d of %d - Enter jumps to this option", selected + 1, nresults);
            else
                snprintf(buf, sizeof(buf), " Result %d of %d - hidden by unmet dependencies; ? shows details", selected + 1, nresults);
            draw_line(&sb, rows, cols, A_DIM, buf, 0);
            flush_frame(&sb);

            key = term_getkey();
            if (key == KEY_ABORT) {
                free(results);
                return;
            }
            switch (key) {
                case KEY_UP: case 'k':
                    if (selected > 0) selected--;
                    break;
                case KEY_DOWN: case 'j':
                    if (selected + 1 < nresults) selected++;
                    break;
                case KEY_PGUP:
                    selected -= page;
                    if (selected < 0) selected = 0;
                    break;
                case KEY_PGDN:
                    selected += page;
                    if (selected >= nresults) selected = nresults - 1;
                    break;
                case KEY_HOME: selected = 0; break;
                case KEY_END: selected = nresults - 1; break;
                case '?': case 'h': case 'H':
                    show_option_help(&g_opts[results[selected]]);
                    break;
                case KEY_ENTER: case KEY_RIGHT:
                    if (opt_visible(&g_opts[results[selected]]) && jump_to_option(&g_opts[results[selected]])) {
                        free(results);
                        fputs("\x1b[2J", stdout);
                        fflush(stdout);
                        return;
                    }
                    break;
                case '/':
                    free(results);
                    goto new_search;
                case KEY_ESC: case KEY_LEFT: case 'q': case 'Q':
                    free(results);
                    fputs("\x1b[2J", stdout);
                    fflush(stdout);
                    return;
                default:
                    break;
            }
            if (g_abort_requested) {
                free(results);
                return;
            }
        }
new_search:
        ;
    }
}

static void do_save(void)
{
    if (cache_save(g_cache_path) == 0) {
        g_modified = 0;
        snprintf(g_flash, sizeof(g_flash), "[saved to %s]", g_cache_path);
    } else {
        snprintf(g_flash, sizeof(g_flash), "[SAVE FAILED: %s]", g_cache_path);
    }
}

static int confirm_action(const char *message)
{
    for (;;) {
        SB sb = {0};
        int rows, cols, key;
        term_size(&rows, &cols);
        draw_main(&sb, rows, cols);
        draw_line(&sb, rows, cols, A_SEL, message, 1);
        flush_frame(&sb);
        key = term_getkey();
        if (key == KEY_ABORT)
            return -1;
        switch (key) {
            case 'y': case 'Y': case KEY_ENTER:
                return 1;
            case 'n': case 'N': case 'c': case 'C': case KEY_ESC:
                return 0;
            default:
                break;
        }
    }
}

static void reset_all_defaults(void)
{
    int i, changed = 0;
    for (i = 0; i < g_nopts; i++)
        if (strcmp(g_opts[i].value, g_opts[i].def) != 0)
            changed = 1;
    if (!changed) {
        snprintf(g_flash, sizeof(g_flash), "[all options already use their defaults]");
        return;
    }
    if (confirm_action(" Reset every option to its default? (Y)es / (N)o ") != 1)
        return;
    for (i = 0; i < g_nopts; i++)
        set_value(&g_opts[i], g_opts[i].def);
    mark_modified();
    build_items();
    clamp_sel(+1);
    snprintf(g_flash, sizeof(g_flash), "[all options reset to defaults]");
}

static void load_saved_state(void)
{
    cache_reload(g_cache_path);
    g_modified = 0;
    g_nnav = 0;
    g_menu = -1;
    g_sel = 0;
    g_off = 0;
    build_items();
    clamp_sel(+1);
}

static void reload_saved(void)
{
    if (g_modified && confirm_action(" Discard changes and reload the saved configuration? (Y)es / (N)o ") != 1)
        return;
    load_saved_state();
    snprintf(g_flash, sizeof(g_flash), "[reloaded from %s]", g_cache_path);
}

static void toggle_hidden(void)
{
    g_show_hidden = !g_show_hidden;
    build_items();
    clamp_sel(+1);
    snprintf(g_flash, sizeof(g_flash), "[hidden options %s]", g_show_hidden ? "shown" : "hidden");
}

/*
 * save_choice/configure_choice are -1 for cancel, 0 for no, and 1 for yes.
 * A negative result means stay in the menu.
 */
int rosconfig_quit_status(int had_changes, int save_choice,
                          int save_succeeded, int ask_configure,
                          int configure_choice)
{
    if (had_changes && (save_choice < 0 || (save_choice > 0 && !save_succeeded)))
        return -1;
    if (!ask_configure)
        return 0;
    if (configure_choice < 0)
        return -1;
    return configure_choice ? 0 : ROSCONFIG_EXIT_SKIP_CONFIGURE;
}

static int prompt_configure(const char *message)
{
    for (;;) {
        SB sb = {0};
        int rows, cols, key;
        term_size(&rows, &cols);
        draw_main(&sb, rows, cols);
        draw_line(&sb, rows, cols, A_SEL, message, 1);
        flush_frame(&sb);
        key = term_getkey();
        if (key == KEY_ABORT)
            return -2;
        switch (key) {
            case 'y': case 'Y': case KEY_ENTER:
                return 1;
            case 'n': case 'N':
                return 0;
            case 'c': case 'C': case KEY_ESC:
                return -1;
            default:
                break;
        }
    }
}

/* Returns 1 when the main loop should exit. */
static int try_quit(int ask_configure, int *exit_status)
{
    int had_changes = g_modified;
    int save_choice = 0;
    int save_succeeded = 1;
    int configure_choice = 1;
    const char *configure_prompt =
        " No unsaved changes. Continue to configure? (Y)es / (N)o / (C)ancel ";

    if (g_modified) {
        for (;;) {
            SB sb = {0};
            int rows, cols, key;
            term_size(&rows, &cols);
            draw_main(&sb, rows, cols);
            draw_line(&sb, rows, cols, A_SEL, " Save configuration? (Y)es / (N)o, discard / (C)ancel ", 1);
            flush_frame(&sb);
            key = term_getkey();
            if (key == KEY_ABORT)
                return 0;
            switch (key) {
                case 'y': case 'Y': case KEY_ENTER:
                    save_choice = 1;
                    do_save();
                    save_succeeded = !g_modified;
                    if (!save_succeeded)
                        return 0;
                    configure_prompt =
                        " Configuration saved. Continue to configure? (Y)es / (N)o / (C)ancel ";
                    break;
                case 'n': case 'N':
                    save_choice = 0;
                    load_saved_state();
                    snprintf(g_flash, sizeof(g_flash), "[changes discarded]");
                    configure_prompt =
                        " Changes discarded. Configure saved values? (Y)es / (N)o / (C)ancel ";
                    break;
                case 'c': case 'C': case KEY_ESC:
                    save_choice = -1;
                    break;
                default:
                    continue;
            }
            break;
        }
    }

    if (ask_configure && save_choice >= 0 && save_succeeded) {
        configure_choice = prompt_configure(configure_prompt);
        if (configure_choice == -2)
            return 0;
    }

    *exit_status = rosconfig_quit_status(had_changes, save_choice,
                                         save_succeeded, ask_configure,
                                         configure_choice);
    return *exit_status >= 0;
}

static void cycle_bool(Option *o, int direct)
{
    const char *next;
    if (direct) {
        return;     /* value already set by caller */
    }
    if (strcmp(o->value, "y") == 0)
        next = "n";
    else if (strcmp(o->value, "n") == 0)
        next = o->tristate ? "auto" : "y";
    else
        next = "y";
    set_value(o, next);
}

static void activate(void)
{
    Option *o;
    if (is_menu_item(g_sel)) {
        enter_menu(g_items[g_sel].index);
        return;
    }
    if (!is_option_item(g_sel))
        return;
    o = &g_opts[g_items[g_sel].index];
    if (!opt_visible(o)) {
        snprintf(g_flash, sizeof(g_flash), "[option is hidden by unmet dependencies]");
        return;
    }
    if (o->type == OPT_BOOL) {
        cycle_bool(o, 0);
        mark_modified();
        build_items();
        select_opt(o);
    } else if (o->type == OPT_CHOICE) {
        popup_choice(o);
    } else {
        edit_string(o);
    }
}

static void set_bool_direct(const char *v)
{
    Option *o;
    if (!is_option_item(g_sel))
        return;
    o = &g_opts[g_items[g_sel].index];
    if (!opt_visible(o))
        return;
    if (o->type != OPT_BOOL)
        return;
    if (strcmp(v, "auto") == 0 && !o->tristate)
        return;
    if (strcmp(o->value, v) != 0) {
        set_value(o, v);
        mark_modified();
        build_items();
        select_opt(o);
    }
}

static void reset_default(void)
{
    Option *o;
    if (!is_option_item(g_sel))
        return;
    o = &g_opts[g_items[g_sel].index];
    if (!opt_visible(o))
        return;
    if (strcmp(o->value, o->def) != 0) {
        set_value(o, o->def);
        mark_modified();
        build_items();
        select_opt(o);
    }
}

int tui_run(const char *cache_path, int ask_configure)
{
    int running = 1;
    int exit_status = 0;

    g_cache_path = cache_path;
    g_abort_requested = 0;

    if (!ISATTY(INPUT_FD) || !ISATTY(OUTPUT_FD)) {
        fprintf(stderr, "rosconfig: --menu requires an interactive terminal\n");
        return 2;
    }
    if (term_setup() != 0)
        return 2;

    build_items();
    g_sel = 0;
    clamp_sel(+1);
    g_off = 0;

    while (running) {
        int key;
        redraw();
        key = term_getkey();
        g_flash[0] = '\0';
        switch (key) {
            case KEY_UP:    case 'k': move_sel(-1); break;
            case KEY_DOWN:  case 'j': move_sel(+1); break;
            case KEY_PGUP:  move_sel(-10); break;
            case KEY_PGDN:  move_sel(+10); break;
            case KEY_HOME:  g_sel = 0; clamp_sel(+1); break;
            case KEY_END:   g_sel = g_nitems - 1; clamp_sel(-1); break;
            case KEY_ENTER: case KEY_RIGHT: case ' ': activate(); break;
            case KEY_LEFT: case KEY_BACKSPACE:
                leave_menu();
                break;
            case KEY_ESC:
                if (!leave_menu() && try_quit(ask_configure, &exit_status))
                    running = 0;
                break;
            case 'y': case 'Y': set_bool_direct("y"); break;
            case 'n': case 'N': set_bool_direct("n"); break;
            case 'a': case 'A': set_bool_direct("auto"); break;
            case 'd': case 'D': reset_default(); break;
            case '?': case 'h': case 'H':
                show_selected_help();
                break;
            case KEY_F1: show_global_help(); break;
            case '/': search_options(); break;
            case 'v': case 'V': toggle_hidden(); break;
            case 'l': case 'L': reload_saved(); break;
            case 'r': case 'R': reset_all_defaults(); break;
            case 's': case 'S': do_save(); break;
            case KEY_ABORT:
                running = 0;
                break;
            case 'q': case 'Q':
                if (try_quit(ask_configure, &exit_status))
                    running = 0;
                break;
            default:
                break;
        }
        if (g_abort_requested)
            running = 0;
    }

    term_restore();
    return g_abort_requested ? 130 : exit_status;
}
