#include <array>
#include <cstdio>
#include <cwchar>

#include "Milan/Base.hpp"
#include "Milan/Hooks.hpp"
#include "Systems/Output.hpp"


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
    thread_local std::array<char, 4096> FormatBuffer{ '\0' };
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
    thread_local std::array<wchar_t, 4096> FormatBuffer{ L'\0' };
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


// ! Fatal error handling.
// ========================================

void Details::OnFatalErrorW(CodePlaceW const& Place,
    wchar_t const* const Expression, wchar_t const* const Format, ...)
{
    if (::Milan::Output::HasInitialized())
    {
        auto& Logger = ::Milan::Output::GetMainLogger();

        std::va_list Args;
        va_start(Args, Format);

        bool const bWithExpression = Expression != nullptr && std::wcslen(Expression) > 0;
        Logger.WriteFormatUnicode(Place, ESeverity::Error, L"fatal error: %s", bWithExpression ? Expression : L"???");
        Logger.WriteFormatUnicodeVararg(Place, ESeverity::Error, Format, Args);

        va_end(Args);

        // TODO: Pop up a MessageBox of our own.

        // Flush stuff just to be sure.
        ::Milan::Output::FlushEverything();
    }
    else
    {
        static std::array<wchar_t, 4096> AuxiliaryBuffer{ 0u };
        signed int Offset = _snwprintf(AuxiliaryBuffer.data(), AuxiliaryBuffer.size(),
            L"fatal error: %s\nlocation: %s:%d\nfunction: %s\n", Expression, Place.File, Place.Line, Place.Func);

        std::va_list Args;
        va_start(Args, Format);

        if (Offset < 0) Offset = 0;
        _vsnwprintf(AuxiliaryBuffer.data() + Offset, static_cast<size_t>(AuxiliaryBuffer.size() - Offset), Format, Args);

        va_end(Args);

        ::OutputDebugStringW(AuxiliaryBuffer.data());

        std::fwprintf(stderr, L"%s\n", AuxiliaryBuffer.data());
        std::fflush(stderr);
    }

    if (BioRequestExit_orig != nullptr)
    {
        // If available, call the engine's termination routine.
        // BioWare seem to have added some logic of their own there.
        BioRequestExit_hook(TRUE, 0, 0);
        MILAN_UNREACHABLE;
    }
    else
    {
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        std::abort();
    }
}

bool Details::ShouldBreak() noexcept
{
    return !bDisableSafeBreak && ::IsDebuggerPresent();
}

bool Details::bDisableSafeBreak = false;
