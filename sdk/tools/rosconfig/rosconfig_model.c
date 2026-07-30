/*
 * PROJECT:     ReactOS build tools
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Definition parser, configuration model, and cache output
 */

#include "rosconfig.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Option model                                                       */
/* ------------------------------------------------------------------ */

Option *g_opts;
int g_nopts;
static int g_copts;

Menu *g_menus;
int g_nmenus;
static int g_cmenus;

DefEntry *g_entries;
int g_nentries;
static int g_centries;

static char **g_extras;     /* unknown cache lines, preserved verbatim */
static int g_nextras, g_cextras;

typedef struct {
    char *key;
    char *value;
} Override;

static Override *g_ovr;
static int g_novr, g_covr;

void add_override(const char *kv)
{
    const char *eq = strchr(kv, '=');
    Override *ov;
    if (!eq)
        die("--override expects KEY=VALUE");
    ov = GROW(g_ovr, g_novr, g_covr);
    ov->key = xstrndup(kv, (size_t)(eq - kv));
    ov->value = xstrdup(eq + 1);
}

Option *find_opt(const char *key)
{
    int i;
    for (i = 0; i < g_nopts; i++)
        if (strcmp(g_opts[i].key, key) == 0)
            return &g_opts[i];
    return NULL;
}

static int add_menu(const char *title, int parent)
{
    Menu *menu = GROW(g_menus, g_nmenus, g_cmenus);
    DefEntry *entry;

    memset(menu, 0, sizeof(*menu));
    menu->title = xstrdup(title);
    menu->parent = parent;
    entry = GROW(g_entries, g_nentries, g_centries);
    entry->type = ENTRY_MENU;
    entry->index = g_nmenus - 1;
    return g_nmenus - 1;
}

static void add_option_entry(int index)
{
    DefEntry *entry = GROW(g_entries, g_nentries, g_centries);
    entry->type = ENTRY_OPTION;
    entry->index = index;
}

int choice_index(const Option *o, const char *value)
{
    int i;
    for (i = 0; i < o->nvalues; i++)
        if (strcmp(o->values[i].value, value) == 0)
            return i;
    return -1;
}

static int bool_value_ok(const Option *o, const char *v)
{
    return strcmp(v, "y") == 0 || strcmp(v, "n") == 0 ||
           (o->tristate && strcmp(v, "auto") == 0);
}

void set_value(Option *o, const char *v)
{
    free(o->value);
    o->value = xstrdup(v);
}

/* ------------------------------------------------------------------ */
/* Definition file parser                                             */
/* ------------------------------------------------------------------ */

static void def_error(const char *path, int lineno, const char *msg)
{
    die("%s:%d: %s", path, lineno, msg);
}

static void parse_depends_term(Dep **deps, int *ndeps, int *cdeps, const char *expr,
                               size_t length, int or_with_next, const char *path, int lineno)
{
    char *term = xstrndup(expr, length);
    const char *sep;
    Dep *d;

    rtrim(term);
    while (*term == ' ' || *term == '\t')
        term++;

    sep = strstr(term, "!=");
    d = GROW(*deps, *ndeps, *cdeps);
    d->negate = 0;
    d->or_with_next = or_with_next;
    if (sep) {
        d->negate = 1;
    } else {
        sep = strchr(term, '=');
        if (!sep)
            def_error(path, lineno, "depends expects KEY=VALUE or KEY!=VALUE");
    }
    d->key = xstrndup(term, (size_t)(sep - term));
    d->value = xstrdup(sep + (d->negate ? 2 : 1));
    rtrim(d->key);
    rtrim(d->value);
    if (d->key[0] == '\0' || d->value[0] == '\0')
        def_error(path, lineno, "depends expects KEY=VALUE or KEY!=VALUE");
}

static void parse_depends(Dep **deps, int *ndeps, int *cdeps, const char *expr, const char *path, int lineno)
{
    const char *term = expr;
    const char *alt;

    /*
     * Terms of one line joined by "||" are alternatives; separate depends
     * lines stay ANDed.
     */
    while ((alt = strstr(term, "||")) != NULL) {
        parse_depends_term(deps, ndeps, cdeps, term, (size_t)(alt - term), 1, path, lineno);
        term = alt + 2;
    }
    parse_depends_term(deps, ndeps, cdeps, term, strlen(term), 0, path, lineno);
}

static int path_is_absolute(const char *path)
{
    return path[0] == '/' || path[0] == '\\' ||
           (path[0] != '\0' && path[1] == ':');
}

static char *definition_source_path(const char *path, const char *source)
{
    const char *slash;
    const char *backslash;
    const char *separator;
    size_t directory_length;
    char *resolved;

    if (path_is_absolute(source))
        return xstrdup(source);
    slash = strrchr(path, '/');
    backslash = strrchr(path, '\\');
    separator = slash;
    if (!separator || (backslash && backslash > separator))
        separator = backslash;
    if (!separator)
        return xstrdup(source);
    directory_length = (size_t)(separator - path + 1);
    resolved = xmalloc(directory_length + strlen(source) + 1);
    memcpy(resolved, path, directory_length);
    strcpy(resolved + directory_length, source);
    return resolved;
}

static void parse_def_file(const char *path, int *menu_state, int depth)
{
    char *buf = read_file(path);
    char **lines;
    int nlines, i;
    int cur_menu = *menu_state;
    int base_menu = cur_menu;
    int menu_attrs = -1;
    Option *cur = NULL;

    if (depth > 16)
        die("definition source nesting is too deep at '%s'", path);
    if (!buf)
        die("cannot read definition file '%s'", path);
    lines = split_lines(buf, &nlines);

    for (i = 0; i < nlines; i++) {
        const char *p = lines[i];
        char *word;
        int lineno = i + 1;

        if (line_blank(p) || *skip_ws(p) == '#')
            continue;

        p = skip_ws(p);
        word = tok_word(&p);
        if (!word)
            continue;

        if (strcmp(word, "source") == 0) {
            char *source;
            char *resolved;
            source = tok_rest(&p);
            if (!source || source[0] == '\0')
                def_error(path, lineno, "source requires a path");
            resolved = definition_source_path(path, source);
            parse_def_file(resolved, &cur_menu, depth + 1);
            free(resolved);
            free(source);
            menu_attrs = -1;
            cur = NULL;
        } else if (strcmp(word, "menu") == 0) {
            char *title = tok_rest(&p);
            if (!title || title[0] == '\0')
                def_error(path, lineno, "menu requires a title");
            cur_menu = add_menu(title, cur_menu);
            menu_attrs = cur_menu;
            free(title);
            cur = NULL;
        } else if (strcmp(word, "endmenu") == 0) {
            if (cur_menu < 0 || cur_menu == base_menu)
                def_error(path, lineno, "endmenu without a matching menu");
            cur_menu = g_menus[cur_menu].parent;
            menu_attrs = -1;
            cur = NULL;
        } else if (strcmp(word, "config") == 0) {
            char *key = tok_word(&p);
            Option *o;
            if (!key)
                def_error(path, lineno, "config requires a name");
            if (find_opt(key))
                def_error(path, lineno, "duplicate config name");
            o = GROW(g_opts, g_nopts, g_copts);
            memset(o, 0, sizeof(*o));
            o->key = key;
            o->menu = cur_menu;
            o->type = OPT_BOOL;
            cur = o;
            menu_attrs = -1;
            add_option_entry(g_nopts - 1);
        } else if (!cur && menu_attrs >= 0 && strcmp(word, "depends") == 0) {
            char *v = tok_rest(&p);
            Menu *menu = &g_menus[menu_attrs];
            if (!v || v[0] == '\0')
                def_error(path, lineno, "depends requires an expression");
            parse_depends(&menu->deps, &menu->ndeps, &menu->cdeps, v, path, lineno);
            free(v);
        } else if (!cur) {
            def_error(path, lineno, "directive outside of a config block");
        } else if (strcmp(word, "prompt") == 0) {
            char *v = tok_rest(&p);
            if (!v)
                def_error(path, lineno, "unterminated string");
            free(cur->prompt);
            cur->prompt = v;
        } else if (strcmp(word, "type") == 0) {
            char *v = tok_word(&p);
            if (!v)
                def_error(path, lineno, "type requires a value");
            if (strcmp(v, "bool") == 0)
                cur->type = OPT_BOOL;
            else if (strcmp(v, "choice") == 0)
                cur->type = OPT_CHOICE;
            else if (strcmp(v, "string") == 0)
                cur->type = OPT_STRING;
            else
                def_error(path, lineno, "unknown type (bool/choice/string)");
            free(v);
        } else if (strcmp(word, "value") == 0) {
            char *v = tok_word(&p);
            ChoiceValue *cv;
            if (!v)
                def_error(path, lineno, "value requires a value");
            cv = GROW(cur->values, cur->nvalues, cur->cvalues);
            cv->value = v;
            cv->label = tok_rest(&p);
            if (cv->label && cv->label[0] == '\0') {
                free(cv->label);
                cv->label = NULL;
            }
        } else if (strcmp(word, "default") == 0) {
            char *v = tok_rest(&p);
            if (!v)
                def_error(path, lineno, "unterminated string");
            free(cur->def);
            cur->def = v;
        } else if (strcmp(word, "depends") == 0) {
            char *v = tok_rest(&p);
            if (!v || v[0] == '\0')
                def_error(path, lineno, "depends requires an expression");
            parse_depends(&cur->deps, &cur->ndeps, &cur->cdeps, v, path, lineno);
            free(v);
        } else if (strcmp(word, "meta") == 0) {
            cur->meta = 1;
        } else if (strcmp(word, "var") == 0) {
            char *v = tok_word(&p);
            if (!v)
                def_error(path, lineno, "var requires a name");
            free(cur->var);
            cur->var = v;
        } else if (strcmp(word, "cmaketype") == 0) {
            char *v = tok_word(&p);
            if (!v)
                def_error(path, lineno, "cmaketype requires a value");
            free(cur->cmaketype);
            cur->cmaketype = v;
        } else if (strcmp(word, "help") == 0) {
            int help_indent = line_indent(lines[i]);
            int base = -1, j = i + 1, pending_blanks = 0;
            SB sb = {0};
            for (; j < nlines; j++) {
                const char *l = lines[j];
                int ind;
                if (line_blank(l)) {
                    pending_blanks++;
                    continue;
                }
                ind = line_indent(l);
                if (ind <= help_indent)
                    break;
                if (base < 0)
                    base = ind;
                while (pending_blanks > 0) {
                    sb_str(&sb, "\n");
                    pending_blanks--;
                }
                {
                    const char *text = l;
                    int col = 0;
                    while (*text && col < base) {
                        if (*text == ' ')
                            col++;
                        else if (*text == '\t')
                            col = (col / 8 + 1) * 8;
                        else
                            break;
                        text++;
                    }
                    sb_str(&sb, text);
                    sb_str(&sb, "\n");
                }
            }
            i = j - 1;
            if (sb.buf) {
                rtrim(sb.buf);
                sb.len = strlen(sb.buf);
            }
            free(cur->help);
            cur->help = sb.buf ? xstrdup(sb.buf) : xstrdup("");
            sb_free(&sb);
        } else {
            def_error(path, lineno, "unknown directive");
        }
        free(word);
    }

    free(lines);
    free(buf);

    if (cur_menu != base_menu)
        die("%s: menu '%s' is missing endmenu", path, g_menus[cur_menu].title);
    *menu_state = cur_menu;
}

void load_def(const char *path)
{
    int cur_menu = -1;
    int i;

    parse_def_file(path, &cur_menu, 0);

    /* Finalize and validate. */
    for (i = 0; i < g_nopts; i++) {
        Option *o = &g_opts[i];
        if (!o->prompt)
            o->prompt = xstrdup(o->key);
        if (!o->var)
            o->var = xstrdup(o->key);
        if (o->type == OPT_CHOICE && o->nvalues == 0)
            die("%s: choice option %s has no values", path, o->key);
        if (!o->def) {
            if (o->type == OPT_BOOL)
                o->def = xstrdup("n");
            else if (o->type == OPT_CHOICE)
                o->def = xstrdup(o->values[0].value);
            else
                o->def = xstrdup("");
        }
        if (o->type == OPT_BOOL) {
            if (strcmp(o->def, "auto") == 0)
                o->tristate = 1;
            if (!bool_value_ok(o, o->def))
                die("%s: bool option %s has invalid default '%s'", path, o->key, o->def);
        }
        if (o->type == OPT_CHOICE && choice_index(o, o->def) < 0)
            die("%s: choice option %s default '%s' is not a listed value", path, o->key, o->def);
        if (!o->cmaketype)
            o->cmaketype = xstrdup(o->type == OPT_BOOL ? "BOOL" : "STRING");
        o->value = xstrdup(o->def);
    }
    if (g_nopts == 0)
        die("%s: no options defined", path);
}

/* ------------------------------------------------------------------ */
/* Cache file                                                         */
/* ------------------------------------------------------------------ */

void cache_load(const char *path)
{
    char *buf = read_file(path);
    char **lines;
    int nlines, i;

    if (!buf)
        return;                 /* no cache yet: defaults stay active */
    lines = split_lines(buf, &nlines);

    for (i = 0; i < nlines; i++) {
        char *line = lines[i];
        char *eq;
        Option *o;
        if (line_blank(line) || *skip_ws(line) == '#')
            continue;
        eq = strchr(line, '=');
        if (!eq) {
            *GROW(g_extras, g_nextras, g_cextras) = xstrdup(line);
            continue;
        }
        *eq = '\0';
        rtrim(line);
        o = find_opt(line);
        if (!o) {
            *eq = '=';
            *GROW(g_extras, g_nextras, g_cextras) = xstrdup(line);
            continue;
        }
        {
            char *val = xstrdup(eq + 1);
            rtrim(val);
            if (o->type == OPT_BOOL && !bool_value_ok(o, val)) {
                fprintf(stderr, "rosconfig: warning: invalid value '%s' for %s;" " using default '%s'\n", val, o->key, o->def);
                free(val);
            } else if (o->type == OPT_CHOICE && choice_index(o, val) < 0) {
                fprintf(stderr, "rosconfig: warning: unknown value '%s' for %s;" " using default '%s'\n", val, o->key, o->def);
                free(val);
            } else {
                free(o->value);
                o->value = val;
            }
        }
    }

    free(lines);
    free(buf);
}

void cache_reload(const char *path)
{
    int i;
    for (i = 0; i < g_nopts; i++)
        set_value(&g_opts[i], g_opts[i].def);
    for (i = 0; i < g_nextras; i++)
        free(g_extras[i]);
    g_nextras = 0;
    cache_load(path);
}

int cache_save(const char *path)
{
    FILE *f = fopen(path, "wb");
    int i, last_menu = -2;
    if (!f) {
        fprintf(stderr, "rosconfig: cannot write cache '%s'\n", path);
        return -1;
    }
    fprintf(f, "# ReactOS build configuration cache.\n");
    fprintf(f, "# Generated by rosconfig " ROSCONFIG_VERSION "; edit with" " ./menuconfig.sh (or configure with the\n");
    fprintf(f, "# \"menuconfig\" argument) instead of by hand." " Unknown lines are preserved.\n");
    for (i = 0; i < g_nopts; i++) {
        Option *o = &g_opts[i];
        if (o->menu != last_menu) {
            int chain[64];
            int menu = o->menu, depth = 0, j;
            fprintf(f, "\n# --- ");
            while (menu >= 0 && depth < (int)(sizeof(chain) / sizeof(chain[0]))) {
                chain[depth++] = menu;
                menu = g_menus[menu].parent;
            }
            if (depth == 0) {
                fprintf(f, "Options");
            } else {
                for (j = depth - 1; j >= 0; j--) {
                    if (j != depth - 1)
                        fprintf(f, " / ");
                    fprintf(f, "%s", g_menus[chain[j]].title);
                }
            }
            fprintf(f, "\n");
            last_menu = o->menu;
        }
        fprintf(f, "%s=%s\n", o->key, o->value);
    }
    if (g_nextras > 0) {
        fprintf(f, "\n# --- preserved entries (unknown to this rosconfig)\n");
        for (i = 0; i < g_nextras; i++)
            fprintf(f, "%s\n", g_extras[i]);
    }
    if (fclose(f) != 0) {
        fprintf(stderr, "rosconfig: error writing cache '%s'\n", path);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Dependencies, overrides, CMake generation                          */
/* ------------------------------------------------------------------ */

const char *config_value(const char *key)
{
    int i;
    Option *o;
    for (i = 0; i < g_novr; i++)
        if (strcmp(g_ovr[i].key, key) == 0)
            return g_ovr[i].value;
    o = find_opt(key);
    return o ? o->value : NULL;
}

static int deps_visible(const Dep *deps, int ndeps)
{
    int i = 0;
    while (i < ndeps) {
        int group_satisfied = 0;
        for (;;) {
            const Dep *d = &deps[i];
            const char *v = config_value(d->key);
            int eq = (v != NULL) && strcmp(v, d->value) == 0;
            if (d->negate ? !eq : eq)
                group_satisfied = 1;
            i++;
            if (!d->or_with_next)
                break;
        }
        if (!group_satisfied)
            return 0;
    }
    return 1;
}

static int menu_deps_visible(int menu)
{
    if (menu < 0)
        return 1;
    return menu_deps_visible(g_menus[menu].parent) && deps_visible(g_menus[menu].deps, g_menus[menu].ndeps);
}

int opt_visible(const Option *o)
{
    return menu_deps_visible(o->menu) && deps_visible(o->deps, o->ndeps);
}

static void emit_quoted(FILE *f, const char *s)
{
    fputc('"', f);
    for (; *s; s++) {
        if (*s == '"' || *s == '\\')
            fputc('\\', f);
        fputc(*s, f);
    }
    fputc('"', f);
}

int generate_cmake(const char *path)
{
    FILE *f = fopen(path, "wb");
    int i;
    const char *k[] = { "ARCH", "TOOLCHAIN", "BUILD_TYPE" };
    if (!f) {
        fprintf(stderr, "rosconfig: cannot write '%s'\n", path);
        return -1;
    }
    fprintf(f, "# Generated by rosconfig " ROSCONFIG_VERSION " from .rosconfig/config.cache - do not edit.\n");
    fprintf(f, "# Run ./menuconfig.sh (or configure with \"menuconfig\")" " to change these values.\n");
    fprintf(f, "# Included by /PreLoad.cmake; the cache sets below are not" " FORCEd, so explicit\n");
    fprintf(f, "# -D arguments on the cmake/configure command line still" " take precedence.\n#\n");
    fprintf(f, "# Effective target:");
    for (i = 0; i < (int)(sizeof(k) / sizeof(k[0])); i++) {
        const char *v = config_value(k[i]);
        if (v)
            fprintf(f, " %s=%s", k[i], v);
    }
    fprintf(f, "\n\n");

    for (i = 0; i < g_nopts; i++) {
        Option *o = &g_opts[i];
        if (o->meta || !opt_visible(o))
            continue;
        if (o->type == OPT_BOOL && strcmp(o->value, "auto") == 0)
            continue;   /* let config.cmake apply its conditional default */
        fprintf(f, "set(%s ", o->var);
        if (o->type == OPT_BOOL) {
            fputs(strcmp(o->value, "y") == 0 ? "TRUE" : "FALSE", f);
        } else {
            emit_quoted(f, o->value);
        }
        fprintf(f, " CACHE %s ", o->cmaketype);
        emit_quoted(f, o->prompt);
        fprintf(f, ")\n");
    }
    if (fclose(f) != 0) {
        fprintf(stderr, "rosconfig: error writing '%s'\n", path);
        return -1;
    }
    return 0;
}
