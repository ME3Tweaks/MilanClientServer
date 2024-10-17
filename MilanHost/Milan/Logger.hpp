/**
 * @file        MilanHost/Milan/Logger.hpp
 * @header      Logger implementations.
 */

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>
#include <tuple>

#include "Containers/vector.hpp"
#include "Milan/Base.hpp"
#include "Milan/Misc.hpp"


struct FormatStyleA;
struct FormatStyleW;


/**
 * @brief
 * Interface for a type that persists pre-formatted logging output,
 * for example by printing it to console or writing to a file.
 *
 * @details
 * Both @c Accept* methods expect @c Size argument to exclude null termination!
 */
class ILoggerOutput : public AllocateThroughEngine
{
public:

    virtual ~ILoggerOutput() noexcept {}

    virtual void AcceptAnsi(CodePlaceA const& Place, ESeverity Severity, char const* Data, size_t Size, float Seconds, FormatStyleA const& Style) = 0;
    virtual void AcceptUnicode(CodePlaceW const& Place, ESeverity Severity, wchar_t const* Data, size_t Size, float Seconds, FormatStyleW const& Style) = 0;

    virtual void Flush() = 0;
};


// ! Output devices.
// ========================================

class BasicConsoleLoggerOutput final : public ILoggerOutput
{
public:

    BasicConsoleLoggerOutput();
    ~BasicConsoleLoggerOutput() noexcept override;
    void AcceptAnsi(CodePlaceA const& Place, ESeverity Severity, char const* Data, size_t Size, float Seconds, FormatStyleA const& Style) override;
    void AcceptUnicode(CodePlaceW const& Place, ESeverity Severity, wchar_t const* Data, size_t Size, float Seconds, FormatStyleW const& Style) override;
    void Flush() override;
};

class ConsoleLoggerOutput final : public ILoggerOutput
{
public:

    ConsoleLoggerOutput();
    ~ConsoleLoggerOutput() noexcept override;
    void AcceptAnsi(CodePlaceA const& Place, ESeverity Severity, char const* Data, size_t Size, float Seconds, FormatStyleA const& Style) override;
    void AcceptUnicode(CodePlaceW const& Place, ESeverity Severity, wchar_t const* Data, size_t Size, float Seconds, FormatStyleW const& Style) override;
    void Flush() override;
};

class FileLoggerOutput final : public ILoggerOutput
{
public:

    bool Initialized() const noexcept;

    FileLoggerOutput(std::string_view OutputPath, bool bAppend);
    ~FileLoggerOutput() noexcept override;
    void AcceptAnsi(CodePlaceA const& Place, ESeverity Severity, char const* Data, size_t Size, float Seconds, FormatStyleA const& Style) override;
    void AcceptUnicode(CodePlaceW const& Place, ESeverity Severity, wchar_t const* Data, size_t Size, float Seconds, FormatStyleW const& Style) override;
    void Flush() override;

private:

    std::FILE* OutputFile{ nullptr };
    std::mutex AnsiConvMutex;
};


// ! Logger implementations.
// ========================================

/**
 * @brief
 * Primary logger implementation.
 */
class MilanLogger : public ILogger, public NonCopyable
{
public:

    MilanLogger();

    ~MilanLogger() noexcept override;
    void WriteAnsi(CodePlaceA const& Place, ESeverity Severity, std::string_view Message) override;
    void WriteUnicode(CodePlaceW const& Place, ESeverity Severity, std::wstring_view Message) override;
    void Flush() override;

    virtual void Initialize() {}
    virtual void Cleanup() noexcept {}
    virtual void AddSink(ILoggerOutput* Output);

protected:

    // ILoggerOutput specifies these will allocate via GMalloc.
    using SinkPointer = std::unique_ptr<ILoggerOutput>;

    game::vector<SinkPointer>   Sinks{};                    /* Registered output devices. */
    std::mutex                  WriteMutex;                 /* Synchronization primitive for protecting writes. */
    RelativeTimer               MessageTimer{};             /* Timer used for stamping log messages. */
};


/**
 * @brief
 * Primary logger implementation which dispatches logging
 * events for processing by configurable sinks on a separate thread.
 */
class AsyncMilanLogger final : public MilanLogger
{
public:

    static constexpr std::size_t k_defaultBuffer = 10 * 1024 * 1024;
    explicit AsyncMilanLogger(std::size_t InBufferSize = k_defaultBuffer);

    // ILogger implementation:

    ~AsyncMilanLogger() noexcept override;
    void WriteAnsi(CodePlaceA const& Place, ESeverity Severity, std::string_view Message) override;
    void WriteUnicode(CodePlaceW const& Place, ESeverity Severity, std::wstring_view Message) override;
    void Flush() override;

    // MilanLogger implementation:

    void Initialize() override;
    void Cleanup() noexcept override;
    void AddSink(ILoggerOutput* Output) override;

private:

    template<typename CodePlaceType, typename ViewType>
        requires (std::same_as<ViewType, std::string_view> || std::same_as<ViewType, std::wstring_view>)
    void WriteInternal(CodePlaceType const& Place, ESeverity Severity, ViewType Message);

    void ProcessThreadInternal();
    void FlushInternal(std::unique_lock<std::mutex>& Lock);

#pragma pack(push, 1)
    struct RecordHeader final
    {
        float                   WriteSeconds;
        std::int32_t            Severity        : 8;
        std::int32_t            LocationLine    : 24;
        void const*             LocationFile;
        std::uint16_t           ContentLength   : 15;
        std::uint16_t           bWideChars      : 1;
    };
#pragma pack(pop)

    std::atomic_flag            bInitialized{};             /* Flag set when processing loop has started. */
    std::atomic_flag            bWaitingForExit{};          /* Flag set when processing thread should terminate. */
    std::atomic_flag            bWaitingForFlush{};         /* Flag set when a flush is requested. */

    std::condition_variable     FlushCondvar;               /* Condition variable used by callers to wait for a record flush. */
    std::condition_variable     BufferCondvar;              /* Condition variable used by processing thread to wait for new entries. */
    std::thread                 ProcessThread;              /* Handle to the processing thread. */

    unsigned char*              WriteBuffer{ nullptr };     /* Buffer to which new log entries are written. */
    unsigned char*              ProcessBuffer{ nullptr };   /* Buffer from which processing loop picks records. */

    std::size_t                 BufferCapacity{ 0 };        /* Number of bytes allocated for each buffer. */
    std::size_t                 BufferCompletion{ 0 };      /* Number of bytes used up by write buffer. */
};


/**
 * @brief
 * An ILogger proxy that filters messages based on their severity.
 *
 * @todo
 * Use bitmask for filtering severity to reduce hotpath.
 */
class FilterLoggerProxy : public ILogger
{
    template<typename T>
    struct RangePair final
    {
        T Min{}, Max{};
    };

    ILogger* Inner{};
    RangePair<ESeverity> Accepted{};

public:

    ~FilterLoggerProxy() noexcept override {}

    void Flush() override
    {
        Inner->Flush();
    }

    FilterLoggerProxy(ILogger* const pInner)
        : FilterLoggerProxy{ pInner, ESeverity::Trace, ESeverity::Error }
    {
        // ...
    }

    FilterLoggerProxy(ILogger* const pInner, ESeverity const InMin, ESeverity InMax)
        : Inner{ pInner }, Accepted{ .Min = InMin, .Max = InMax }
    {
        assert(pInner != nullptr);
        assert(InMin >= ESeverity::Trace && InMin <= ESeverity::Error);
        assert(InMax >= ESeverity::Trace && InMax <= ESeverity::Error);
        assert(InMin < InMax && "can't heave empty or reverse filter range");
    }

    void WriteAnsi(CodePlaceA const& Place, ESeverity const Severity, std::string_view const Message) override
    {
        if (Severity >= Accepted.Min && Severity <= Accepted.Max)
            Inner->WriteAnsi(Place, Severity, Message);
    }

    void WriteUnicode(CodePlaceW const& Place, ESeverity const Severity, std::wstring_view const Message) override
    {
        if (Severity >= Accepted.Min && Severity <= Accepted.Max)
            Inner->WriteUnicode(Place, Severity, Message);
    }
};


/**
 * @brief
 * An ILogger implementation that does nothing.
 */
class NopLogger final : public ILogger
{
public:

    ~NopLogger() noexcept override {}
    void Flush() override {}
    void WriteAnsi(CodePlaceA const&, ESeverity, std::string_view) override {}
    void WriteUnicode(CodePlaceW const&, ESeverity, std::wstring_view) override {}
};
