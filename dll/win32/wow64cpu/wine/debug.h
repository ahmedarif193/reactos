/* Minimal Wine debug compatibility for ReactOS WoW64 imports. */

#pragma once

#define WINE_DEFAULT_DEBUG_CHANNEL(channel)
#define TRACE(...) do { } while (0)
#define WARN(...) DbgPrint(__VA_ARGS__)
#define ERR(...) DbgPrint(__VA_ARGS__)
#define FIXME(...) DbgPrint(__VA_ARGS__)
