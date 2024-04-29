/**
 * @file        MilanHost/Logger.hpp
 * @header      Logger implementations.
 */

#pragma once

#include <cstdio>
#include <mutex>

#include "Containers/vector.hpp"

#include "Base.hpp"
#include "Misc.hpp"


// ! Output devices.
// ========================================

class ConsoleLoggerOutput final : public ILoggerOutput
{
public:

    ConsoleLoggerOutput();

    ~ConsoleLoggerOutput() noexcept override;
    void AcceptAnsi(ESeverity Severity, char const* Data, size_t Size) override;
    void AcceptUnicode(ESeverity Severity, wchar_t const* Data, size_t Size) override;
    void Flush(bool bError) override;
};

class FileLoggerOutput final : public ILoggerOutput
{
    std::FILE* OutputFile{ nullptr };

public:

    FileLoggerOutput(std::string_view OutputPath, bool bAppend);
    bool Initialized() const noexcept;

    ~FileLoggerOutput() noexcept override;
    void AcceptAnsi(ESeverity Severity, char const* Data, size_t Size) override;
    void AcceptUnicode(ESeverity Severity, wchar_t const* Data, size_t Size) override;
    void Flush(bool bError) override;
};


// ! Logger implementations.
// ========================================

/**
 * @brief
 * The ILogger implementation for MilanHost main logger.
 */
class MilanLogger final : public ILogger, public NonCopyable
{
    // These unique_ptrs will allocate through GMalloc because ILoggerOutput specifies that.
    using SinkVector = game::vector<std::pair<std::unique_ptr<ILoggerOutput>, int>>;

    SinkVector      Sinks{};
    std::mutex      WriteMutex;
    RelativeTimer   MessageTimer{};

public:

    MilanLogger();
    void ForceFlush();

    ~MilanLogger() noexcept override;
    void WriteAnsi(CodePlaceA const& Place, ESeverity Severity, std::string_view Message) override;
    void WriteUnicode(CodePlaceW const& Place, ESeverity Severity, std::wstring_view Message) override;

    void RegisterOutput(int Tag, ILoggerOutput* Output);
    void UnregisterOutput(int Tag);
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
    void WriteAnsi(CodePlaceA const&, ESeverity, std::string_view) override {}
    void WriteUnicode(CodePlaceW const&, ESeverity, std::wstring_view) override {}
};
