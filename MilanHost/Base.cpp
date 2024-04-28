#include <array>
#include <cstdio>
#include <cwchar>

#include "Base.hpp"


// ! ILogger implementation.
// ========================================

void ILogger::WriteFormatAnsi(CodePlaceA const& Place, ESeverity const Severity,
    std::string_view const Format, ...)
{
    std::va_list Args;
    va_start(Args, Format);
    WriteFormatAnsiVararg(Place, Severity, Format, Args);
    va_end(Args);
}

void ILogger::WriteFormatUnicode(CodePlaceW const& Place, ESeverity const Severity,
    std::wstring_view const Format, ...)
{
    std::va_list Args;
    va_start(Args, Format);
    WriteFormatUnicodeVararg(Place, Severity, Format, Args);
    va_end(Args);
}

void ILogger::WriteFormatAnsiVararg(CodePlaceA const& Place, ESeverity Severity,
    std::string_view const Format, std::va_list Args)
{
    thread_local std::array<char, 2048> FormatBuffer{ '\0' };
    int Result = _vsnprintf(FormatBuffer.data(), FormatBuffer.size() - 1, Format.data(), Args);

    if (Result < 0)
    {
        MILAN_BREAK_ATTACHED();
        Result = _snprintf(FormatBuffer.data(), FormatBuffer.size() - 1,
            "WriteFormatAnsiVararg - formatting failed");
        Severity = ESeverity::Error;
    }

    std::string_view const Message(FormatBuffer.data(), static_cast<size_t>(Result));
    WriteAnsi(Place, Severity, Message);
}

void ILogger::WriteFormatUnicodeVararg(CodePlaceW const& Place, ESeverity Severity,
    std::wstring_view const Format, std::va_list Args)
{
    thread_local std::array<wchar_t, 1024> FormatBuffer{ L'\0' };
    int Result = _vsnwprintf(FormatBuffer.data(), FormatBuffer.size() - 1, Format.data(), Args);

    if (Result < 0)
    {
        MILAN_BREAK_ATTACHED();
        Result = _snwprintf(FormatBuffer.data(), FormatBuffer.size() - 1,
            L"WriteFormatUnicodeVararg - formatting failed");
        Severity = ESeverity::Error;
    }

    std::wstring_view const Message(FormatBuffer.data(), static_cast<size_t>(Result));
    WriteUnicode(Place, Severity, Message);
}
