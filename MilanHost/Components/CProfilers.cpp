#include "Components/CCommands.hpp"
#include "Components/CProfilers.hpp"
#include "Engine/Intercept.hpp"
#include "Milan/Misc.hpp"
#include "NetProf/NetProfStream.hpp"


// ! CProfilers implementation.
// ========================================

bool CProfilers::Initialize(::LESDK::Initializer& Context)
{
    SetupProfilerRecords();
    RegisterCommands();

    MILAN_UNUSED(Context);
    MILAN_DEBUGW(ELOG_Startup, L"component '%s' initialized", k_typeName);
    return true;
}

void CProfilers::Terminate(::LESDK::Initializer& Context)
{
    MILAN_UNUSED(Context);
    MILAN_DEBUGW(ELOG_Teardown, L"component '%s' terminating", k_typeName);
}

void CProfilers::On_VectoredException(void* const Address, DWORD const Code, _EXCEPTION_POINTERS* const Exceptions)
{
    MILAN_UNUSED_3(Address, Code, Exceptions);
}

void CProfilers::On_UObject_ProcessEvent(UObject* const Context, UFunction* const Function, void* const Parms, void* const Result)
{
    MILAN_UNUSED_4(Context, Function, Parms, Result);

    static FunctionInterceptLegacy GameViewportClient_PostRender{};
    if (GameViewportClient_PostRender.Applies(Function, L"Function Engine.GameViewportClient.PostRender"))
    {
        auto* const TParms = reinterpret_cast<UGameViewportClient_eventPostRender_Parms*>(Parms);

        m_activeCanvas.UpdateCanvas(TParms->Canvas);
        m_activeCanvas.UpdateGlobals((*GEngine)->Cast<USFXEngine>());
        m_activeCanvas.Reset();

        UObject* const Target = nullptr;

        if (m_profilerMode != CPM_None)
        {
            MILAN_CHECKW(m_profilerMode >= 0 && m_profilerMode < CPM_Max, L"");
            auto const& Profiler = m_profilerRecords[m_profilerMode];

            m_activeCanvas.DrawHeader(Profiler.Subtitle, Profiler.Description, Target, MilanCanvasDrawer::k_warnColor);
            if (Profiler.Function) (*Profiler.Function)(m_activeCanvas, Target);
        }
    }
}

void CProfilers::SetupProfilerRecords()
{
    m_profilerRecords[CPM_None] = {
        .Subtitle = L"",
        .Description = L"",
        .Function = nullptr,
    };

    m_profilerRecords[CPM_PendingPackages] = {
        .Subtitle = L"Pending packages",
        .Description = L"Displays pending packages in the active server connection",
        .Function = ::ProfilePendingPackages,
    };

    m_profilerRecords[CPM_PlayerControllers] = {
        .Subtitle = L"Player controllers",
        .Description = L"Displays active APlayerController instances",
        .Function = ::ProfilePlayerControllers,
    };

    m_profilerRecords[CPM_Network] = {
        .Subtitle = L"Network",
        .Description = L"Displays active channels and related networking info",
        .Function = ::ProfileNetwork,
    };

    m_profilerRecords[CPM_WorldInfo] = {
        .Subtitle = L"WorldInfo",
        .Description = L"Displays information about active world",
        .Function = ::ProfileWorldInfo,
    };
}

void CProfilers::RegisterCommands()
{
    auto& Commands = COMPONENT(CCommands);

    Commands.RegisterCommand(L"dbg", L"nextprofile",
        [&](wchar_t const*)
        {
            auto NextMode = (ECanvasProfilerMode)((int)m_profilerMode + 1);
            if (NextMode >= CPM_Max) NextMode = CPM_None;
            m_profilerMode = NextMode;
            return true;
        });

    Commands.RegisterCommand(L"dbg", L"profile",
        [&](wchar_t const* const Params)
        {
            //MILAN_INFOW(L"dbg.profile: params = %s (%d)",
            //    Params ? Params : L"(null)",
            //    Params ? (int)::wcslen(Params) : -1);

            if (!Params || ::wcslen(Params) == 0)
            {
                MILAN_WARNW(L"empty dbg.profile parameter");
                return true;
            }

            if (!::_wcsicmp(Params, L"none"))
                m_profilerMode = CPM_None;
            else if (!::_wcsicmp(Params, L"pending") || !::_wcsicmp(Params, L"pend"))
                m_profilerMode = CPM_PendingPackages;
            else if (!::_wcsicmp(Params, L"playercontrollers") || !::_wcsicmp(Params, L"pcs"))
                m_profilerMode = CPM_PlayerControllers;
            else if (!::_wcsicmp(Params, L"network") || !::_wcsicmp(Params, L"net"))
                m_profilerMode = CPM_Network;
            else if (!::_wcsicmp(Params, L"worldinfo") || !::_wcsicmp(Params, L"wi"))
                m_profilerMode = CPM_WorldInfo;
            else
                MILAN_WARNW(L"unknown dbg.profile parameter: %s", Params);

            return true;
        });

    Commands.RegisterCommand(L"dump", L"pckgmap",
        [](wchar_t const*)
        {
            MILAN_DEBUGW(L"dumping package maps for active connections");
            for (TypedObjectIterator<UPackageMap, true, false> Iter; Iter; ++Iter)
            {
                MILAN_DEBUGW(L"  %s", *(*Iter)->StaticFullName());
                //auto const Packages = reinterpret_cast<UPackageMap_Proxy*>(*Iter);
                for (FPackageInfo const& Package : (*Iter)->List)
                {
                    MILAN_UNUSED(Package);
                    MILAN_DEBUGW(L"    %s, guid = %s, gen = %d / %d",
                        *Package.PackageName.ToString(), *FormatGuid(Package.Guid),
                        Package.LocalGeneration, Package.RemoteGeneration);
                }
            }

            return true;
        });
}
