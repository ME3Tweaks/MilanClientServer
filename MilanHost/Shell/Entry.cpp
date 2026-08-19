#include <array>
#include <atomic>
#include <fstream>
#include <unordered_set>

#include <LESDK/Headers.hpp>

#include "Shell/Entry.hpp"

#include "Base/Base.hpp"
#include "Base/Logger.hpp"
#include "Components/CCommands.hpp"
#include "Components/CGlobals.hpp"
#ifdef WITH_IMGUI
#include "Components/CImgui.hpp"
#endif
#include "Components/CNetworkProfiler.hpp"
#include "Components/COutput.hpp"
#include "Components/CProfilers.hpp"
#include "Components/CState.hpp"
#include "Components/CRenderdoc.hpp"
#include "Components/CConstants.hpp"
#include "Engine/ExtObject.hpp"
#include "Misc/GuidCacheUtils.hpp"
#include "Shell/ClientConfig.hpp"
#include "Shell/CmdArgs.hpp"
#include "Version/SharedVersion.h"


SPI_PLUGINSIDE_SUPPORT(ASI_NAME_NO_SPACE_W, DEVELOPER_W, L"" VERSION_STRING_W, SPI_GAME_LE3, SPI_VERSION_ANY);
SPI_PLUGINSIDE_POSTLOAD;
SPI_PLUGINSIDE_SEQATTACH;

SPI_IMPLEMENT_ATTACH
{
    ::RelativeTimer AttachTimer{};


    // Things that component initialization depends on.
    // ----------------------------------------

    gp_sdkContext = new ::LESDK::Initializer{ InterfacePtr, "MilanHost" };
    gp_componentContainer = new ComponentContainer;
    gb_disableSafeBreak = CmdArgs::Contain(L"-nobreak");

    InitSDKBasics(*gp_sdkContext);
    InitGlobalConfig(k_configFilename);

    // Initialize subsystems / compontents.
    // ----------------------------------------
    gp_componentContainer->Initialize<COutput>(*gp_sdkContext);
    gp_componentContainer->Initialize<CConstants>(*gp_sdkContext);
    gp_componentContainer->Initialize<CGlobals>(*gp_sdkContext);
    gp_componentContainer->Initialize<CCommands>(*gp_sdkContext);
    gp_componentContainer->Initialize<CProfilers>(*gp_sdkContext);
#ifdef WITH_IMGUI
    gp_componentContainer->Initialize<CImgui>(*gp_sdkContext);
#endif
    gp_componentContainer->Initialize<CState>(*gp_sdkContext);

#ifndef _FINAL_RELEASE
    gp_componentContainer->Initialize<CNetworkProfiler>(*gp_sdkContext);
    gp_componentContainer->Initialize<CRenderdoc>(*gp_sdkContext);
#endif

    // These need to run after globals.
    COMPONENT(COutput).PostInitialize(*gp_sdkContext);
    COMPONENT(CGlobals).PostInitialize(*gp_sdkContext);


    // Run additional startup logic.
    // ----------------------------------------

    ReportConfigError();
    SetupImguiInterfaces();
    RegisterNonshipCommands();
    RegisterVectoredExceptions();
    SpawnLocalClients();


    MILAN_INFOW(ELOG_Startup, L"attach completed in %.3f ms.", AttachTimer.GetMilliseconds());
    MILAN_INFOW(ELOG_Startup, L"game ordinal instance = %d", ::GetGameInstanceOrdinal());

    COMPONENT(COutput).FlushEverything();

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

    MILAN_INFOW(ELOG_Teardown, L"single detach starting");

    COMPONENT(COutput).FlushEverything();
    COMPONENT(CGlobals).UninstallSomeHooks(*gp_sdkContext);

    // Terminate subsystems in reverse.
    // ----------------------------------------

#ifndef _FINAL_RELEASE
    gp_componentContainer->Terminate<CRenderdoc>(*gp_sdkContext);
    gp_componentContainer->Terminate<CNetworkProfiler>(*gp_sdkContext);
#endif

    gp_componentContainer->Terminate<CState>(*gp_sdkContext);
#ifdef WITH_IMGUI
    gp_componentContainer->Terminate<CImgui>(*gp_sdkContext);
#endif
    gp_componentContainer->Terminate<CProfilers>(*gp_sdkContext);
    gp_componentContainer->Terminate<CCommands>(*gp_sdkContext);
    gp_componentContainer->Terminate<CGlobals>(*gp_sdkContext);
    gp_componentContainer->Terminate<COutput>(*gp_sdkContext);

    CleanupGlobalConfig();

    delete std::exchange(gp_componentContainer, nullptr);
    delete std::exchange(gp_sdkContext, nullptr);
}

void InitSDKBasics(::LESDK::Initializer& Context)
{
    // Cannot use logging here because that is dependent on GMalloc.
    GMalloc = Context.ResolveTyped<FMallocLike*>(BUILTIN_GMALLOC_RIP);
    assert(GMalloc != nullptr && *GMalloc != nullptr);

    // If this doesn't hard crash we should be good to go...
    void* const BogusAllocation = (*GMalloc)->Malloc(4, UN_DEFAULT_ALIGNMENT);
    void* const BogusReallocation = (*GMalloc)->Realloc(BogusAllocation, 128, UN_DEFAULT_ALIGNMENT);
    (*GMalloc)->Free(BogusReallocation);

    // Initialize SFXName addresses here too because logging is going to need them.
    SFXName::GBioNamePools = Context.ResolveTyped<SFXNameEntry const*>(BUILTIN_SFXNAMEPOOLS_RIP);
    SFXName::GInitMethod = Context.ResolveTyped<SFXName::tInitMethod>(BUILTIN_SFXNAMEINIT_PHOOK);
    assert(SFXName::GBioNamePools != nullptr && SFXName::GInitMethod != nullptr);
}

void ReportConfigError()
{
    if (gp_clientConfig == nullptr && gp_clientConfigError != nullptr) [[unlikely]]
        MILAN_ERRORW(ELOG_Startup, L"failed to read config: %S", gp_clientConfigError->c_str());
}

void SetupImguiInterfaces()
{
#ifdef WITH_IMGUI
    COMPONENT(CImgui).RegisterInterface(std::make_unique<DemoInterface>());
#endif
}

void RegisterNonshipCommands()
{
    auto& Commands = COMPONENT(CCommands);

#ifndef _FINAL_RELEASE
    Commands.RegisterCommand(L"dbg", L"assertme",
        [](wchar_t const*)
        {
            MILAN_ERRORW(L"ASSERTING THIS PLANE");  // what
            MILAN_CHECKW(4 % 2 == 1, L"some assertion message");
            return true;
        });

    Commands.RegisterCommand(L"dbg", L"breakme",
        [](wchar_t const*)
        {
            MILAN_ERRORW(L"BREAKING THIS PLANE");
            MILAN_BREAK_ATTACHED();
            return true;
        });

    Commands.RegisterCommand(L"dbg", L"crashme",
        [](wchar_t const*)
        {
            MILAN_ERRORW(L"CRASHING THIS PLANE");
            ::RaiseException(1, 0, 0, nullptr);
            return true;
        });

    Commands.RegisterCommand(L"dump", L"classinit",
        [](wchar_t const*)
        {
            for (TypedObjectIterator<UClass, false, false> Iter{}; Iter; ++Iter)
            {
                MILAN_INFOW(L"%s => %p / %p / %p", *(*Iter)->GetFullName(),
                    (*Iter)->ClassConstructor, (*Iter)->ClassStaticConstructor, (*Iter)->ClassStaticInitializer);
            }
            return true;
        });

    Commands.RegisterCommand(L"dump", L"guidcaches",
        [](wchar_t const*)
        {
            MILAN_INFOW(L"dumping guid caches (to log !!!)");
            int Index = 0;
            for (TypedObjectIterator<UGuidCache, true, false> Iter{}; Iter; ++Iter)
            {
                MILAN_INFOW(L"UGuidCache n. %d: filename = %s", Index, *(*Iter)->Filename);
                for (auto const& [Name, Guid] : (*Iter)->PackageGuids)
                {
                    MILAN_INFOW(L"  %s => %s", *Name.ToString(), *Guid.ToString());
                }
                Index++;
            }
            return true;
        });

    Commands.RegisterCommand(L"dump", L"matexprs",
        [](wchar_t const*)
        {
            MILAN_INFOW(ELOG_DevelUtils, L"Dumping UMaterialExpression infos...");

            for (TypedObjectIterator<UClass, false, false> Iter{}; Iter; ++Iter)
            {
                if ((*Iter)->IsChildOf<UMaterialExpression>())
                {
                    UObject* const Default = (*Iter)->ClassDefaultObject;
                    MILAN_CHECKW(Default->IsA<UMaterialExpression>(), L"strange cdo");

                    auto const IntoRelative = [](void* const Pointer) -> std::ptrdiff_t
                        {
                            auto const Base = reinterpret_cast<BYTE*>(gp_sdkContext->ModuleBase);
                            auto const Address = reinterpret_cast<BYTE*>(Pointer);
                            return static_cast<std::ptrdiff_t>(Address - Base);
                        };

                    auto const __vftable = *reinterpret_cast<BYTE**>(Default);

                    std::ptrdiff_t const Vftable = IntoRelative(__vftable);
                    std::ptrdiff_t const VfCompile = IntoRelative(*reinterpret_cast<void**>(__vftable + 0x2B8));
                    std::ptrdiff_t const Constructor = IntoRelative((*Iter)->ClassConstructor);
                    std::ptrdiff_t const StaticConstructor = IntoRelative((*Iter)->ClassStaticConstructor);
                    std::ptrdiff_t const StaticInitializer = IntoRelative((*Iter)->ClassStaticInitializer);

                    MILAN_INFOW(ELOG_DevelUtils, L"%s / %s => Vftable = 0x%llx, Compile() = 0x%llx, Ctor = 0x%llx, StaticCtor = 0x%llx, StaticInit = 0x%llx",
                        *(*Iter)->GetOutermost()->GetName(), *(*Iter)->GetName(), Vftable, VfCompile, Constructor, StaticConstructor, StaticInitializer);
                    MILAN_UNUSED_5(Vftable, VfCompile, Constructor, StaticConstructor, StaticInitializer);
                }
            }

            return true;
        });

    Commands.RegisterCommand(L"dump", L"nativefuncs",
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

    Commands.RegisterCommand(L"dump", L"nativeops",
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

    Commands.RegisterCommand(L"dump", L"objdesc",
        [](wchar_t const* const InParams)
        {
            FString const Params{ InParams };

            if (Params.Contains(L"detail", true))
            {
                for (TypedObjectIterator<UObject, true, false> Iter{}; Iter; ++Iter)
                {
                    UObject* const Object = *Iter;
                    FString Detailed{};

                    for (int i = 0; i < 16; ++i)
                    {
                        FString NewLine{};
                        Object->GetDescDetailed(NewLine, i);

                        if (!NewLine.Empty())
                        {
                            Detailed.Append(NewLine);
                            Detailed.Append(L" / ");
                        }
                    }

                    if (!Detailed.Empty())
                    {
                        // This is quite unsafe but will have to do until I add Erase().
                        const_cast<wchar_t*>(Detailed.Chars())[Detailed.Length() - 3] = L'\0';
                        MILAN_INFOW(L"%s: %s", *Object->GetFullName(), *Detailed);
                    }
                }
            }
            else
            {
                for (TypedObjectIterator<UObject, true, false> Iter{}; Iter; ++Iter)
                {
                    UObject* const Object = *Iter;
                    FString const Brief = Object->GetDescBrief();

                    if (!Brief.Empty())
                    {
                        MILAN_INFOW(L"%s: %s", *Object->GetFullName(), *Brief);
                    }
                }
            }

            return true;
        });

    Commands.RegisterCommand(L"dump", L"onlinevers",
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

    Commands.RegisterCommand(L"dump", L"replistcalc",
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

    Commands.RegisterCommand(L"dump", L"sfxonline",
        [](wchar_t const*)
        {
            SFXName const NAME_SFXOnlineFoundation(L"SFXOnlineFoundation", 0);
            std::ptrdiff_t const Base = reinterpret_cast<std::ptrdiff_t>(::LESDK::GetMainModuleBase());

            MILAN_INFOW(L"Dumping native functions in SFXOnlineFoundation package:");
            for (TypedObjectIterator<UFunction, false, false> Iter; Iter; ++Iter)
            {
                UFunction* const Function = *Iter;
                UPackage* const Package = Function->GetOutermost();
                UClass* const Outer = Function->Outer->Cast<UClass>();

                bool const bIsNative = (Function->FunctionFlags & 0x400) != 0;
                bool const bIsInterface = Outer && (Outer->ClassFlags & 0x4000) != 0;

                if (Package->Name == NAME_SFXOnlineFoundation && bIsNative && !bIsInterface)
                    MILAN_INFOW(L"%s => 0x%llx", *Function->GetFullName(), (std::ptrdiff_t)Function->Func - Base);

                MILAN_UNUSED_2(Outer, bIsInterface);
            }
            return true;
        });

    Commands.RegisterCommand(L"dump", L"sfxuid",
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

#if defined(MILAN_BUILD_LOGINTERNAL) && MILAN_BUILD_LOGINTERNAL

    Commands.RegisterCommand(L"log", L"loginternal",
        [](wchar_t const*)
        {
            bool const Enabled = COMPONENT(COutput).ToggleLogInternal();
            MILAN_INFOW(L"loginternal: %s", Enabled ? L"enabled" : L"disabled");
            return true;
        });

    Commands.RegisterCommand(L"log", L"warninternal",
        [](wchar_t const*)
        {
            bool const Enabled = COMPONENT(COutput).ToggleWarnInternal();
            MILAN_INFOW(L"warninternal: %s", Enabled ? L"enabled" : L"disabled");
            return true;
        });

#endif

    Commands.RegisterCommand(L"log", L"processinternal",
        [](wchar_t const*)
        {
            gb_logProcessInternal = !gb_logProcessInternal;
            MILAN_INFOW(L"logging of uobject::processinternal: %s",
                gb_logProcessInternal ? L"enabled" : L"disabled");
            return true;
        });

    Commands.RegisterCommand(L"misc", L"updateguidcacheoverride",
        [](wchar_t const*)
        {
            COMPONENT(CGlobals).UpdateGuidCacheOverride();
            return true;
        });

#endif

    MILAN_UNUSED(Commands);
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
                COMPONENT(CProfilers).On_VectoredException(Address, Code, Exceptions);
                COMPONENT(COutput).On_VectoredException(Address, Code, Exceptions);
                COMPONENT(CRenderdoc).On_VectoredException(Address, Code, Exceptions);

            #ifndef _FINAL_RELEASE
                COMPONENT(CNetworkProfiler).On_VectoredException(Address, Code, Exceptions);
            #endif // !_FINAL_RELEASE
            }

            return EXCEPTION_CONTINUE_SEARCH;
        });
}

void SpawnLocalClients()
{
#ifndef _FINAL_RELEASE

    int const Instance = ::GetGameInstanceOrdinal();

    FString const Command{ CmdArgs::GetWithExec() };
    FString const Arguments{ CmdArgs::Get() };

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

        MILAN_DEBUGW(ELOG_Startup, L"spawning %d additional client(s)", Count);
        MILAN_TRACEW(ELOG_Startup, L"command arguments: %s", *Arguments);
        MILAN_TRACEW(ELOG_Startup, L"working directory: %s", WorkingDirectory.data());
        MILAN_TRACEW(ELOG_Startup, L"executable filename: %s", ExecutableFilename);

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
                MILAN_TRACEW(ELOG_Startup, L"spawned additional client %d / %d, pid = %d", i + 1, Count, ProcessInfo.dwProcessId);
                ::CloseHandle(ProcessInfo.hProcess);
                ::CloseHandle(ProcessInfo.hThread);
            }
            else
            {
                MILAN_ERRORW(ELOG_Startup, L"failed to spawn additional client %d / %d", i + 1, Count);
                MILAN_ERRORW(ELOG_Startup, L"  last error code = %d", ::GetLastError());
                break;
            }
        }
    }

#endif
}

::LESDK::Initializer* gp_sdkContext = nullptr;
HMODULE g_hModule = nullptr;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        // Capture pointer to module
        g_hModule = hModule;
    }
    return TRUE;
}
