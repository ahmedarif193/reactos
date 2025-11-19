/*
 * PROJECT:     GCC c++ support library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     __throw_out_of_range_fmt implementation
 * COPYRIGHT:   Copyright 2024 Timo Kreuzer <timo.kreuzer@reactos.org>
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>

#if defined(__has_include)
#  if __has_include(<stdexcept>)
#    define PAL_STDCPP_HAS_STDEXCEPT 1
#    include <stdexcept>
#  elif __has_include(<exception>)
#    define PAL_STDCPP_HAS_EXCEPTION 1
#    include <exception>
#  endif
#endif

#ifdef PAL_STDCPP_FORCE_THROW_OUT_OF_RANGE_FMT
#undef PAL_STDCPP_HAS_STDEXCEPT
#endif

#if !defined(PAL_STDCPP_HAS_STDEXCEPT)
namespace std
{
#if !defined(PAL_STDCPP_HAS_EXCEPTION)
class exception
{
public:
    exception() noexcept = default;
    virtual ~exception() = default;
    virtual const char* what() const noexcept { return ""; }
};
#endif

class out_of_range : public exception
{
    char* m_msg;
public:
    explicit out_of_range(const char* msg) noexcept
        : m_msg(nullptr)
    {
        const size_t len = strlen(msg) + 1;
        m_msg = static_cast<char*>(malloc(len));
        if (m_msg)
            memcpy(m_msg, msg, len);
    }

    ~out_of_range() override
    {
        if (m_msg)
            free(m_msg);
    }

    const char* what() const noexcept override
    {
        return m_msg ? m_msg : "";
    }
};
}
#endif

#if !defined(PAL_STDCPP_HAS_STDEXCEPT) || defined(PAL_STDCPP_FORCE_THROW_OUT_OF_RANGE_FMT)
namespace std {

void __throw_out_of_range_fmt(const char *format, ...)
{
    char buffer[1024];
    va_list argptr;

    va_start(argptr, format);
    _vsnprintf(buffer, sizeof(buffer), format, argptr);
    buffer[sizeof(buffer) - 1] = 0;
    va_end(argptr);

    throw out_of_range(buffer);
}

}  // namespace std
#endif
