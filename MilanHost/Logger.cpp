#include <algorithm>
#include <array>

#include "Containers/string.hpp"
#include "Logger.hpp"


// ! Windows console colors.
// ! Taken from: https://stackoverflow.com/a/49929936
//========================================

enum FG_COLORS : WORD
{
    FG_BLACK = 0,
    FG_BLUE = 1,
    FG_GREEN = 2,
    FG_CYAN = 3,
    FG_RED = 4,
    FG_MAGENTA = 5,
    FG_BROWN = 6,
    FG_LIGHTGRAY = 7,
    FG_GRAY = 8,
    FG_LIGHTBLUE = 9,
    FG_LIGHTGREEN = 10,
    FG_LIGHTCYAN = 11,
    FG_LIGHTRED = 12,
    FG_LIGHTMAGENTA = 13,
    FG_YELLOW = 14,
    FG_WHITE = 15
} FG_COLORS;

enum BG_COLORS : WORD
{
    BG_NAVYBLUE = 16,
    BG_GREEN = 32,
    BG_TEAL = 48,
    BG_MAROON = 64,
    BG_PURPLE = 80,
    BG_OLIVE = 96,
    BG_SILVER = 112,
    BG_GRAY = 128,
    BG_BLUE = 144,
    BG_LIME = 160,
    BG_CYAN = 176,
    BG_RED = 192,
    BG_MAGENTA = 208,
    BG_YELLOW = 224,
    BG_WHITE = 240
} BG_COLORS;


// ! Shared internal logic.
// ========================================

char const* FormatSeverityAnsi(ESeverity const Severity)
{
    switch (Severity)
    {
    case ESeverity::Trace:  return "TRC";
    case ESeverity::Debug:  return "DBG";
    case ESeverity::Info:   return "INF";
    case ESeverity::Warn:   return "WRN";
    case ESeverity::Error:  return "ERR";
    };

    return "???";
}

wchar_t const* FormatSeverityUnicode(ESeverity const Severity)
{
    switch (Severity)
    {
    case ESeverity::Trace:  return L"TRC";
    case ESeverity::Debug:  return L"DBG";
    case ESeverity::Info:   return L"INF";
    case ESeverity::Warn:   return L"WRN";
    case ESeverity::Error:  return L"ERR";
    };

    return L"???";
}

unsigned short MapSeverityToColor(ESeverity const Severity)
{
    switch (Severity)
    {
    case ESeverity::Trace:  return FG_GRAY;
    case ESeverity::Debug:  return FG_LIGHTGRAY;
    case ESeverity::Info:   return FG_CYAN;
    case ESeverity::Warn:   return FG_YELLOW;
    case ESeverity::Error:  return FG_RED;
    };

    return FG_LIGHTGRAY;
}

bool EnableVirtualTerminalMode(bool const bStandardError)
{
    DWORD const HandleIndex = bStandardError ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE;

    HANDLE const StandardOutput = ::GetStdHandle(HandleIndex);
    if (StandardOutput == INVALID_HANDLE_VALUE)
        return false;

    DWORD CurrentMode = 0u;
    if (!::GetConsoleMode(StandardOutput, &CurrentMode))
        return false;

    CurrentMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (!::SetConsoleMode(StandardOutput, CurrentMode))
        return false;

    return true;
}


// ! ConsoleLoggerOutput impl.
// ========================================

HANDLE ConsoleLoggerOutput::InitHandle(DWORD const Target)
{
    HANDLE const Handle = ::GetStdHandle(Target);

    if (Handle == INVALID_HANDLE_VALUE)
    {
#ifndef _FINAL_RELEASE
        std::array<char, 64> ErrorBuffer{ '\0' };
        _snprintf(ErrorBuffer.data(), ErrorBuffer.size(),
            "GetStdHandle(%d) error = %d\n", Target, ::GetLastError());
        ::OutputDebugString(ErrorBuffer.data());
        MILAN_BREAK_ATTACHED();
#endif
    }

    return Handle;
}

unsigned short ConsoleLoggerOutput::ExchangeConsoleColor(unsigned short const Attributes, bool const bUseError)
{
    WORD PreviousAttributes = FG_WHITE;
    CONSOLE_SCREEN_BUFFER_INFO ConsoleBufferInfo{};
    HANDLE const Handle = !bUseError ? OutputHandle : ErrorHandle;

    if (::GetConsoleScreenBufferInfo(Handle, &ConsoleBufferInfo))
        PreviousAttributes = ConsoleBufferInfo.wAttributes;

    [[maybe_unused]] BOOL const Result = ::SetConsoleTextAttribute(Handle, Attributes);
    assert(Result == TRUE && "SetConsoleTextAttribute failed");

    return PreviousAttributes;
}

ConsoleLoggerOutput::ConsoleLoggerOutput()
{
    OutputHandle = InitHandle(STD_OUTPUT_HANDLE);
    ErrorHandle = InitHandle(STD_ERROR_HANDLE);
}

ConsoleLoggerOutput::~ConsoleLoggerOutput() noexcept {}

void ConsoleLoggerOutput::AcceptAnsi(ESeverity const Severity,
    char const* const Data, [[maybe_unused]] size_t const Size)
{
    assert(Data != nullptr && Size != 0);

    FILE* Stream = stdout;
    bool bUseError = false;
    if (Severity == ESeverity::Error)
    {
        Stream = stderr;
        bUseError = true;
    }

    auto const Attributes = ExchangeConsoleColor(MapSeverityToColor(Severity), bUseError);

    std::fprintf(Stream, "%s\n", Data);
    if (Severity == ESeverity::Error)
        std::fflush(Stream);

    ExchangeConsoleColor(Attributes, bUseError);
}

void ConsoleLoggerOutput::AcceptUnicode(ESeverity const Severity,
    wchar_t const* const Data, [[maybe_unused]] size_t const Size)
{
    assert(Data != nullptr && Size != 0);

    FILE* Stream = stdout;
    bool bUseError = false;
    if (Severity == ESeverity::Error)
    {
        Stream = stderr;
        bUseError = true;
    }

    auto const Attributes = ExchangeConsoleColor(MapSeverityToColor(Severity), bUseError);

    std::fwprintf(Stream, L"%s\n", Data);
    if (Severity == ESeverity::Error)
        std::fflush(Stream);

    ExchangeConsoleColor(Attributes, bUseError);
}

void ConsoleLoggerOutput::Flush(bool const bError)
{
    MILAN_UNUSED(bError);
    std::fflush(stdout);
    std::fflush(stderr);
}


// ! FileLoggerOutput impl.
// ========================================

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

bool FileLoggerOutput::Initialized() const noexcept
{
    return OutputFile != nullptr;
}

FileLoggerOutput::~FileLoggerOutput() noexcept
{
    if (OutputFile != nullptr)
    {
        std::fflush(OutputFile);
        std::fclose(OutputFile);
    }
}

void FileLoggerOutput::AcceptAnsi(ESeverity const Severity,
    char const* const Data, size_t const Size)
{
    assert(Data != nullptr && Size != 0);
    assert(OutputFile != nullptr);

    if (Data != nullptr && Size != 0)
    {
        assert(Size < 8192);
        thread_local std::array<wchar_t, 8192> UnicodeBuffer{ L'\0' };

        [[maybe_unused]] int const UnicodeLength = ::MultiByteToWideChar(CP_ACP, 0u,
            Data, static_cast<int>(Size + 1), UnicodeBuffer.data(), static_cast<int>(UnicodeBuffer.size()));

        assert(UnicodeLength > 0 && UnicodeLength < UnicodeBuffer.size());
        std::fwprintf(OutputFile, L"%s\n", UnicodeBuffer.data());
    }

    if (Severity == ESeverity::Error)
        std::fflush(OutputFile);
}

void FileLoggerOutput::AcceptUnicode(ESeverity const Severity,
    wchar_t const* const Data, [[maybe_unused]] size_t const Size)
{
    assert(Data != nullptr && Size != 0);
    assert(OutputFile != nullptr);

    std::fwprintf(OutputFile, L"%s\n", Data);
    if (Severity == ESeverity::Error)
        std::fflush(OutputFile);
}

void FileLoggerOutput::Flush(bool const bError)
{
    MILAN_UNUSED(bError);
    assert(OutputFile != nullptr);
    std::fflush(OutputFile);
}


// ! MilanLogger impl.
// ========================================

MilanLogger::MilanLogger()
{
    Sinks.reserve(8);
}

void MilanLogger::ForceFlush()
{
    for (auto& [Sink, Tag] : Sinks)
    {
        MILAN_UNUSED(Tag);
        Sink->Flush(true);
    }
}

MilanLogger::~MilanLogger() noexcept
{
    // It should clean up Sinks automatically...
}

void MilanLogger::WriteAnsi(CodePlaceA const& Place,
    ESeverity const Severity, std::string_view const Message)
{
    std::array<char, 4096> FormatBuffer{ '\0' };

#ifndef _FINAL_RELEASE
    int const Result = _snprintf(FormatBuffer.data(), FormatBuffer.size(), "[%.3f] [%s] %s  [%s:%d]",
        MessageTimer.GetSeconds(), FormatSeverityAnsi(Severity), Message.data(), Place.File, Place.Line);
#else
    int const Result = _snprintf(FormatBuffer.data(), FormatBuffer.size(), "[%.3f] [%s] %s",
        MessageTimer.GetSeconds(), FormatSeverityAnsi(Severity), Message.data());
    MILAN_UNUSED(Place);
#endif

    assert(Result > 0);

    std::lock_guard<std::mutex> _(WriteMutex);
    for (auto& [Sink, Tag] : Sinks)
    {
        MILAN_UNUSED(Tag);
        Sink->AcceptAnsi(Severity, FormatBuffer.data(), static_cast<size_t>(Result));
    }
}

void MilanLogger::WriteUnicode(CodePlaceW const& Place,
    ESeverity const Severity, std::wstring_view const Message)
{
    std::array<wchar_t, 2048> FormatBuffer{ '\0' };

#ifndef _FINAL_RELEASE
    int const Result = _snwprintf(FormatBuffer.data(), FormatBuffer.size(), L"[%.3f] [%s] %s  [%s:%d]",
        MessageTimer.GetSeconds(), FormatSeverityUnicode(Severity), Message.data(), Place.File, Place.Line);
#else
    int const Result = _snwprintf(FormatBuffer.data(), FormatBuffer.size(), L"[%.3f] [%s] %s",
        MessageTimer.GetSeconds(), FormatSeverityUnicode(Severity), Message.data());
    MILAN_UNUSED(Place);
#endif

    assert(Result > 0);

    std::lock_guard<std::mutex> _(WriteMutex);
    for (auto& [Sink, Tag] : Sinks)
    {
        MILAN_UNUSED(Tag);
        Sink->AcceptUnicode(Severity, FormatBuffer.data(), static_cast<size_t>(Result));
    }
}

void MilanLogger::RegisterOutput(int const Tag, ILoggerOutput* const Output)
{
    assert(Output != nullptr);
    std::lock_guard<std::mutex> _(WriteMutex);
    Sinks.emplace_back(std::unique_ptr<ILoggerOutput>(Output), Tag);
}

void MilanLogger::UnregisterOutput(int const Tag)
{
    std::lock_guard<std::mutex> _(WriteMutex);
    std::erase_if(Sinks, [Tag](auto& Pair) { return Pair.second == Tag; });
}
