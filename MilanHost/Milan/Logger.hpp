/**
 * @file        MilanHost/Milan/Logger.hpp
 * @header      Logger implementations.
 */

#pragma once

#include <cstdio>
#include <mutex>

#include "Containers/vector.hpp"
#include "Milan/Base.hpp"
#include "Milan/Misc.hpp"


// ! Output devices.
// ========================================

class BasicConsoleLoggerOutput final : public ILoggerOutput
{
public:

    BasicConsoleLoggerOutput();
    ~BasicConsoleLoggerOutput() noexcept override;
    void AcceptAnsi(CodePlaceA const& Place, ESeverity Severity, char const* Data, size_t Size, float Seconds) override;
    void AcceptUnicode(CodePlaceW const& Place, ESeverity Severity, wchar_t const* Data, size_t Size, float Seconds) override;
    void Flush() override;
};

class ConsoleLoggerOutput final : public ILoggerOutput
{
public:

    ConsoleLoggerOutput();
    ~ConsoleLoggerOutput() noexcept override;
    void AcceptAnsi(CodePlaceA const& Place, ESeverity Severity, char const* Data, size_t Size, float Seconds) override;
    void AcceptUnicode(CodePlaceW const& Place, ESeverity Severity, wchar_t const* Data, size_t Size, float Seconds) override;
    void Flush() override;
};

class FileLoggerOutput final : public ILoggerOutput
{
    std::FILE* OutputFile{ nullptr };
    std::mutex AnsiConvMutex;

public:

    bool Initialized() const noexcept;

    FileLoggerOutput(std::string_view OutputPath, bool bAppend);
    ~FileLoggerOutput() noexcept override;
    void AcceptAnsi(CodePlaceA const& Place, ESeverity Severity, char const* Data, size_t Size, float Seconds) override;
    void AcceptUnicode(CodePlaceW const& Place, ESeverity Severity, wchar_t const* Data, size_t Size, float Seconds) override;
    void Flush() override;
};


// ! Logger implementations.
// ========================================

/**
 * @brief
 * The ILogger implementation for MilanHost main logger.
 */
class MilanLogger final : public ILogger, public NonCopyable
{
    // ILoggerOutput specifies these will allocate via GMalloc.
    using SinkPointer = std::unique_ptr<ILoggerOutput>;

    game::vector<SinkPointer>   Sinks{};
    std::mutex                  WriteMutex;
    RelativeTimer               MessageTimer{};

public:

    MilanLogger();

    ~MilanLogger() noexcept override;
    void Flush() override;

    void WriteAnsi(CodePlaceA const& Place, ESeverity Severity, std::string_view Message) override;
    void WriteUnicode(CodePlaceW const& Place, ESeverity Severity, std::wstring_view Message) override;

    void RegisterOutput(ILoggerOutput* Output);
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
