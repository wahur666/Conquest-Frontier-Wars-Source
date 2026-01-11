#pragma once

#include <string>
#include <cstdarg>
#include <cstdio>
#include <format>

constexpr size_t TempStrSize = 8 * 1024;

class TempStr
{
private:
    std::string buffer;

public:
    TempStr() = default;

    // Constructor with variadic arguments (C++20 style with std::format)
    template<typename... Args>
    TempStr(std::string_view fmt, Args&&... args)
    {
        buffer = std::vformat(fmt, std::make_format_args(args...));
    }

    // Set with variadic arguments
    template<typename... Args>
    void set(std::string_view fmt, Args&&... args)
    {
        buffer = std::vformat(fmt, std::make_format_args(args...));
    }

    // Traditional printf-style (if needed for compatibility)
    void set_printf(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        set_printf_impl(fmt, args);
        va_end(args);
    }

    // Conversion operators
    operator std::string() const { return buffer; }
    operator std::string_view() const { return buffer; }
    operator const char*() const { return buffer.c_str(); }

    // Utility methods
    const char* c_str() const { return buffer.c_str(); }
    std::string_view view() const { return buffer; }
    size_t size() const { return buffer.size(); }
    bool empty() const { return buffer.empty(); }

private:
    void set_printf_impl(const char* fmt, va_list args)
    {
        char temp[TempStrSize];
        vsnprintf(temp, TempStrSize, fmt, args);
        buffer = temp;
    }
};

// No macro needed - use directly: TempStr("format", args...)