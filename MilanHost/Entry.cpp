#include <fstream>

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
#include "Systems/Imgui.hpp"
#include "Systems/Output.hpp"
#include "Systems/Profilers.hpp"
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
    ::Milan::Profilers::Initialize(Context);
    ::Milan::Imgui::Initialize(Context);
    ::Milan::State::Initialize(Context);


    // Register miscellaneous commands.
    // ----------------------------------------

    ::Milan::Console::RegisterCommand(L"dbg", L"dumpnatives",
        [](wchar_t const*)
        {
            MILAN_INFOW(L"dumping native ufuncs");
            ::RelativeTimer Timer{};

            std::wofstream Stream{ L"MilanHost-natives.csv" };
            Stream << L"FullName;RVA;Address\n" << std::hex;

            auto const Base = reinterpret_cast<std::ptrdiff_t>(::LESDK::GetMainModuleBase());

            for (TypedObjectIterator<UFunction, false, false> Iter{}; Iter; ++Iter)
            {
                // TODO: Port select FUNC flags.
                if (((*Iter)->FunctionFlags & 0x400) == 0)
                    continue;

                auto const Address = reinterpret_cast<std::ptrdiff_t>((*Iter)->Func);
                Stream << *(*Iter)->StaticFullName() << L";0x" << Address - Base << L";0x" << Address << L"\n";
            }

            MILAN_INFOW(L"dumping native ufuncs - done in %.3f ms.", Timer.GetMilliseconds());
            return true;
        });

    ::Milan::Console::RegisterCommand(L"dbg", L"dumpsfxuid",
        [](wchar_t const*)
        {
            MILAN_INFOW(L"dumping SFXUniqueID map");
            ::RelativeTimer Timer{};

            USFXEngine* const Engine = (*GEngine)->Cast<USFXEngine>();
            MILAN_CHECKW(Engine != nullptr, L"");

            auto const LookupAddress = (std::intptr_t)::LESDK::GetMainModuleBase() + 0x1CBC7F8;
            auto const LookupPtr = (TMap<INT32, INT32>*)LookupAddress;

            for (auto const [Hash, Index] : *LookupPtr)
            {
                wchar_t const* StringValue = L"n/a";
                if (Index >= 0 && Index < (INT32)Engine->SFXUniqueIDs.Count())
                    StringValue = *Engine->SFXUniqueIDs(Index);

                MILAN_INFOW(L" Hash = %d, Index = %d  =>  Value = %s", Hash, Index, StringValue);
            }

            MILAN_INFOW(L"dumping SFXUniqueID map - done in %.3f ms.", Timer.GetMilliseconds());
            return true;
        });


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
    ::Milan::Imgui::Terminate();
    ::Milan::Profilers::Terminate();
    ::Milan::Console::Terminate();
    ::Milan::Globals::Terminate();
    ::Milan::Output::Terminate();
    ::Milan::Allocator::Terminate();

    return true;
}
