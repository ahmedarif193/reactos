/*
 * Provide std::codecvt<char,char,int>::id for toolchains where mbstate_t is
 * not int in the STL but remains int in our CRT headers.
 */
#include <locale>

namespace std
{
    locale::id codecvt<char, char, int>::id;
}
