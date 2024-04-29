//#include <LESDK/Headers.hpp>
#include <LESDK/Init.hpp>
#include <SPI.h>

#include "Base.hpp"
#include "Entry.hpp"
#include "Logger.hpp"
#include "Systems/Builtins.hpp"
#include "Systems/Console.hpp"
#include "Systems/Output.hpp"
#include "Systems/Panels.hpp"


SPI_PLUGINSIDE_SUPPORT(L"MilanHost", L"Project Milan Team", L"0.1.0", SPI_GAME_LE3, SPI_VERSION_ANY);
SPI_PLUGINSIDE_POSTLOAD;
SPI_PLUGINSIDE_SEQATTACH;

SPI_IMPLEMENT_ATTACH
{
    ::LESDK::Initializer Context{ InterfacePtr, "MilanHost" };


    // Initialize subsystems.
    // ----------------------------------------

    ::Milan::Allocator::Initialize(Context);
    ::Milan::Output::Initialize(Context);
    ::Milan::Builtins::Initialize(Context);
    ::Milan::Panels::Initialize(Context);


    MILAN_WARNW(L"attach completed");
    ::Milan::Output::FlushEverything();

    return true;
}

SPI_IMPLEMENT_DETACH
{
    MILAN_UNUSED(InterfacePtr);

    // Logging calls in system termination will not be flushed here.
    ::Milan::Output::FlushEverything();


    // Terminate subsystems in reverse.
    // ----------------------------------------

    ::Milan::Panels::Terminate();
    ::Milan::Builtins::Terminate();
    ::Milan::Output::Terminate();
    ::Milan::Allocator::Terminate();

    return true;
}
