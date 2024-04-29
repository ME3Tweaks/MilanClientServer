/**
 * @file        MilanHost/Systems/Output.hpp
 * @header      Subsystem for logging and other debug output.
 */

#pragma once


class ILogger;
namespace LESDK { class Initializer; }

namespace Milan::Output
{
    void Initialize(::LESDK::Initializer& Context);
    void Terminate();

    ILogger& GetMainLogger();
    void FlushEverything();
}
