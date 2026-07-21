/*
 * PROJECT:     ReactOS build tools
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Shared declarations for the rosconfig host tool
 */

#pragma once

#ifdef _WIN32
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#endif

#include <stddef.h>
#include <stdio.h>

#define ROSCONFIG_VERSION "1.2"

void die(const char *fmt, ...);
void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);

#define GROW(arr, count, cap) \
    (((count) >= (cap) ? ((cap) = (cap) ? (cap) * 2 : 8, \
       (arr) = xrealloc((arr), (cap) * sizeof(*(arr)))) : (void *)0), \
     &(arr)[(count)++])

typedef struct {
    char *buf;
    size_t len, cap;
} SB;

void sb_addn(SB *sb, const char *s, size_t n);
void sb_str(SB *sb, const char *s);
void sb_fmt(SB *sb, const char *fmt, ...);
void sb_free(SB *sb);
void rtrim(char *s);
const char *skip_ws(const char *p);
int line_indent(const char *line);
int line_blank(const char *line);
char *tok_word(const char **pp);
char *tok_rest(const char **pp);
char *read_file(const char *path);
char **split_lines(char *buf, int *count);

typedef enum {
    OPT_BOOL,
    OPT_CHOICE,
    OPT_STRING
} OptType;

typedef struct {
    char *value;
    char *label;
} ChoiceValue;

typedef struct {
    char *key;
    char *value;
    int negate;
} Dep;

typedef struct {
    char *key;
    char *var;
    char *prompt;
    char *help;
    char *cmaketype;
    char *def;
    char *value;
    OptType type;
    int meta;
    int tristate;
    int menu;
    ChoiceValue *values;
    int nvalues, cvalues;
    Dep *deps;
    int ndeps, cdeps;
} Option;

typedef struct {
    char *title;
    int parent;
    Dep *deps;
    int ndeps, cdeps;
} Menu;

typedef enum {
    ENTRY_MENU,
    ENTRY_OPTION
} EntryType;

typedef struct {
    EntryType type;
    int index;
} DefEntry;

extern Option *g_opts;
extern int g_nopts;
extern Menu *g_menus;
extern int g_nmenus;
extern DefEntry *g_entries;
extern int g_nentries;

void add_override(const char *kv);
Option *find_opt(const char *key);
int choice_index(const Option *o, const char *value);
void set_value(Option *o, const char *v);
void load_def(const char *path);
void cache_load(const char *path);
void cache_reload(const char *path);
int cache_save(const char *path);
const char *config_value(const char *key);
int opt_visible(const Option *o);
int generate_cmake(const char *path);

#define ROSCONFIG_EXIT_SKIP_CONFIGURE 3

int rosconfig_quit_status(int had_changes, int save_choice,
                          int save_succeeded, int ask_configure,
                          int configure_choice);
int tui_run(const char *cache_path, int ask_configure);
int rosconfig_self_test(void);
