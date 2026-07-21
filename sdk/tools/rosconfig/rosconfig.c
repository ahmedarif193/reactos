/*
 * PROJECT:     ReactOS build tools
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Command-line entry point for rosconfig
 */

#include "rosconfig.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Entry point                                                        */
/* ------------------------------------------------------------------ */

static void usage(FILE *f)
{
    fprintf(f, "rosconfig " ROSCONFIG_VERSION " - menuconfig-style configurator for the ReactOS build\n" "\n" "Usage: rosconfig --def <file> --cache <file> [mode] [options]\n" "       rosconfig --self-test\n" "\n" "Modes (default: --menu):\n" "  --menu             interactive terminal UI\n" "  --defaults         create/refresh the cache file (keeps existing values)\n" "  --generate <out>   write a CMake pre-load fragment from the cache\n" "  --get <KEY>        print the cached (or default) value of an option\n" "  --self-test        run parser/model/cache/generator tests and exit\n" "\n" "Options:\n" "  --ask-configure    on clean menu exit, ask whether configure should run\n" "  --override K=V     transient value used for dependency evaluation in\n" "                     --generate/--get (e.g. the ARCH chosen on the\n" "                     configure command line); not written to the cache\n" "  --version          print version and exit\n" "  --help             this text\n");
}
int main(int argc, char **argv)
{
    const char *def_path = NULL;
    const char *cache_path = NULL;
    const char *gen_path = NULL;
    const char *get_key = NULL;
    enum { M_MENU, M_DEFAULTS, M_GENERATE, M_GET, M_SELF_TEST } mode = M_MENU;
    int ask_configure = 0;
    int i;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--def") == 0 && i + 1 < argc) {
            def_path = argv[++i];
        } else if (strcmp(a, "--cache") == 0 && i + 1 < argc) {
            cache_path = argv[++i];
        } else if (strcmp(a, "--menu") == 0) {
            mode = M_MENU;
        } else if (strcmp(a, "--defaults") == 0) {
            mode = M_DEFAULTS;
        } else if (strcmp(a, "--generate") == 0 && i + 1 < argc) {
            mode = M_GENERATE;
            gen_path = argv[++i];
        } else if (strcmp(a, "--get") == 0 && i + 1 < argc) {
            mode = M_GET;
            get_key = argv[++i];
        } else if (strcmp(a, "--self-test") == 0) {
            mode = M_SELF_TEST;
        } else if (strcmp(a, "--ask-configure") == 0) {
            ask_configure = 1;
        } else if (strcmp(a, "--override") == 0 && i + 1 < argc) {
            add_override(argv[++i]);
        } else if (strcmp(a, "--version") == 0) {
            printf("rosconfig " ROSCONFIG_VERSION "\n");
            return 0;
        } else if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            usage(stdout);
            return 0;
        } else {
            usage(stderr);
            die("unknown argument '%s'", a);
        }
    }

    if (mode == M_SELF_TEST)
        return rosconfig_self_test();

    if (!def_path || !cache_path) {
        usage(stderr);
        die("--def and --cache are required");
    }

    load_def(def_path);
    cache_load(cache_path);

    switch (mode) {
        case M_DEFAULTS:
            return cache_save(cache_path) == 0 ? 0 : 2;
        case M_GENERATE:
            return generate_cmake(gen_path) == 0 ? 0 : 2;
        case M_GET: {
            Option *o = find_opt(get_key);
            printf("%s\n", o ? o->value : "");
            return 0;
        }
        case M_SELF_TEST:
            return rosconfig_self_test();
        case M_MENU:
        default:
            return tui_run(cache_path, ask_configure);
    }
}
