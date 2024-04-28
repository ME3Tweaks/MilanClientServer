#include <string>
#include "Logger.hpp"


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

inline void WriteAnsiToStandard(std::FILE* const Stream,
    ESeverity const Severity, char const* const Data, size_t const Size)
{
    std::fwrite(Data, sizeof(char), Size, Stream);
    std::fwrite("\r\n", sizeof(char), 2, Stream);

    if (Severity == ESeverity::Error)
        std::fflush(Stream);
}

inline void WriteUnicodeToStandard(std::FILE* const Stream,
    ESeverity const Severity, wchar_t const* const Data, size_t const Size)
{
    std::fwrite(Data, sizeof(wchar_t), Size, Stream);
    std::fwrite(L"\r\n", sizeof(wchar_t), 2, Stream);

    if (Severity == ESeverity::Error)
        std::fflush(Stream);
}


// ! ConsoleLoggerOutput impl.
// ========================================

ConsoleLoggerOutput::~ConsoleLoggerOutput() noexcept {}

void ConsoleLoggerOutput::AcceptAnsi(ESeverity const Severity,
    char const* const Data, size_t const Size)
{
    assert(Data != nullptr && Size != 0);
    auto const Stream = Severity < ESeverity::Error ? stdout : stderr;
    WriteAnsiToStandard(Stream, Severity, Data, Size);
}

void ConsoleLoggerOutput::AcceptUnicode(ESeverity const Severity,
    wchar_t const* const Data, size_t const Size)
{
    assert(Data != nullptr && Size != 0);
    auto const Stream = Severity < ESeverity::Error ? stdout : stderr;
    WriteUnicodeToStandard(Stream, Severity, Data, Size);
}

void ConsoleLoggerOutput::Flush(bool const bError)
{
    MILAN_UNUSED(bError);
    std::fflush(stdout);
    std::fflush(stderr);
}


// ! FileLoggerOutput impl.
// ========================================

FileLoggerOutput::FileLoggerOutput(std::string_view const OutputPath)
{
    OutputFile = std::fopen(OutputPath.data(), "wb");
    if (OutputFile == nullptr)
    {
        std::string ErrorText{};
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
    WriteAnsiToStandard(OutputFile, Severity, Data, Size);
}

void FileLoggerOutput::AcceptUnicode(ESeverity const Severity,
    wchar_t const* const Data, size_t const Size)
{
    assert(Data != nullptr && Size != 0);
    assert(OutputFile != nullptr);
    WriteUnicodeToStandard(OutputFile, Severity, Data, Size);
}

void FileLoggerOutput::Flush(bool const bError)
{
    MILAN_UNUSED(bError);
    assert(OutputFile != nullptr);
    std::fflush(OutputFile);
}
