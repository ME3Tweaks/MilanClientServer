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


// ! Logger implementation.
// ========================================

class MilanLogger final : public ILogger, public NonCopyable
{
    // These unique_ptrs will allocate through GMalloc because ILoggerOutput specifies that.
    using SinkVector = game::vector<std::pair<std::unique_ptr<ILoggerOutput>, int>>;

    SinkVector      Sinks{};
    std::mutex      WriteMutex;
    RelativeTimer   MessageTimer{};

public:

    MilanLogger();

    ~MilanLogger() noexcept override;
    void WriteAnsi(CodePlaceA const& Place, ESeverity Severity, std::string_view Message) override;
    void WriteUnicode(CodePlaceW const& Place, ESeverity Severity, std::wstring_view Message) override;

    void RegisterOutput(int Tag, ILoggerOutput* Output);
    void UnregisterOutput(int Tag);
};
