#include <algorithm>
#include <array>

#include "Containers/string.hpp"
#include "Milan/Logger.hpp"


// ! Shared internal logic.
// ========================================

struct FormatStyleA final
{
    std::FILE*          OutStream;      /** Standard stream to log to, if logging to stdout/stderr. */
    bool                bFlush;         /** Whether logger should flush stream after current line. */
    char const*         SevToken;       /** Textual representation of a severity level. */
    char const*         OpenSeq;        /** Characters to prepend to the message line (escape sequences). */
    char const*         CloseSeq;       /** Characters to append to the message line (escape sequences). */
};

struct FormatStyleW final
{
    std::FILE*          OutStream;      /** Standard stream to log to, if logging to stdout/stderr. */
    bool                bFlush;         /** Whether logger should flush stream after current line. */
    wchar_t const*      SevToken;       /** Textual representation of a severity level. */
    wchar_t const*      OpenSeq;        /** Characters to prepend to the message line (escape sequences). */
    wchar_t const*      CloseSeq;       /** Characters to append to the message line (escape sequences). */
};

FormatStyleA const& GetFormatStyleShortA(ESeverity const Severity) noexcept
{
    static FormatStyleA s_styleTrace    { stdout, false,    "TRC",      "\x1b[1m\x1b[30m", "\x1b[0m" };     // bright black (= darkgray)
    static FormatStyleA s_styleDebug    { stdout, false,    "DBG",      "\x1b[0m", "\x1b[0m" };             // default
    static FormatStyleA s_styleInfo     { stdout, false,    "INF",      "\x1b[1m\x1b[34m", "\x1b[0m" };     // bright blue
    static FormatStyleA s_styleWarn     { stdout, true,     "WRN",      "\x1b[33m", "\x1b[0m" };            // yellow
    static FormatStyleA s_styleError    { stdout, true,     "ERR",      "\x1b[1m\x1b[31m", "\x1b[0m" };     // bright red
    static FormatStyleA s_styleDefault  { stdout, true,     "N/A",      "", "" };

    switch (Severity)
    {
    case k_logTrace:    return s_styleTrace;
    case k_logDebug:    return s_styleDebug;
    case k_logInfo:     return s_styleInfo;
    case k_logWarn:     return s_styleWarn;
    case k_logError:    return s_styleError;
    default:            return s_styleDefault;
    }
}

FormatStyleW const& GetFormatStyleShortW(ESeverity const Severity) noexcept
{
    static FormatStyleW s_styleTrace    { stdout, false,    L"TRC",     L"\x1b[1m\x1b[30m", L"\x1b[0m" };   // bright black (= darkgray)
    static FormatStyleW s_styleDebug    { stdout, false,    L"DBG",     L"\x1b[0m", L"\x1b[0m" };           // default
    static FormatStyleW s_styleInfo     { stdout, false,    L"INF",     L"\x1b[1m\x1b[34m", L"\x1b[0m" };   // bright blue
    static FormatStyleW s_styleWarn     { stdout, true,     L"WRN",     L"\x1b[33m", L"\x1b[0m" };          // yellow
    static FormatStyleW s_styleError    { stdout, true,     L"ERR",     L"\x1b[1m\x1b[31m", L"\x1b[0m" };   // bright red
    static FormatStyleW s_styleDefault  { stdout, true,     L"N/A",     L"", L"" };

    switch (Severity)
    {
    case k_logTrace:    return s_styleTrace;
    case k_logDebug:    return s_styleDebug;
    case k_logInfo:     return s_styleInfo;
    case k_logWarn:     return s_styleWarn;
    case k_logError:    return s_styleError;
    default:            return s_styleDefault;
    }
}


// ! BasicConsoleLoggerOutput implementation.
// ========================================

BasicConsoleLoggerOutput::BasicConsoleLoggerOutput() = default;
BasicConsoleLoggerOutput::~BasicConsoleLoggerOutput() noexcept {}

void BasicConsoleLoggerOutput::AcceptAnsi(CodePlaceA const& Place, ESeverity const Severity,
    char const* const Data, size_t const Size, float const Seconds)
{
    assert(Data != nullptr && Size != 0);

    auto const& [Stream, bFlush, SevToken, OpenSeq, CloseSeq] = GetFormatStyleShortA(Severity);
    std::fprintf(Stream, "[%07.3f] [%s] %s\n", Seconds, SevToken, Data);

    if (Severity >= ESeverity::Warn)
        std::fflush(Stream);

    MILAN_UNUSED_4(Place, Size, OpenSeq, CloseSeq);
}

void BasicConsoleLoggerOutput::AcceptUnicode(CodePlaceW const& Place, ESeverity const Severity,
    wchar_t const* const Data, size_t const Size, float const Seconds)
{
    assert(Data != nullptr && Size != 0);

    auto const& [Stream, bFlush, SevToken, OpenSeq, CloseSeq] = GetFormatStyleShortW(Severity);
    std::fwprintf(Stream, L"[%07.3f] [%s] %s\n", Seconds, SevToken, Data);

    if (Severity >= ESeverity::Warn)
        std::fflush(Stream);

    MILAN_UNUSED_4(Place, Size, OpenSeq, CloseSeq);
}

void BasicConsoleLoggerOutput::Flush()
{
    std::fflush(stdout);
    std::fflush(stderr);
}


// ! ConsoleLoggerOutput implementation.
// ========================================

ConsoleLoggerOutput::ConsoleLoggerOutput() = default;
ConsoleLoggerOutput::~ConsoleLoggerOutput() noexcept {}

void ConsoleLoggerOutput::AcceptAnsi(CodePlaceA const& Place, ESeverity const Severity,
    char const* const Data, size_t const Size, float const Seconds)
{
    assert(Data != nullptr && Size != 0);

    auto const& [Stream, bFlush, SevToken, OpenSeq, CloseSeq] = GetFormatStyleShortA(Severity);
    std::fprintf(Stream, "%s[%07.3f] [%s] %s%s\n", OpenSeq, Seconds, SevToken, Data, CloseSeq);
    if (bFlush) std::fflush(Stream);

    MILAN_UNUSED_2(Place, Size);
}

void ConsoleLoggerOutput::AcceptUnicode(CodePlaceW const& Place, ESeverity const Severity,
    wchar_t const* const Data, size_t const Size, float const Seconds)
{
    assert(Data != nullptr && Size != 0);

    auto const& [Stream, bFlush, SevToken, OpenSeq, CloseSeq] = GetFormatStyleShortW(Severity);
    std::fwprintf(Stream, L"%s[%07.3f] [%s] %s%s\n", OpenSeq, Seconds, SevToken, Data, CloseSeq);
    if (bFlush) std::fflush(Stream);

    MILAN_UNUSED_2(Place, Size);
}

void ConsoleLoggerOutput::Flush()
{
    std::fflush(stdout);
    std::fflush(stderr);
}


// ! FileLoggerOutput impl.
// ========================================

bool FileLoggerOutput::Initialized() const noexcept
{
    return OutputFile != nullptr;
}

FileLoggerOutput::FileLoggerOutput(std::string_view const OutputPath, bool const bAppend)
{
    // Created file writes wide strings as utf-8, but only accepts wide strings.
    OutputFile = fopen(OutputPath.data(), !bAppend ? "w,ccs=UTF-8" : "a,ccs=UTF-8");

    if (OutputFile == nullptr)
    {
        ::game::string ErrorText{};
        ErrorText += "FileLoggerOutput - std::fopen failed: ";
        ErrorText += std::strerror(errno);

        ::OutputDebugStringA(ErrorText.c_str());
        std::fprintf(stderr, "%s\n", ErrorText.c_str());
    }
}

FileLoggerOutput::~FileLoggerOutput() noexcept
{
    if (OutputFile != nullptr)
    {
        std::fflush(OutputFile);
        std::fclose(OutputFile);
    }
}

void FileLoggerOutput::AcceptAnsi(CodePlaceA const& Place, ESeverity const Severity,
    char const* const Data, size_t const Size, float const Seconds)
{
    assert(Data != nullptr && Size != 0);
    assert(OutputFile != nullptr);

    if (Data != nullptr && Size != 0)
    {
        assert(Size < 8192);

        static std::array<wchar_t, 4096>    UnicodeBuffer{ 0u };
        std::lock_guard<std::mutex>         AnsiConvLock{ AnsiConvMutex };

        std::fill(UnicodeBuffer.begin(), UnicodeBuffer.end(), wchar_t(0u));
        [[maybe_unused]] int const UnicodeLength = ::MultiByteToWideChar(CP_ACP, 0u,
            Data, static_cast<int>(Size + 1), UnicodeBuffer.data(), static_cast<int>(UnicodeBuffer.size()));

        assert(UnicodeLength > 0 && UnicodeLength < UnicodeBuffer.size());

        auto const Token = GetFormatStyleShortW(Severity).SevToken;
        std::fwprintf(OutputFile, L"[%07.3f] [%s] %s\n", Seconds, Token, UnicodeBuffer.data());
    }

    if (Severity >= ESeverity::Warn)
        std::fflush(OutputFile);

    MILAN_UNUSED_2(Place, Seconds);
}

void FileLoggerOutput::AcceptUnicode(CodePlaceW const& Place, ESeverity const Severity,
    wchar_t const* const Data, size_t const Size, float const Seconds)
{
    assert(Data != nullptr && Size != 0);
    assert(OutputFile != nullptr);

    auto const Token = GetFormatStyleShortW(Severity).SevToken;
    std::fwprintf(OutputFile, L"[%07.3f] [%s] %s\n", Seconds, Token, Data);

    if (Severity >= ESeverity::Warn)
        std::fflush(OutputFile);

    MILAN_UNUSED_3(Place, Size, Seconds);
}

void FileLoggerOutput::Flush()
{
    if (OutputFile != nullptr)
        std::fflush(OutputFile);
}


// ! MilanLogger impl.
// ========================================

MilanLogger::MilanLogger()
{
    Sinks.reserve(8);
}

MilanLogger::~MilanLogger() noexcept
{
    // It should clean up Sinks automatically...
}

void MilanLogger::Flush()
{
    for (auto& Sink : Sinks)
        Sink->Flush();
}

void MilanLogger::WriteAnsi(CodePlaceA const& Place, ESeverity Severity, std::string_view const Message)
{
    auto const Seconds = MessageTimer.GetSeconds();
    std::lock_guard<std::mutex> _(WriteMutex);

    for (SinkPointer& Sink : Sinks)
    {
        Sink->AcceptAnsi(Place, Severity, Message.data(), Message.size(), Seconds);
    }
}

void MilanLogger::WriteUnicode(CodePlaceW const& Place, ESeverity Severity, std::wstring_view const Message)
{
    auto const Seconds = MessageTimer.GetSeconds();
    std::lock_guard<std::mutex> _(WriteMutex);

    for (SinkPointer& Sink : Sinks)
    {
        Sink->AcceptUnicode(Place, Severity, Message.data(), Message.size(), Seconds);
    }
}

void MilanLogger::RegisterOutput(ILoggerOutput* const Output)
{
    assert(Output != nullptr);
    std::lock_guard<std::mutex> _(WriteMutex);
    Sinks.emplace_back(std::unique_ptr<ILoggerOutput>(Output));
}
