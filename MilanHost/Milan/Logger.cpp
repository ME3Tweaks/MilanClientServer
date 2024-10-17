#include <algorithm>
#include <array>

#include <locale.h>

#include "Containers/string.hpp"
#include "Milan/Logger.hpp"


// ! Shared internal logic.
// ========================================

bool s_sinkLocaleInitialized = false;
_locale_t s_sinkLocale = nullptr;

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
    char const* const Data, size_t const Size, float const Seconds, FormatStyleA const& Style)
{
    std::fprintf(Style.OutStream, "[%07.3f] [%s] %s\n", Seconds, Style.SevToken, Data);
    MILAN_UNUSED_3(Place, Severity, Size);
}

void BasicConsoleLoggerOutput::AcceptUnicode(CodePlaceW const& Place, ESeverity const Severity,
    wchar_t const* const Data, size_t const Size, float const Seconds, FormatStyleW const& Style)
{
    std::fwprintf(Style.OutStream, L"[%07.3f] [%s] %s\n", Seconds, Style.SevToken, Data);
    MILAN_UNUSED_3(Place, Severity, Size);
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
    char const* const Data, size_t const Size, float const Seconds, FormatStyleA const& Style)
{
    _fprintf_l(Style.OutStream, "%s[%07.3f] [%s] %s%s\n", s_sinkLocale,
        Style.OpenSeq, Seconds, Style.SevToken, Data, Style.CloseSeq);
    MILAN_UNUSED_3(Place, Severity, Size);
}

void ConsoleLoggerOutput::AcceptUnicode(CodePlaceW const& Place, ESeverity const Severity,
    wchar_t const* const Data, size_t const Size, float const Seconds, FormatStyleW const& Style)
{
    _fwprintf_l(Style.OutStream, L"%s[%07.3f] [%s] %s%s\n", s_sinkLocale,
        Style.OpenSeq, Seconds, Style.SevToken, Data, Style.CloseSeq);
    MILAN_UNUSED_3(Place, Severity, Size);
}

void ConsoleLoggerOutput::Flush()
{
    std::fflush(stdout);
    std::fflush(stderr);
}


// ! FileLoggerOutput implementation.
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
    char const* const Data, size_t const Size, float const Seconds, FormatStyleA const& Style)
{
    if (Data != nullptr && Size != 0)
    {
        assert(Size < 8192);

        static std::array<wchar_t, 4096>    UnicodeBuffer{ 0u };
        std::lock_guard<std::mutex>         AnsiConvLock{ AnsiConvMutex };

        std::fill(UnicodeBuffer.begin(), UnicodeBuffer.end(), wchar_t(0u));
        [[maybe_unused]] int const UnicodeLength = ::MultiByteToWideChar(CP_ACP, 0u,
            Data, static_cast<int>(Size + 1), UnicodeBuffer.data(), static_cast<int>(UnicodeBuffer.size()));

        assert(UnicodeLength > 0 && UnicodeLength < UnicodeBuffer.size());

        // Can't use the style here, but this should be a corner case anyway...
        auto const Token = GetFormatStyleShortW(Severity).SevToken;

        _fwprintf_l(OutputFile, L"[%07.3f] [%s] %s\n", s_sinkLocale, Seconds, Token, UnicodeBuffer.data());
    }

    MILAN_UNUSED_2(Place, Style);
}

void FileLoggerOutput::AcceptUnicode(CodePlaceW const& Place, ESeverity const Severity,
    wchar_t const* const Data, size_t const Size, float const Seconds, FormatStyleW const& Style)
{
    _fwprintf_l(OutputFile, L"[%07.3f] [%s] %s\n", s_sinkLocale, Seconds, Style.SevToken, Data);
    MILAN_UNUSED_4(Place, Severity, Size, Seconds);
}

void FileLoggerOutput::Flush()
{
    if (OutputFile != nullptr)
        std::fflush(OutputFile);
}


// ! MilanLogger implementation.
// ========================================

MilanLogger::MilanLogger()
{
    Sinks.reserve(8);

    s_sinkLocale = _get_current_locale();
    s_sinkLocaleInitialized = true;
}

MilanLogger::~MilanLogger() noexcept
{
    // It should clean up Sinks automatically...
}

void MilanLogger::WriteAnsi(CodePlaceA const& Place, ESeverity const Severity, std::string_view const Message)
{
    auto const Seconds = MessageTimer.GetSeconds();
    std::lock_guard<std::mutex> _(WriteMutex);

    for (SinkPointer& Sink : Sinks)
    {
        auto const& Style = GetFormatStyleShortA(Severity);
        Sink->AcceptAnsi(Place, Severity, Message.data(), Message.size(), Seconds, Style);
    }
}

void MilanLogger::WriteUnicode(CodePlaceW const& Place, ESeverity const Severity, std::wstring_view const Message)
{
    auto const Seconds = MessageTimer.GetSeconds();
    std::lock_guard<std::mutex> _(WriteMutex);

    for (SinkPointer& Sink : Sinks)
    {
        auto const& Style = GetFormatStyleShortW(Severity);
        Sink->AcceptUnicode(Place, Severity, Message.data(), Message.size(), Seconds, Style);
    }
}

void MilanLogger::Flush()
{
    for (auto& Sink : Sinks)
        Sink->Flush();
}

void MilanLogger::AddSink(ILoggerOutput* const Output)
{
    MILAN_CHECKW(Output != nullptr, L"");
    std::lock_guard<std::mutex> _(WriteMutex);
    Sinks.emplace_back(std::unique_ptr<ILoggerOutput>(Output));
}


// ! AsyncMilanLogger implementation.
// ========================================

AsyncMilanLogger::AsyncMilanLogger(std::size_t const InBufferSize)
    : MilanLogger{}
{
    MILAN_CHECKW(InBufferSize > 0, L"");
    BufferCapacity = InBufferSize;
    BufferCompletion = 0u;

    WriteBuffer = new (std::nothrow) unsigned char[BufferCapacity];
    ProcessBuffer = new (std::nothrow) unsigned char[BufferCapacity];

    MILAN_CHECKW(WriteBuffer != nullptr, L"");
    MILAN_CHECKW(ProcessBuffer != nullptr, L"");
}

AsyncMilanLogger::~AsyncMilanLogger() noexcept
{
    delete[] WriteBuffer;
    delete[] ProcessBuffer;
}

template<typename CodePlaceType, typename ViewType>
    requires (std::same_as<ViewType, std::string_view> || std::same_as<ViewType, std::wstring_view>)
void AsyncMilanLogger::WriteInternal(CodePlaceType const& Place, ESeverity const Severity, ViewType const Message)
{
    std::unique_lock<std::mutex> Lock{ WriteMutex };
    std::size_t const RecordLength = sizeof(RecordHeader) + (Message.length() + 1) * sizeof(ViewType::value_type);

    if (Message.length() >= 0x7FFF) [[unlikely]]
    {
        // Discard the message if it is too long for record header...
        MILAN_BREAK_ATTACHED();
        return;
    }

    if (BufferCompletion + RecordLength >= BufferCapacity)  [[unlikely]]
    {
        // Force current buffer out to have space for the message...
        FlushInternal(Lock);
    }

    auto* const HeaderAllocation = reinterpret_cast<RecordHeader*>(WriteBuffer + BufferCompletion);
    auto* const ContentAllocation = reinterpret_cast<void*>(WriteBuffer + BufferCompletion + sizeof(RecordHeader));

    HeaderAllocation->WriteSeconds = MessageTimer.GetSeconds();
    HeaderAllocation->Severity = static_cast<std::int32_t>(Severity);
    HeaderAllocation->LocationLine = static_cast<std::int32_t>(Place.Line);
    HeaderAllocation->LocationFile = reinterpret_cast<void const*>(Place.File);
    HeaderAllocation->ContentLength = static_cast<std::uint16_t>(Message.length());
    HeaderAllocation->bWideChars = static_cast<std::uint16_t>(sizeof(ViewType::value_type) > 1u);

    auto* const ContentTermination = std::copy(Message.begin(), Message.end(), (ViewType::pointer)ContentAllocation);
    *ContentTermination = L'\0';

    // We want to check this, but don't want to touch global logging system from within.
    assert((char*)ContentTermination + sizeof(ViewType::value_type) - (char*)HeaderAllocation == (std::ptrdiff_t)RecordLength);

    BufferCompletion += RecordLength;
    BufferCondvar.notify_all();

    if (Severity >= ESeverity::Warn) [[unlikely]]
    {
        // Flush all queued messages on error.
        FlushInternal(Lock);
    }
}

void AsyncMilanLogger::WriteAnsi(CodePlaceA const& Place, ESeverity const Severity, std::string_view const Message)
{
    WriteInternal<CodePlaceA, std::string_view>(Place, Severity, Message);
}

void AsyncMilanLogger::WriteUnicode(CodePlaceW const& Place, ESeverity const Severity, std::wstring_view const Message)
{
    WriteInternal<CodePlaceW, std::wstring_view>(Place, Severity, Message);
}

void AsyncMilanLogger::Flush()
{
    std::unique_lock Lock{ WriteMutex };
    FlushInternal(Lock);
}

void AsyncMilanLogger::Initialize()
{
    // Prevent repeated initialization.
    if (bInitialized.test_and_set())
    {
        MILAN_BREAK_ATTACHED();
        return;
    }

    bWaitingForExit.clear();
    bWaitingForFlush.clear();

    ProcessThread = std::thread(&AsyncMilanLogger::ProcessThreadInternal, this);
}

void AsyncMilanLogger::Cleanup() noexcept
{
    Flush();

    bInitialized.clear();
    bWaitingForExit.test_and_set();

    BufferCondvar.notify_all();

    if (ProcessThread.joinable())
        ProcessThread.join();
}

void AsyncMilanLogger::AddSink(ILoggerOutput* const Output)
{
    MILAN_CHECKW(!bInitialized.test(), L"can't add sinks to initialized logger");
    MilanLogger::AddSink(Output);
}

void AsyncMilanLogger::ProcessThreadInternal()
{
    while (!bWaitingForExit.test())
    {
        std::size_t ProcessCompletion = 0;

        {
            std::unique_lock Lock{ WriteMutex };
            BufferCondvar.wait(Lock, [this]() { return BufferCompletion != 0 || bWaitingForExit.test() || bWaitingForFlush.test(); });
            std::swap(BufferCompletion, ProcessCompletion);
            std::swap(WriteBuffer, ProcessBuffer);
        }

        std::size_t Position = 0;
        while (Position < ProcessCompletion)
        {
            auto* const HeaderPointer = reinterpret_cast<RecordHeader*>(ProcessBuffer + Position);
            auto* const ContentsPointer = reinterpret_cast<void*>(ProcessBuffer + Position + sizeof(RecordHeader));

            if (!!HeaderPointer->bWideChars)
            {
                CodePlaceW const Location{ (wchar_t const*)nullptr, (wchar_t const*)HeaderPointer->LocationFile, HeaderPointer->LocationLine };
                std::wstring_view const Contents{ (wchar_t const*)(ContentsPointer), static_cast<std::size_t>(HeaderPointer->ContentLength) };
                FormatStyleW const& Style = GetFormatStyleShortW(static_cast<ESeverity>(HeaderPointer->Severity));

                std::ranges::for_each(Sinks, [&](SinkPointer& Sink)
                    {
                        Sink->AcceptUnicode(Location, static_cast<ESeverity>(HeaderPointer->Severity),
                            Contents.data(), Contents.size(), HeaderPointer->WriteSeconds, Style);
                    });

                Position += sizeof RecordHeader + (static_cast<std::size_t>(HeaderPointer->ContentLength) + 1) * sizeof(wchar_t);
            }
            else
            {
                CodePlaceA const Location{ (char const*)nullptr, (char const*)HeaderPointer->LocationFile, HeaderPointer->LocationLine };
                std::string_view const Contents{ (char const*)(ContentsPointer), static_cast<std::size_t>(HeaderPointer->ContentLength) };
                FormatStyleA const& Style = GetFormatStyleShortA(static_cast<ESeverity>(HeaderPointer->Severity));

                std::ranges::for_each(Sinks, [&](SinkPointer& Sink)
                    {
                        Sink->AcceptAnsi(Location, static_cast<ESeverity>(HeaderPointer->Severity),
                            Contents.data(), Contents.size(), HeaderPointer->WriteSeconds, Style);
                    });

                Position += sizeof RecordHeader + (static_cast<std::size_t>(HeaderPointer->ContentLength) + 1) * sizeof(char);
            }

        }

        if (bWaitingForFlush.test())
        {
            //std::unique_lock Lock{ WriteMutex };
            bWaitingForFlush.clear();
            FlushCondvar.notify_all();
        }
    }
}

void AsyncMilanLogger::FlushInternal(std::unique_lock<std::mutex>& Lock)
{
    bWaitingForFlush.test_and_set();
    BufferCondvar.notify_all();

    if (bInitialized.test())
    {
        // Checking for initialization here prevents deadlocks in outer initialization code.
        FlushCondvar.wait(Lock, [this]() { return !bWaitingForFlush.test(); });
    }

    std::ranges::for_each(Sinks, [](SinkPointer& Sink) { Sink->Flush(); });
}
