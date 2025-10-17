#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*
 * ReactOS stub implementation that only satisfies Wine's discovery
 * of the ntlm_auth helper. The actual NTLM handshake is still handled
 * by secur32 and will fail with BH responses until a full helper is
 * integrated.
 */

static void print_usage(void)
{
    fputs("Usage: ntlm_auth [--version] [--helper-protocol=PROTO] [options]\n", stdout);
}

static void print_version(void)
{
    puts("Version 3.0.25");
}

int main(int argc, char **argv)
{
    const char *protocol = NULL;
    int i;

    for (i = 1; i < argc; ++i)
    {
        const char *arg = argv[i];

        if (strcmp(arg, "--version") == 0)
        {
            print_version();
            return 0;
        }
        else if (strcmp(arg, "--usage") == 0 || strcmp(arg, "--help") == 0)
        {
            print_usage();
            return 0;
        }
        else if (strncmp(arg, "--helper-protocol=", 18) == 0)
        {
            protocol = arg + 18;
        }
    }

    if (!protocol)
    {
        fputs("ntlm_auth: missing --helper-protocol option\n", stderr);
        print_usage();
        return 1;
    }

    fprintf(stderr,
            "ntlm_auth: helper protocol '%s' is not implemented in this ReactOS build.\n",
            protocol);
    fprintf(stderr,
            "ntlm_auth: returning a permanent failure so the caller can fall back.\n");

    /* Consume a single helper request line and notify the caller about the failure. */
    while (1)
    {
        char buffer[4096];
        if (!fgets(buffer, sizeof(buffer), stdin))
            break;
        puts("BH ReactOS ntlm_auth helper is not implemented");
        fflush(stdout);
        break;
    }

    return 1;
}
