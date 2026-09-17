#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <cwchar>

#include "Base/Base.hpp"
#include "Components/COutput.hpp"
#include "Engine/Hooks.hpp"


bool                        gb_disableSafeBreak = false;
ILogger*                    gp_mainLogger = nullptr;
ComponentContainer*         gp_componentContainer = nullptr;


#define _CG_LOGCAT(a) SFXName MILAN_CONCAT(ELOG_, a){};
    #include "Base/LoggingCategories.inl"
#undef _CG_LOGCAT


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

void ILogger::WriteFormatAnsi(CodePlaceA const& Place, ESeverity const Severity,
    SFXName const Category, std::string_view const Format, ...)
{
    std::va_list Args;
    va_start(Args, Format);
    WriteFormatAnsiVararg(Place, Severity, Category, Format, Args);
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

void ILogger::WriteFormatUnicode(CodePlaceW const& Place, ESeverity const Severity,
    SFXName const Category, std::wstring_view const Format, ...)
{
    std::va_list Args;
    va_start(Args, Format);
    WriteFormatUnicodeVararg(Place, Severity, Category, Format, Args);
    va_end(Args);
}

void ILogger::WriteFormatAnsiVararg(CodePlaceA const& Place, ESeverity Severity,
    std::string_view const Format, std::va_list Args)
{
    WriteFormatAnsiVararg(Place, Severity, ELOG_General, Format, Args);
}

void ILogger::WriteFormatAnsiVararg(CodePlaceA const& Place, ESeverity Severity,
    SFXName const Category, std::string_view const Format, std::va_list Args)
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
    WriteAnsi(Place, Severity, Category, Message);
}

void ILogger::WriteFormatUnicodeVararg(CodePlaceW const& Place, ESeverity const Severity,
    std::wstring_view const Format, std::va_list Args)
{
    WriteFormatUnicodeVararg(Place, Severity, ELOG_General, Format, Args);
}

void ILogger::WriteFormatUnicodeVararg(CodePlaceW const& Place, ESeverity Severity,
    SFXName const Category, std::wstring_view const Format, std::va_list Args)
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
    WriteUnicode(Place, Severity, Category, Message);
}


// ! Fatal error handling.
// ========================================

void Details::OnFatalErrorW(CodePlaceW const& Place,
    wchar_t const* const Expression, wchar_t const* const Format, ...)
{
    if (ILogger* const Logger = gp_mainLogger; Logger != nullptr)
    {
        std::va_list Args;
        va_start(Args, Format);

        bool const bWithExpression = Expression != nullptr && std::wcslen(Expression) > 0;
        Logger->WriteFormatUnicode(Place, ESeverity::Error, ELOG_Assert, L"fatal error: %s", bWithExpression ? Expression : L"???");
        Logger->WriteFormatUnicodeVararg(Place, ESeverity::Error, ELOG_Assert, Format, Args);

        va_end(Args);

        // TODO: Pop up a MessageBox of our own.

        // Flush stuff just to be sure.
        COMPONENT(COutput).FlushEverything();
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

        ::OutputDebugStringW(L"\n");
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
    return !gb_disableSafeBreak && ::IsDebuggerPresent();
}


// ! RelativeTimer implementation.
// ========================================

RelativeTimer::RelativeTimer()
{
    LARGE_INTEGER liFreq{};
    LARGE_INTEGER liStart{};

    QueryPerformanceFrequency(&liFreq);
    QueryPerformanceCounter(&liStart);

    Frequency = liFreq.QuadPart;
    CounterStart = liStart.QuadPart;
}

float RelativeTimer::GetSeconds() const
{
    LARGE_INTEGER liNow{};
    QueryPerformanceCounter(&liNow);
    return static_cast<float>(liNow.QuadPart - CounterStart) / Frequency;
}

float RelativeTimer::GetMilliseconds() const
{
    return GetSeconds() * 1000;
}


// ! Misc utilities.
// ========================================

int GetGameInstanceOrdinal()
{
    static std::atomic_bool s_initialized{ false };
    static int s_ordinal{ -1 };

    if (!s_initialized.exchange(true))
    {
        HANDLE  Mutex = nullptr;
        int     Ordinal = 0;

        while (true)
        {
            auto const Name = FString::Printf(L"MilanHost-%d", Ordinal);
            Mutex = ::CreateMutexW(nullptr, true, *Name);

            if (Mutex != nullptr && ::GetLastError() != ERROR_ALREADY_EXISTS)
            {
                s_ordinal = Ordinal;
                break;
            }

            Mutex = nullptr;
            Ordinal++;
        }

    }

    return s_ordinal;
}
