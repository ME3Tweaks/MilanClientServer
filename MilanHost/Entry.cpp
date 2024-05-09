//#include <LESDK/Headers.hpp>
#include <LESDK/Init.hpp>
#include <SPI.h>

#include "Base.hpp"
#include "Entry.hpp"
#include "Logger.hpp"
#include "Misc.hpp"
#include "Proxies/Core.hpp"
#include "Proxies/Engine.hpp"
#include "Proxies/SFXGame.hpp"
#include "Systems/Console.hpp"
#include "Systems/Globals.hpp"
#include "Systems/Output.hpp"
#include "Systems/Panels.hpp"
#include "Systems/State.hpp"


SPI_PLUGINSIDE_SUPPORT(L"MilanHost", L"Project Milan Team", L"0.1.0", SPI_GAME_LE3, SPI_VERSION_ANY);
SPI_PLUGINSIDE_POSTLOAD;
SPI_PLUGINSIDE_SEQATTACH;

SPI_IMPLEMENT_ATTACH
{
    ::RelativeTimer AttachTimer{};
    ::LESDK::Initializer Context{ InterfacePtr, "MilanHost" };


    // Initialize subsystems.
    // ----------------------------------------

    ::Milan::Allocator::Initialize(Context);
    ::Milan::Output::Initialize(Context);
    ::Milan::Globals::Initialize(Context);
    ::Milan::Console::Initialize(Context);
    ::Milan::Panels::Initialize(Context);
    ::Milan::State::Initialize(Context);


    MILAN_INFOW(L"attach completed in %.3f ms.", AttachTimer.GetMilliseconds());
    ::Milan::Output::FlushEverything();

    return true;
}

SPI_IMPLEMENT_DETACH
{
    MILAN_UNUSED(InterfacePtr);
    MILAN_INFOW(L"detach starting");

    // Logging calls in system termination will not be flushed here.
    ::Milan::Output::FlushEverything();


    // Terminate subsystems in reverse.
    // ----------------------------------------

    ::Milan::State::Terminate();
    ::Milan::Panels::Terminate();
    ::Milan::Console::Terminate();
    ::Milan::Globals::Terminate();
    ::Milan::Output::Terminate();
    ::Milan::Allocator::Terminate();

    return true;
}
