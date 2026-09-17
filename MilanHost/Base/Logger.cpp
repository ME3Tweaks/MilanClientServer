#include <algorithm>
#include <array>
#include <cstring>
#include <cwchar>
#include <limits>
#include <string>
#include <locale.h>

#include "Base/Logger.hpp"
#include "Containers/string.hpp"


// ! Shared internal logic.
// ========================================

bool s_sinkLocaleInitialized = false;
_locale_t s_sinkLocale = nullptr;

struct FormatStyleA final
{
    bool                bFlush;         /** Whether logger should flush stream after current line. */
    char const*         SevToken;       /** Textual representation of a severity level. */
    char const*         OpenSeq;        /** Characters to prepend to the message line (escape sequences). */
    char const*         CloseSeq;       /** Characters to append to the message line (escape sequences). */
};

struct FormatStyleW final
{
    bool                bFlush;         /** Whether logger should flush stream after current line. */
    wchar_t const*      SevToken;       /** Textual representation of a severity level. */
    wchar_t const*      OpenSeq;        /** Characters to prepend to the message line (escape sequences). */
    wchar_t const*      CloseSeq;       /** Characters to append to the message line (escape sequences). */
};

namespace
{
    bool WriteBytes(HANDLE const OutputHandle, char const* const Data, size_t const Size) noexcept
    {
        if (OutputHandle == nullptr || OutputHandle == INVALID_HANDLE_VALUE || (Data == nullptr && Size != 0))
            return false;

        size_t Offset = 0;
        while (Offset < Size)
        {
            DWORD const ChunkSize = static_cast<DWORD>(std::min<size_t>(Size - Offset, 0x7ffff000u));
            DWORD Written = 0;
            if (::WriteFile(OutputHandle, Data + Offset, ChunkSize, &Written, nullptr) == FALSE || Written == 0 || Written > ChunkSize)
                return false;
            Offset += Written;
        }
        return true;
    }

    bool WriteWide(HANDLE const OutputHandle, wchar_t const* const Data, size_t const Size) noexcept
    {
        if (OutputHandle == nullptr || OutputHandle == INVALID_HANDLE_VALUE || (Data == nullptr && Size != 0))
            return false;
        if (Size == 0)
            return true;

        auto WriteUtf8 = [&]() noexcept
        {
            if (Size > static_cast<size_t>(std::numeric_limits<int>::max()))
                return false;

            int const CharacterCount = static_cast<int>(Size);
            int const Utf8Length = ::WideCharToMultiByte(CP_UTF8, 0, Data, CharacterCount, nullptr, 0, nullptr, nullptr);
            if (Utf8Length <= 0)
                return false;

            std::string Utf8(static_cast<size_t>(Utf8Length), '\0');
            if (::WideCharToMultiByte(CP_UTF8, 0, Data, CharacterCount, Utf8.data(), Utf8Length, nullptr, nullptr) <= 0)
                return false;
            return WriteBytes(OutputHandle, Utf8.data(), Utf8.size());
        };

        DWORD ConsoleMode = 0;
        if (::GetConsoleMode(OutputHandle, &ConsoleMode) != FALSE)
        {
            size_t Offset = 0;
            while (Offset < Size)
            {
                DWORD const ChunkSize = static_cast<DWORD>(std::min<size_t>(Size - Offset, std::numeric_limits<DWORD>::max()));
                DWORD Written = 0;
                if (::WriteConsoleW(OutputHandle, Data + Offset, ChunkSize, &Written, nullptr) == FALSE)
                {
                    // A failed WriteConsoleW may have emitted a partial prefix. Do
                    // not replay the line through the UTF-8 path in that case.
                    return Written == 0 && Offset == 0 ? WriteUtf8() : false;
                }
                if (Written == 0 || Written > ChunkSize)
                    return false;
                Offset += Written;
            }
            return true;
        }

        return WriteUtf8();
    }

    std::string FormatAnsiLine(char const* const Open, char const* const Close, char const* const SeverityToken, SFXName const Category,
        char const* const Data, size_t const Size, float const Seconds)
    {
        char Prefix[256]{};
        sprintf_s(Prefix, _countof(Prefix), "[%07.3f] [%s %s] ", Seconds, SeverityToken, Category.GetName());

        std::string Result;
        Result.reserve(std::strlen(Open) + std::strlen(Prefix) + Size + std::strlen(Close) + 1);
        Result += Open;
        Result += Prefix;
        if (Data != nullptr)
            Result.append(Data, Size);
        Result += Close;
        Result.push_back('\n');
        return Result;
    }

    std::wstring FormatUnicodeLine(wchar_t const* const Open, wchar_t const* const Close, wchar_t const* const SeverityToken, SFXName const Category,
        wchar_t const* const Data, size_t const Size, float const Seconds)
    {
        wchar_t Prefix[256]{};
        swprintf_s(Prefix, _countof(Prefix), L"[%07.3f] [%s %S] ", Seconds, SeverityToken, Category.GetName());

        std::wstring Result;
        Result.reserve(std::wcslen(Open) + std::wcslen(Prefix) + Size + std::wcslen(Close) + 1);
        Result += Open;
        Result += Prefix;
        if (Data != nullptr)
            Result.append(Data, Size);
        Result += Close;
        Result.push_back(L'\n');
        return Result;
    }
}

FormatStyleA const& GetFormatStyleShortA(ESeverity const Severity) noexcept
{
    static FormatStyleA s_styleTrace    { false,    "TRC",      "\x1b[1m\x1b[30m", "\x1b[0m" };     // bright black (= darkgray)
    static FormatStyleA s_styleDebug    { false,    "DBG",      "\x1b[0m", "\x1b[0m" };             // default
    static FormatStyleA s_styleInfo     { false,    "INF",      "\x1b[1m\x1b[34m", "\x1b[0m" };     // bright blue
    static FormatStyleA s_styleWarn     { true,     "WRN",      "\x1b[33m", "\x1b[0m" };            // yellow
    static FormatStyleA s_styleError    { true,     "ERR",      "\x1b[1m\x1b[31m", "\x1b[0m" };     // bright red
    static FormatStyleA s_styleDefault  { true,     "N/A",      "", "" };

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
    static FormatStyleW s_styleTrace    { false,    L"TRC",     L"\x1b[1m\x1b[30m", L"\x1b[0m" };   // bright black (= darkgray)
    static FormatStyleW s_styleDebug    { false,    L"DBG",     L"\x1b[0m", L"\x1b[0m" };           // default
    static FormatStyleW s_styleInfo     { false,    L"INF",     L"\x1b[1m\x1b[34m", L"\x1b[0m" };   // bright blue
    static FormatStyleW s_styleWarn     { true,     L"WRN",     L"\x1b[33m", L"\x1b[0m" };          // yellow
    static FormatStyleW s_styleError    { true,     L"ERR",     L"\x1b[1m\x1b[31m", L"\x1b[0m" }; // bright red
    static FormatStyleW s_styleDefault  { true,     L"N/A",     L"", L"" };

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

BasicConsoleLoggerOutput::BasicConsoleLoggerOutput(HANDLE const InOutputHandle)
    : OutputHandle{ InOutputHandle }
{
}
BasicConsoleLoggerOutput::~BasicConsoleLoggerOutput() noexcept {}

void BasicConsoleLoggerOutput::AcceptAnsi(CodePlaceA const& Place, ESeverity const Severity, SFXName const Category,
    char const* const Data, size_t const Size, float const Seconds, FormatStyleA const& Style)
{
    std::string const Line = FormatAnsiLine("", "", Style.SevToken, Category, Data, Size, Seconds);
    WriteBytes(OutputHandle, Line.data(), Line.size());
    MILAN_UNUSED_4(Place, Severity, Category, Size);
}

void BasicConsoleLoggerOutput::AcceptUnicode(CodePlaceW const& Place, ESeverity const Severity, SFXName const Category,
    wchar_t const* const Data, size_t const Size, float const Seconds, FormatStyleW const& Style)
{
    std::wstring const Line = FormatUnicodeLine(L"", L"", Style.SevToken, Category, Data, Size, Seconds);
    WriteWide(OutputHandle, Line.data(), Line.size());
    MILAN_UNUSED_4(Place, Severity, Category, Size);
}

void BasicConsoleLoggerOutput::Flush()
{
    if (OutputHandle != nullptr && OutputHandle != INVALID_HANDLE_VALUE)
        ::FlushFileBuffers(OutputHandle);
}


// ! ConsoleLoggerOutput implementation.
// ========================================

ConsoleLoggerOutput::ConsoleLoggerOutput(HANDLE const InOutputHandle)
    : OutputHandle{ InOutputHandle }
{
}
ConsoleLoggerOutput::~ConsoleLoggerOutput() noexcept {}

void ConsoleLoggerOutput::AcceptAnsi(CodePlaceA const& Place, ESeverity const Severity, SFXName const Category,
    char const* const Data, size_t const Size, float const Seconds, FormatStyleA const& Style)
{
    std::string const Line = FormatAnsiLine(Style.OpenSeq, Style.CloseSeq, Style.SevToken, Category, Data, Size, Seconds);
    WriteBytes(OutputHandle, Line.data(), Line.size());
    MILAN_UNUSED_4(Place, Severity, Category, Size);
}

void ConsoleLoggerOutput::AcceptUnicode(CodePlaceW const& Place, ESeverity const Severity, SFXName const Category,
    wchar_t const* const Data, size_t const Size, float const Seconds, FormatStyleW const& Style)
{
    std::wstring const Line = FormatUnicodeLine(Style.OpenSeq, Style.CloseSeq, Style.SevToken, Category, Data, Size, Seconds);
    WriteWide(OutputHandle, Line.data(), Line.size());
    MILAN_UNUSED_4(Place, Severity, Category, Size);
}

void ConsoleLoggerOutput::Flush()
{
    if (OutputHandle != nullptr && OutputHandle != INVALID_HANDLE_VALUE)
        ::FlushFileBuffers(OutputHandle);
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

void FileLoggerOutput::AcceptAnsi(CodePlaceA const& Place, ESeverity const Severity, SFXName const Category,
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

        _fwprintf_l(OutputFile, L"[%07.3f] [%s %S] %s\n", s_sinkLocale, Seconds, Token, Category.GetName(), UnicodeBuffer.data());
    }

    MILAN_UNUSED_3(Place, Category, Style);
}

void FileLoggerOutput::AcceptUnicode(CodePlaceW const& Place, ESeverity const Severity, SFXName const Category,
    wchar_t const* const Data, size_t const Size, float const Seconds, FormatStyleW const& Style)
{
    _fwprintf_l(OutputFile, L"[%07.3f] [%s %S] %s\n", s_sinkLocale, Seconds, Style.SevToken, Category.GetName(), Data);
    MILAN_UNUSED_5(Place, Severity, Category, Size, Seconds);
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

void MilanLogger::WriteAnsi(CodePlaceA const& Place, ESeverity const Severity, SFXName const Category, std::string_view const Message)
{
    if (!FilterMessage(Severity, Category)) return;

    auto const Seconds = MessageTimer.GetSeconds();
    std::lock_guard<std::mutex> _(WriteMutex);

    for (SinkPointer& Sink : Sinks)
    {
        auto const& Style = GetFormatStyleShortA(Severity);
        Sink->AcceptAnsi(Place, Severity, Category, Message.data(), Message.size(), Seconds, Style);
    }
}

void MilanLogger::WriteUnicode(CodePlaceW const& Place, ESeverity const Severity, SFXName const Category, std::wstring_view const Message)
{
    if (!FilterMessage(Severity, Category)) return;

    auto const Seconds = MessageTimer.GetSeconds();
    std::lock_guard<std::mutex> _(WriteMutex);

    for (SinkPointer& Sink : Sinks)
    {
        auto const& Style = GetFormatStyleShortW(Severity);
        Sink->AcceptUnicode(Place, Severity, Category, Message.data(), Message.size(), Seconds, Style);
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
void AsyncMilanLogger::WriteInternal(CodePlaceType const& Place, ESeverity const Severity, SFXName const Category, ViewType const Message)
{
    if (!FilterMessage(Severity, Category)) return;

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
    HeaderAllocation->Category = Category;
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

void AsyncMilanLogger::WriteAnsi(CodePlaceA const& Place, ESeverity const Severity, SFXName const Category, std::string_view const Message)
{
    WriteInternal<CodePlaceA, std::string_view>(Place, Severity, Category, Message);
}

void AsyncMilanLogger::WriteUnicode(CodePlaceW const& Place, ESeverity const Severity, SFXName const Category, std::wstring_view const Message)
{
    WriteInternal<CodePlaceW, std::wstring_view>(Place, Severity, Category, Message);
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
    // Cleanup may be reached through more than one shutdown path. Once the
    // worker has been joined there is no processing thread left to satisfy a
    // flush request, so make the operation idempotent before calling Flush().
    if (!ProcessThread.joinable())
        return;

    Flush();

    bInitialized.clear();

    {
        // Must hold WriteMutex while setting the flag and notifying, otherwise the
        // processing thread's BufferCondvar.wait() predicate check can race with this
        // update and miss the notification entirely, hanging shutdown forever.
        std::unique_lock Lock{ WriteMutex };
        bWaitingForExit.test_and_set();
        BufferCondvar.notify_all();
    }

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
                            HeaderPointer->Category, Contents.data(), Contents.size(), HeaderPointer->WriteSeconds, Style);
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
                            HeaderPointer->Category, Contents.data(), Contents.size(), HeaderPointer->WriteSeconds, Style);
                    });

                Position += sizeof RecordHeader + (static_cast<std::size_t>(HeaderPointer->ContentLength) + 1) * sizeof(char);
            }
        }

        if (bWaitingForFlush.test())
        {
            // Must hold WriteMutex here: FlushInternal() waits on FlushCondvar with the
            // predicate `!bWaitingForFlush.test()` checked under this same lock. Clearing
            // the flag and notifying without holding it can race with the waiter's
            // check-then-block sequence and lose the wakeup, hanging the caller forever.
            std::unique_lock Lock{ WriteMutex };
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
