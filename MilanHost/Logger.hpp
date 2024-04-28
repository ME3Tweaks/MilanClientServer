/**
 * @file        MilanHost/Logger.hpp
 * @header      Logger implementations.
 */

#pragma once

#include <cstdio>
#include "Base.hpp"


// ! Output devices.
// ========================================

class ConsoleLoggerOutput final : public ILoggerOutput
{
public:

    ~ConsoleLoggerOutput() noexcept override;
    void AcceptAnsi(ESeverity Severity, char const* Data, size_t Size) override;
    void AcceptUnicode(ESeverity Severity, wchar_t const* Data, size_t Size) override;
    void Flush(bool bError) override;
};

class FileLoggerOutput final : public ILoggerOutput
{
    std::FILE* OutputFile{ nullptr };

public:

    FileLoggerOutput(std::string_view OutputPath);
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

};
