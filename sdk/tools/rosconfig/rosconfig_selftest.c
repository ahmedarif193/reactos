/*
 * PROJECT:     ReactOS build tools
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Built-in non-interactive tests for rosconfig
 */

#include "rosconfig.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

typedef struct {
    int checks;
    int failures;
} SelfTest;

static void expect(SelfTest *test, int condition, const char *description)
{
    test->checks++;
    if (!condition) {
        test->failures++;
        fprintf(stderr, "not ok %d - %s\n", test->checks, description);
    }
}

static void expect_string(SelfTest *test, const char *actual, const char *expected, const char *description)
{
    int matches = actual != NULL && strcmp(actual, expected) == 0;
    expect(test, matches, description);
    if (!matches)
        fprintf(stderr, "  expected: '%s'\n  actual:   '%s'\n", expected, actual ? actual : "(null)");
}

static int write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    size_t length = strlen(text);
    size_t written;
    int close_result;

    if (!file)
        return -1;
    written = fwrite(text, 1, length, file);
    close_result = fclose(file);
    return written == length && close_result == 0 ? 0 : -1;
}

static int file_contains(const char *path, const char *text)
{
    char *contents = read_file(path);
    int found = contents != NULL && strstr(contents, text) != NULL;
    free(contents);
    return found;
}

static int make_temp_path(char *path, size_t size, const char *tag)
{
    const char *base;
    size_t length;
    char separator;
    unsigned long process;
    int written;

#ifdef _WIN32
    base = getenv("TEMP");
    if (!base || !base[0])
        base = getenv("TMP");
    process = (unsigned long)_getpid();
    separator = '\\';
#else
    base = getenv("TMPDIR");
    process = (unsigned long)getpid();
    separator = '/';
#endif
    if (!base || !base[0])
        base = ".";
    length = strlen(base);
    written = snprintf(path, size, "%s%srosconfig-selftest-%lu-%lu-%s.tmp", base,
                       length > 0 && (base[length - 1] == '/' || base[length - 1] == '\\') ? "" :
                       (separator == '/' ? "/" : "\\"), process, (unsigned long)time(NULL), tag);
    return written < 0 || (size_t)written >= size ? -1 : 0;
}

static const char *path_basename(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    const char *separator = slash;

    if (!separator || (backslash && backslash > separator))
        separator = backslash;
    return separator ? separator + 1 : path;
}

int rosconfig_self_test(void)
{
    static const char definition_prefix[] =
        "menu \"Target platform\"\n"
        "config ARCH\n"
        "    prompt \"Target architecture\"\n"
        "    type choice\n"
        "    value amd64 \"AMD64\"\n"
        "    value i386 \"i386\"\n"
        "    value arm64 \"ARM64\"\n"
        "    default amd64\n"
        "    meta\n"
        "    help\n"
        "      Select the target architecture.\n"
        "\n"
        "config TOOLCHAIN\n"
        "    prompt \"Compiler toolchain\"\n"
        "    type choice\n"
        "    value clang \"Clang\"\n"
        "    value gcc \"GCC\"\n"
        "    value msvc \"MSVC\"\n"
        "    default clang\n"
        "    meta\n"
        "\n"
        "config BUILD_TYPE\n"
        "    prompt \"Build type\"\n"
        "    type choice\n"
        "    value Debug \"Debug\"\n"
        "    value Release \"Release\"\n"
        "    default Debug\n"
        "    meta\n"
        "\n";
    static const char definition_suffix[] =
        "endmenu\n"
        "\n"
        "menu \"Modules\"\n"
        "config ENABLE_ROSTESTS\n"
        "    prompt \"ReactOS test suite and RosAutoTest\"\n"
        "    type bool\n"
        "    default n\n"
        "    depends BUILD_TYPE=Debug\n"
        "endmenu\n"
        "\n"
        "menu \"Build\"\n"
        "    depends ARCH=amd64\n"
        "\n"
        "config ENABLE\n"
        "    prompt \"Enable advanced settings\"\n"
        "    type bool\n"
        "    default n\n"
        "\n"
        "menu \"Advanced\"\n"
        "    depends ENABLE=y\n"
        "\n"
        "config LEVEL\n"
        "    prompt \"Detail level\"\n"
        "    type choice\n"
        "    value basic \"Basic\"\n"
        "    value expert \"Expert\"\n"
        "    default basic\n"
        "\n"
        "config LABEL\n"
        "    prompt \"Display label\"\n"
        "    type string\n"
        "    default \"starter\"\n"
        "    depends LEVEL!=basic\n"
        "    help\n"
        "      Label emitted into the CMake cache.\n"
        "\n"
        "endmenu\n"
        "endmenu\n"
        "\n"
        "menu \"Debug compiler level\"\n"
        "    depends BUILD_TYPE=Debug\n"
        "config OPTIMIZE\n"
        "    prompt \"GCC/Clang compiler level\"\n"
        "    type choice\n"
        "    value 0 \"Off\"\n"
        "    value 4 \"Optimize\"\n"
        "    default 4\n"
        "    depends TOOLCHAIN!=msvc\n"
        "config OPTIMIZE_MSVC\n"
        "    prompt \"MSVC compiler level\"\n"
        "    type choice\n"
        "    var OPTIMIZE\n"
        "    value 0 \"Off\"\n"
        "    value 4 \"Disable optimization\"\n"
        "    default 4\n"
        "    depends TOOLCHAIN=msvc\n"
        "endmenu\n"
        "\n"
        "menu \"Release optimizations\"\n"
        "    depends BUILD_TYPE=Release\n"
        "    depends TOOLCHAIN!=msvc\n"
        "config LTCG\n"
        "    prompt \"Link-time code generation\"\n"
        "    type bool\n"
        "    default n\n"
        "endmenu\n"
        "\n"
        "menu \"Toolchain support\"\n"
        "    depends TOOLCHAIN!=msvc\n"
        "config STACK_PROTECTOR\n"
        "    prompt \"GCC stack protector\"\n"
        "    type bool\n"
        "    default n\n"
        "    depends TOOLCHAIN=gcc\n"
        "config USE_DUMMY_PSEH\n"
        "    prompt \"Use dummy PSEH\"\n"
        "    type bool\n"
        "    default n\n"
        "endmenu\n"
        "\n"
        "menu \"MSVC diagnostics\"\n"
        "    depends TOOLCHAIN=msvc\n"
        "config RUNTIME_CHECKS\n"
        "    prompt \"Runtime checks\"\n"
        "    type bool\n"
        "    default auto\n"
        "    depends BUILD_TYPE=Debug\n"
        "config _PREFAST_\n"
        "    prompt \"PREFAST\"\n"
        "    type bool\n"
        "    default n\n"
        "endmenu\n"
        "\n"
        "menu \"Boot options\"\n"
        "config FREELDR_HTTP_BOOT\n"
        "    prompt \"Enable HTTP boot\"\n"
        "    type bool\n"
        "    default n\n"
        "    depends PROFILE_AMD64=lattepandamu || PROFILE_ARM64=rpi5\n"
        "endmenu\n";
    static const char included_definition[] =
        "config PROFILE_AMD64\n"
        "    prompt \"Target profile\"\n"
        "    type choice\n"
        "    var ROSCONFIG_PROFILE\n"
        "    value generic \"Generic AMD64\"\n"
        "    value lattepandamu \"LattePanda Mu\"\n"
        "    default generic\n"
        "    depends ARCH=amd64\n"
        "config PROFILE_I386\n"
        "    prompt \"Target profile\"\n"
        "    type choice\n"
        "    var ROSCONFIG_PROFILE\n"
        "    value generic \"Generic i386\"\n"
        "    default generic\n"
        "    depends ARCH=i386\n"
        "config PROFILE_ARM64\n"
        "    prompt \"Target profile\"\n"
        "    type choice\n"
        "    var ROSCONFIG_PROFILE\n"
        "    value generic \"Generic ARM64\"\n"
        "    value rpi5 \"Raspberry Pi 5\"\n"
        "    default generic\n"
        "    depends ARCH=arm64\n";
    static const char cache[] =
        "ENABLE=y\n"
        "LEVEL=expert\n"
        "LABEL=from cache\n"
        "UNKNOWN_KEEP=y\n"
        "legacy line\n";
    char definition_path[FILENAME_MAX];
    char included_definition_path[FILENAME_MAX];
    char input_cache_path[FILENAME_MAX];
    char saved_cache_path[FILENAME_MAX];
    char generated_path[FILENAME_MAX];
    SB definition_text = {0};
    SelfTest test = {0, 0};
    Option *arch;
    Option *toolchain;
    Option *build_type;
    Option *enable;
    Option *level;
    Option *label;
    Option *optimize;
    Option *optimize_msvc;
    Option *ltcg;
    Option *stack_protector;
    Option *dummy_pseh;
    Option *runtime_checks;
    Option *prefast;
    Option *profile_amd64;
    Option *profile_i386;
    Option *profile_arm64;
    Option *http_boot;
    Option *enable_rostests;

    expect(&test, rosconfig_quit_status(0, 0, 1, 1, 1) == 0,
           "clean exit can continue into configuration");
    expect(&test, rosconfig_quit_status(0, 0, 1, 1, 0) == ROSCONFIG_EXIT_SKIP_CONFIGURE,
           "clean exit can skip configuration");
    expect(&test, rosconfig_quit_status(0, 0, 1, 1, -1) < 0,
           "configure confirmation can return to the menu");
    expect(&test, rosconfig_quit_status(1, 1, 1, 1, 1) == 0,
           "saved changes can continue into configuration");
    expect(&test, rosconfig_quit_status(1, 0, 1, 1, 0) == ROSCONFIG_EXIT_SKIP_CONFIGURE,
           "discarded changes can skip configuration");
    expect(&test, rosconfig_quit_status(1, -1, 1, 1, 1) < 0,
           "canceling the save prompt stays in the menu");
    expect(&test, rosconfig_quit_status(1, 1, 0, 1, 1) < 0,
           "a failed save stays in the menu");

    if (make_temp_path(definition_path, sizeof(definition_path), "definition") != 0 ||
        make_temp_path(included_definition_path, sizeof(included_definition_path), "included-definition") != 0 ||
        make_temp_path(input_cache_path, sizeof(input_cache_path), "input-cache") != 0 ||
        make_temp_path(saved_cache_path, sizeof(saved_cache_path), "saved-cache") != 0 ||
        make_temp_path(generated_path, sizeof(generated_path), "generated") != 0) {
        fprintf(stderr, "rosconfig self-test: temporary path is too long\n");
        return 1;
    }
    sb_str(&definition_text, definition_prefix);
    sb_str(&definition_text, "source \"");
    sb_str(&definition_text, path_basename(included_definition_path));
    sb_str(&definition_text, "\"\n");
    sb_str(&definition_text, definition_suffix);
    if (write_text(included_definition_path, included_definition) != 0 ||
        write_text(definition_path, definition_text.buf) != 0 ||
        write_text(input_cache_path, cache) != 0) {
        fprintf(stderr, "rosconfig self-test: cannot create temporary fixtures\n");
        remove(definition_path);
        remove(included_definition_path);
        remove(input_cache_path);
        sb_free(&definition_text);
        return 1;
    }

    load_def(definition_path);
    arch = find_opt("ARCH");
    toolchain = find_opt("TOOLCHAIN");
    build_type = find_opt("BUILD_TYPE");
    enable = find_opt("ENABLE");
    level = find_opt("LEVEL");
    label = find_opt("LABEL");
    optimize = find_opt("OPTIMIZE");
    optimize_msvc = find_opt("OPTIMIZE_MSVC");
    ltcg = find_opt("LTCG");
    stack_protector = find_opt("STACK_PROTECTOR");
    dummy_pseh = find_opt("USE_DUMMY_PSEH");
    runtime_checks = find_opt("RUNTIME_CHECKS");
    prefast = find_opt("_PREFAST_");
    profile_amd64 = find_opt("PROFILE_AMD64");
    profile_i386 = find_opt("PROFILE_I386");
    profile_arm64 = find_opt("PROFILE_ARM64");
    http_boot = find_opt("FREELDR_HTTP_BOOT");
    enable_rostests = find_opt("ENABLE_ROSTESTS");

    expect(&test, g_nopts == 18, "parser creates options from the root and sourced files");
    expect(&test, g_nmenus == 9, "parser creates target, module, and nested menus");
    expect(&test, g_nentries == 27, "definition order includes sourced menus and options");
    expect(&test, g_menus[2].parent == -1 && g_menus[3].parent == 2, "nested menu has the correct parent");
    expect(&test, g_menus[2].ndeps == 1 && g_menus[3].ndeps == 1, "menu dependencies are parsed");
    expect(&test, arch && toolchain && build_type && enable && level && label && optimize && optimize_msvc && ltcg && stack_protector && dummy_pseh && runtime_checks && prefast && profile_amd64 && profile_i386 && profile_arm64 && http_boot && enable_rostests, "symbols can be found across definition files");
    if (!arch || !toolchain || !build_type || !enable || !level || !label || !optimize || !optimize_msvc || !ltcg || !stack_protector || !dummy_pseh || !runtime_checks || !prefast || !profile_amd64 || !profile_i386 || !profile_arm64 || !http_boot || !enable_rostests)
        goto cleanup;

    expect_string(&test, arch->help, "Select the target architecture.", "indented help text is parsed");
    expect_string(&test, label->def, "starter", "quoted string default is parsed");
    expect_string(&test, optimize_msvc->var, "OPTIMIZE", "toolchain-specific symbols can share one CMake variable");
    expect(&test, profile_amd64->menu == arch->menu && profile_i386->menu == arch->menu && profile_arm64->menu == arch->menu, "profiles are part of the target platform menu");
    expect(&test, !opt_visible(http_boot), "HTTP boot is hidden from the generic AMD64 profile");
    set_value(profile_amd64, "lattepandamu");
    expect(&test, opt_visible(http_boot), "the LattePanda Mu profile exposes HTTP boot");
    set_value(profile_amd64, "generic");
    expect(&test, !opt_visible(http_boot), "leaving the LattePanda Mu profile hides HTTP boot");
    expect(&test, http_boot->ndeps == 2 && http_boot->deps[0].or_with_next && !http_boot->deps[1].or_with_next,
           "alternative dependency terms are parsed as one group");
    set_value(profile_arm64, "rpi5");
    expect(&test, opt_visible(http_boot), "the second alternative also exposes HTTP boot");
    set_value(profile_arm64, "generic");
    expect(&test, !opt_visible(http_boot), "HTTP boot is hidden when no alternative holds");
    expect(&test, label->ndeps == 1 && label->deps[0].negate, "negated option dependency is parsed");
    expect(&test, opt_visible(enable_rostests), "rostests are visible in Debug mode");
    expect(&test, opt_visible(enable), "root menu dependency is initially met");
    expect(&test, !opt_visible(level), "nested menu dependency initially hides its option");
    set_value(enable, "y");
    expect(&test, opt_visible(level), "nested menu becomes visible immediately");
    expect(&test, !opt_visible(label), "direct option dependency is enforced");
    set_value(level, "expert");
    expect(&test, opt_visible(label), "direct option dependency updates immediately");
    set_value(arch, "arm64");
    expect(&test, !opt_visible(enable) && !opt_visible(level) && !opt_visible(label), "parent menu dependency is inherited");
    expect(&test, !opt_visible(profile_amd64) && opt_visible(profile_arm64), "only the selected architecture's sourced profile is visible");
    expect(&test, opt_visible(enable_rostests), "rostests remain architecture-independent in Debug mode");
    set_value(profile_arm64, "rpi5");
    set_value(arch, "i386");
    expect(&test, !opt_visible(profile_amd64) && opt_visible(profile_i386) && !opt_visible(profile_arm64), "the i386 profile is isolated from other architectures");
    set_value(arch, "amd64");
    expect(&test, opt_visible(profile_amd64) && !opt_visible(profile_i386) && !opt_visible(profile_arm64), "profile visibility follows architecture changes immediately");

    expect(&test, opt_visible(optimize) && !opt_visible(optimize_msvc), "Debug mode shows the matching compiler-level selector");
    expect(&test, !opt_visible(ltcg), "Release optimization is hidden in Debug mode");
    expect(&test, opt_visible(dummy_pseh), "dummy PSEH is available to Clang");
    expect(&test, !opt_visible(stack_protector) && !opt_visible(runtime_checks) && !opt_visible(prefast), "Clang hides GCC-only and MSVC-only controls");
    set_value(toolchain, "gcc");
    expect(&test, opt_visible(stack_protector) && opt_visible(dummy_pseh), "GCC exposes its stack and PSEH controls");
    set_value(toolchain, "clang");
    set_value(build_type, "Release");
    expect(&test, !opt_visible(enable_rostests), "Release mode hides rostests");
    expect(&test, !opt_visible(optimize) && !opt_visible(optimize_msvc), "compiler-level selectors are hidden when Release fixes the flags");
    expect(&test, opt_visible(ltcg), "Release mode exposes the effective optimization control");
    set_value(toolchain, "msvc");
    expect(&test, !opt_visible(ltcg) && !opt_visible(dummy_pseh), "GNU-style toolchain controls are hidden for MSVC");
    expect(&test, !opt_visible(runtime_checks) && opt_visible(prefast), "MSVC Release hides runtime checks but exposes static analysis");
    set_value(build_type, "Debug");
    expect(&test, opt_visible(enable_rostests), "Debug mode restores rostests");
    expect(&test, !opt_visible(optimize) && opt_visible(optimize_msvc), "MSVC gets its own non-Release compiler levels");
    expect(&test, opt_visible(runtime_checks) && opt_visible(prefast), "MSVC Debug exposes runtime checks and static analysis");
    set_value(toolchain, "clang");

    cache_load(input_cache_path);
    expect_string(&test, enable->value, "y", "cache loads a boolean");
    expect_string(&test, level->value, "expert", "cache loads a choice");
    expect_string(&test, label->value, "from cache", "cache loads a string containing spaces");
    expect(&test, cache_save(saved_cache_path) == 0, "cache saves successfully");
    expect(&test, file_contains(saved_cache_path, "# --- Build / Advanced"), "saved cache records the nested menu path");
    expect(&test, file_contains(saved_cache_path, "UNKNOWN_KEEP=y") && file_contains(saved_cache_path, "legacy line"), "unknown cache entries are preserved");
    set_value(enable, "n");
    cache_reload(input_cache_path);
    expect_string(&test, enable->value, "y", "cache reload discards an unsaved value");

    set_value(enable_rostests, "y");
    set_value(label, "quoted \"path\\tail");
    expect(&test, generate_cmake(generated_path) == 0, "CMake fragment generates successfully");
    expect(&test, !file_contains(generated_path, "set(ARCH "), "meta options are not emitted to CMake");
    expect(&test, file_contains(generated_path, "set(ENABLE TRUE CACHE BOOL \"Enable advanced settings\")"), "boolean values use CMake boolean syntax");
    expect(&test, file_contains(generated_path, "set(LEVEL \"expert\" CACHE STRING \"Detail level\")"), "choice values are quoted");
    expect(&test, file_contains(generated_path, "set(LABEL \"quoted \\\"path\\\\tail\" CACHE STRING \"Display label\")"), "string values are escaped for CMake");
    expect(&test, file_contains(generated_path, "set(ROSCONFIG_PROFILE \"generic\" CACHE STRING \"Target profile\")"), "the selected architecture's generic profile is emitted");
    expect(&test, file_contains(generated_path, "set(ENABLE_ROSTESTS TRUE CACHE BOOL \"ReactOS test suite and RosAutoTest\")"), "the RosAutoTest module is emitted independently of the profile");
    expect(&test, !file_contains(generated_path, "rpi5"), "a hidden architecture profile is not emitted");

    set_value(profile_amd64, "lattepandamu");
    set_value(http_boot, "y");
    expect(&test, generate_cmake(generated_path) == 0, "CMake fragment regenerates for the LattePanda Mu HTTP boot option");
    expect(&test, file_contains(generated_path, "set(ROSCONFIG_PROFILE \"lattepandamu\" CACHE STRING \"Target profile\")"), "the LattePanda Mu profile is emitted");
    expect(&test, file_contains(generated_path, "set(FREELDR_HTTP_BOOT TRUE CACHE BOOL \"Enable HTTP boot\")"), "the LattePanda Mu HTTP boot option is emitted");
    set_value(profile_amd64, "generic");

    set_value(enable, "n");
    expect(&test, generate_cmake(generated_path) == 0, "CMake fragment regenerates after a dependency change");
    expect(&test, !file_contains(generated_path, "set(LEVEL ") && !file_contains(generated_path, "set(LABEL "), "dependency-hidden options are not emitted");

    set_value(arch, "arm64");
    set_value(profile_arm64, "rpi5");
    expect(&test, !opt_visible(enable), "changed target hides an incompatible menu");
    expect(&test, generate_cmake(generated_path) == 0, "CMake fragment regenerates for another architecture profile");
    expect(&test, file_contains(generated_path, "set(ROSCONFIG_PROFILE \"rpi5\" CACHE STRING \"Target profile\")"), "the ARM64 Raspberry Pi 5 profile is emitted");
    expect(&test, file_contains(generated_path, "set(ENABLE_ROSTESTS TRUE CACHE BOOL \"ReactOS test suite and RosAutoTest\")"), "the RosAutoTest module composes with the Raspberry Pi 5 profile");
    add_override("ARCH=amd64");
    expect(&test, opt_visible(enable), "transient overrides participate in dependency evaluation");
    expect_string(&test, config_value("ARCH"), "amd64", "transient override has value precedence");

cleanup:
    remove(definition_path);
    remove(included_definition_path);
    remove(input_cache_path);
    remove(saved_cache_path);
    remove(generated_path);
    sb_free(&definition_text);
    if (test.failures) {
        fprintf(stderr, "rosconfig self-test: %d/%d checks failed\n", test.failures, test.checks);
        return 1;
    }
    printf("rosconfig self-test: ok (%d checks)\n", test.checks);
    return 0;
}
