#include <array>
#include <atomic>
#include <fstream>
#include <unordered_set>

#include <LESDK/Init.hpp>
#include <SPI.h>

#include "Entry.hpp"
#include "Milan/Base.hpp"
#include "Milan/ClientConfig.hpp"
#include "Milan/GuidCacheUtils.hpp"
#include "Milan/Logger.hpp"
#include "Milan/Misc.hpp"
#include "Proxies/Core.hpp"
#include "Proxies/Engine.hpp"
#include "Proxies/SFXGame.hpp"
#include "Systems/Console.hpp"
#include "Systems/Globals.hpp"
#include "Systems/Imgui.hpp"
#include "Systems/Output.hpp"
#include "Systems/Profilers.hpp"
#include "Systems/Renderdoc.hpp"
#include "Systems/State.hpp"


SPI_PLUGINSIDE_SUPPORT(L"MilanHost", L"Project Milan Team", L"0.1.0", SPI_GAME_LE3, SPI_VERSION_ANY);
SPI_PLUGINSIDE_POSTLOAD;
SPI_PLUGINSIDE_SEQATTACH;

SPI_IMPLEMENT_ATTACH
{
    ::RelativeTimer AttachTimer{};
    ::LESDK::Initializer Context{ InterfacePtr, "MilanHost" };


    // Disable safe breaking.
    // ----------------------------------------

    // Can't use FString here because global allocator has not yet been initialized.
    ::Details::bDisableSafeBreak = ::wcsstr(::Milan::Globals::GetCmdArgs(), L"-nobreak") != nullptr;


    // Initialize subsystems.
    // ----------------------------------------

    ::Milan::Allocator::Initialize(Context);
    ::Milan::Output::Initialize(Context);
    ::Milan::Globals::Initialize(Context);
    ::Milan::Console::Initialize(Context);
    ::Milan::Profilers::Initialize(Context);
    ::Milan::Imgui::Initialize(Context);
    ::Milan::State::Initialize(Context);

#ifndef _FINAL_RELEASE
    ::Milan::Renderdoc::Initialize(Context);
#endif

    // This needs to run after globals.
    ::Milan::Output::DeferredInitialize(Context);


    // Run additional startup logic.
    // ----------------------------------------

    RegisterNonshipCommands();
    RegisterVectoredExceptions();
    SpawnLocalClients();


    // Load client-side config.
    // ----------------------------------------

    static constexpr wchar_t const* k_configFilename = L"./MilanHost.toml";

    ClientConfig    Config{};
    FString         ConfigError{};

    if (!Config.TryRead(k_configFilename, ConfigError)) [[unlikely]]
    {
        MILAN_ERRORW(L"failed to load config '%s': %s", k_configFilename, * ConfigError);
        MILAN_WARNW(L"default configuration will be used, which is probably not what you want");
        Config = ClientConfig{};
    }


    MILAN_INFOW(L"attach completed in %.3f ms.", AttachTimer.GetMilliseconds());
    MILAN_INFOW(L"ordinal instance = %d", ::GetGameInstanceOrdinal());

    ::Milan::Output::FlushEverything();

    return true;
}

SPI_IMPLEMENT_DETACH
{
    MILAN_UNUSED(InterfacePtr);
    SingleDetachImpl();
    return true;
}


// ! Local functions.
// ========================================

void SingleDetachImpl()
{
    static std::atomic_bool s_detachCalled{ false };
    if (s_detachCalled.exchange(true))
        return;

    MILAN_INFOW(L"detach starting");
    ::Milan::Output::FlushEverything();


    // Terminate subsystems in reverse.
    // ----------------------------------------

#ifndef _FINAL_RELEASE
    ::Milan::Renderdoc::Terminate();
#endif

    ::Milan::State::Terminate();
    ::Milan::Imgui::Terminate();
    ::Milan::Profilers::Terminate();
    ::Milan::Console::Terminate();
    ::Milan::Globals::Terminate();
    ::Milan::Output::Terminate();
    ::Milan::Allocator::Terminate();
}

void RegisterNonshipCommands()
{
#ifndef _FINAL_RELEASE
    ::Milan::Console::RegisterCommand(L"dbg", L"assertme",
        [](wchar_t const*)
        {
            MILAN_ERRORW(L"ASSERTING THIS PLANE");  // what
            MILAN_CHECKW(4 % 2 == 1, L"some assertion message");
            return true;
        });

    ::Milan::Console::RegisterCommand(L"dbg", L"breakme",
        [](wchar_t const*)
        {
            MILAN_ERRORW(L"BREAKING THIS PLANE");
            MILAN_BREAK_ATTACHED();
            return true;
        });

    ::Milan::Console::RegisterCommand(L"dbg", L"crashme",
        [](wchar_t const*)
        {
            MILAN_ERRORW(L"CRASHING THIS PLANE");
            ::RaiseException(1, 0, 0, nullptr);
            return true;
        });

    ::Milan::Console::RegisterCommand(L"dump", L"guidcaches",
        [](wchar_t const*)
        {
            MILAN_INFOW(L"dumping guid caches (to log !!!)");
            int Index = 0;
            for (TypedObjectIterator<UGuidCache, true, false> Iter{}; Iter; ++Iter)
            {
                auto* const Cache = reinterpret_cast<UGuidCache_Proxy*>(*Iter);
                MILAN_INFOW(L"UGuidCache n. %d: filename = %s", Index, *Cache->Filename);
                for (auto const& [Name, Guid] : Cache->PackageGuids)
                {
                    MILAN_INFOW(L"  %s => %s", *Name.ToString(), *FormatGuid(Guid));
                }
                Index++;
            }
            return true;
        });

    ::Milan::Console::RegisterCommand(L"dump", L"nativeops",
        [](wchar_t const*)
        {
            MILAN_INFOW(L"dumping native ops");
            ::RelativeTimer Timer{};

            std::wofstream Stream{ L"MilanHost-nativeops.csv" };
            Stream << L"Index;RVA;Address\n";

            auto const Base = reinterpret_cast<std::ptrdiff_t>(::LESDK::GetMainModuleBase());

            for (int Index = 0; Index < 0x400; ++Index)
            {
                auto const Address = reinterpret_cast<std::ptrdiff_t>(GNatives[Index]);
                Stream << std::dec << Index << std::hex << L";0x" << Address - Base << L";0x" << Address << L"\n";
            }

            MILAN_INFOW(L"dumping native ops - done in %.3f ms.", Timer.GetMilliseconds());
            return true;
        });

    ::Milan::Console::RegisterCommand(L"dump", L"nativefuncs",
        [](wchar_t const*)
        {
            MILAN_INFOW(L"dumping native ufuncs");
            ::RelativeTimer Timer{};

            std::wofstream Stream{ L"MilanHost-nativefuncs.csv" };
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

    ::Milan::Console::RegisterCommand(L"dump", L"sfxuid",
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

    ::Milan::Console::RegisterCommand(L"dump", L"replistcalc",
        [](wchar_t const*)
        {
            MILAN_INFOW(L"dumping replist calculation addresses");
            ::RelativeTimer Timer{};

            std::wofstream Stream{ L"MilanHost-replist.csv" };
            Stream << L"FullName;RVA;Address\n" << std::hex;

            auto const Base = reinterpret_cast<std::ptrdiff_t>(::LESDK::GetMainModuleBase());

            std::unordered_set<UClass*> DumpedClasses{};
            DumpedClasses.reserve(512);

            for (TypedObjectIterator<AActor, true, true> Iter{}; Iter; ++Iter)
            {
                AActor* const Actor = *Iter;
                UClass* const Class = Actor->Class;

                if (Class == nullptr)
                {
                    MILAN_WARNW(L"Actor %s (%p) has NULL class", *Actor->GetName(), (void*)Actor);
                    continue;
                }

                bool const bNewClass = DumpedClasses.insert(Class).second;
                if (!bNewClass)
                {
                    // We already dumped this class...
                    continue;
                }

                auto const VirtTablePtr = *(void***)Actor;
                auto const VirtMethodPtr = (std::ptrdiff_t)(VirtTablePtr[0x368 / 8]);

                Stream << *Actor->StaticFullName() << L";0x" << VirtMethodPtr - Base << L";0x" << VirtMethodPtr << L"\n";
            }

            MILAN_INFOW(L"dumping replist calculation addresses - done in %.3f ms.", Timer.GetMilliseconds());
            return true;
        });

    ::Milan::Console::RegisterCommand(L"dump", L"onlinevers",
        [](wchar_t const*)
        {
            for (TypedObjectIterator<UOnlineSubsystem, true, false> Iter{}; Iter; ++Iter)
            {
                auto const Subsystem = *Iter;
                auto const Name = Subsystem->GetName();
                MILAN_INFOW(L"%s.GetBuildUniqueId() => 0x%08x", *Name, (unsigned)Subsystem->GetBuildUniqueId());
                MILAN_INFOW(L"%s.GetBioStaticBuildVersion() => 0x%08x", *Name, (unsigned)Subsystem->GetBioStaticBuildVersion());
                MILAN_INFOW(L"%s.GetBioDynamicBuildVersion() => 0x%08x", *Name, (unsigned)Subsystem->GetBioDynamicBuildVersion());
            }
            return true;
        });

#if defined(MILAN_BUILD_LOGINTERNAL) && MILAN_BUILD_LOGINTERNAL

    ::Milan::Console::RegisterCommand(L"log", L"loginternal",
        [](wchar_t const*)
        {
            bool const Enabled = ::Milan::Output::ToggleLogInternal();
            MILAN_INFOW(L"loginternal: %s", Enabled ? L"enabled" : L"disabled");
            return true;
        });

    ::Milan::Console::RegisterCommand(L"log", L"warninternal",
        [](wchar_t const*)
        {
            bool const Enabled = ::Milan::Output::ToggleWarnInternal();
            MILAN_INFOW(L"warninternal: %s", Enabled ? L"enabled" : L"disabled");
            return true;
        });

#endif

    ::Milan::Console::RegisterCommand(L"log", L"processinternal",
        [](wchar_t const*)
        {
            gb_logProcessInternal = !gb_logProcessInternal;
            MILAN_INFOW(L"logging of uobject::processinternal: %s",
                gb_logProcessInternal ? L"enabled" : L"disabled");
            return true;
        });

    ::Milan::Console::RegisterCommand(L"log", L"lookuppackageguids",
        [](wchar_t const*)
        {
            gb_logLookupPackageGuid = !gb_logLookupPackageGuid;
            MILAN_INFOW(L"logging of guid cache package lookups: %s",
                gb_logLookupPackageGuid ? L"enabled" : L"disabled");
            return true;
        });

    ::Milan::Console::RegisterCommand(L"log", L"pendingpackages",
        [](wchar_t const*)
        {
            gb_logPendingPackages = !gb_logPendingPackages;
            MILAN_INFOW(L"logging of net pending packages: %s",
                gb_logPendingPackages ? L"enabled" : L"disabled");
            return true;
        });

    ::Milan::Console::RegisterCommand(L"util", L"updateguidcacheoverride",
        [](wchar_t const*)
        {
            if (gp_guidCacheOverride)
                gp_guidCacheOverride->UpdateLookup();
            return true;
        });

#endif
}

void RegisterVectoredExceptions()
{
    // Ignore these exception codes.
    static const std::array<DWORD, 2> CommonCodes{ 0xE06D7363, 0x406D1388 };

    [[maybe_unused]]
    auto const Handle = AddVectoredExceptionHandler(1,
        [](PEXCEPTION_POINTERS const Exceptions) -> LONG
        {
            auto const Address = Exceptions->ExceptionRecord->ExceptionAddress;
            auto const Code = Exceptions->ExceptionRecord->ExceptionCode;

            bool bDispatchHandler = true;

            if (std::find(CommonCodes.begin(), CommonCodes.end(), Code) != CommonCodes.end())
                bDispatchHandler = false;
            else if (Code >= DBG_REPLY_LATER && Code <= DBG_PRINTEXCEPTION_WIDE_C)
                bDispatchHandler = false;

            if (bDispatchHandler)
            {
                // Actual handlers must be filtered, so call them within this branch.
                ::Milan::Profilers::On_VectoredException(Address, Code, Exceptions);
                ::Milan::Output::On_VectoredException(Address, Code, Exceptions);
                ::Milan::Renderdoc::On_VectoredException(Address, Code, Exceptions);
            }

            return EXCEPTION_CONTINUE_SEARCH;
        });
}

void SpawnLocalClients()
{
#ifndef _FINAL_RELEASE

    int const Instance = ::GetGameInstanceOrdinal();

    FString const Command{ ::Milan::Globals::GetCmdString() };
    FString const Arguments{ ::Milan::Globals::GetCmdArgs() };

    if (Instance == 0)
    {
        int Count = 0;

        if (Arguments.Contains(L"-spawnone")) Count = 1;
        else if (Arguments.Contains(L"-spawntwo")) Count = 2;
        else if (Arguments.Contains(L"-spawnthree")) Count = 3;

        if (Count == 0)
            return;

        std::array<wchar_t, 512> WorkingDirectory{ L'\0' };
        DWORD const WorkingDirectoryReturn = ::GetCurrentDirectoryW((DWORD)WorkingDirectory.size() - 1, WorkingDirectory.data());
        MILAN_CHECKW(WorkingDirectoryReturn > 0, L"failed to get cwd, last error = %d", ::GetLastError());
        MILAN_UNUSED(WorkingDirectoryReturn);

        std::array<wchar_t, 512> ExecutableFilenameBuffer{ L'\0' };
        std::copy_n(Command.Chars(), Command.Length() - Arguments.Length(), ExecutableFilenameBuffer.begin());

        wchar_t* EndCursor = ExecutableFilenameBuffer.data() + ::wcslen(ExecutableFilenameBuffer.data());
        while (EndCursor != ExecutableFilenameBuffer.data() && (*EndCursor == L'\"' || *EndCursor == L'\0'))
        {
            *EndCursor = L'\0';
            EndCursor--;
        }

        wchar_t* ExecutableFilename = ExecutableFilenameBuffer.data();
        while (ExecutableFilename != EndCursor && *ExecutableFilename != L'\0' && (*ExecutableFilename == L'\"'))
            ExecutableFilename++;

        MILAN_DEBUGW(L"spawning %d additional client(s)", Count);
        MILAN_TRACEW(L"command arguments: %s", *Arguments);
        MILAN_TRACEW(L"working directory: %s", WorkingDirectory.data());
        MILAN_TRACEW(L"executable filename: %s", ExecutableFilename);

        for (int i = 0; i < Count; ++i)
        {
            STARTUPINFOW StartupInfo{};
            PROCESS_INFORMATION ProcessInfo{};

            std::memset(&StartupInfo, 0, sizeof StartupInfo);
            std::memset(&ProcessInfo, 0, sizeof ProcessInfo);
            StartupInfo.cb = sizeof StartupInfo;

            auto const Ret = CreateProcessW(
                ExecutableFilename, (wchar_t*)*Arguments,
                nullptr, nullptr, false, 0u, nullptr,
                WorkingDirectory.data(), &StartupInfo, &ProcessInfo);

            if (Ret != 0)
            {
                MILAN_TRACEW(L"spawned additional client %d / %d, pid = %d", i + 1, Count, ProcessInfo.dwProcessId);
                ::CloseHandle(ProcessInfo.hProcess);
                ::CloseHandle(ProcessInfo.hThread);
            }
            else
            {
                MILAN_ERRORW(L"failed to spawn additional client %d / %d", i + 1, Count);
                MILAN_ERRORW(L"  last error code = %d", ::GetLastError());
                break;
            }
        }
    }

#endif
}
