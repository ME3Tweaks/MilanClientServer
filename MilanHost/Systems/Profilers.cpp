#include <optional>

#include <LESDK/Headers.hpp>

#include "Milan/Base.hpp"
#include "Milan/Canvas.hpp"
#include "Milan/Hooks.hpp"
#include "Milan/Intercept.hpp"
#include "Milan/Misc.hpp"
#include "Milan/NetProfStream.hpp"
#include "Systems/Console.hpp"
#include "Systems/Output.hpp"
#include "Systems/Profilers.hpp"


namespace Milan::Profilers
{
    MilanCanvasDrawer g_canvas{};
    ECanvasProfilerMode g_mode = CPM_None;

    CanvasProfilerRecord g_profilers[CPM_Max] =
    {
        /* CPM_None */
        CanvasProfilerRecord
        {
            .Subtitle = L"",
            .Description = L"",
            .Function = nullptr
        },

        /* CPM_PlayerControllers */
        CanvasProfilerRecord
        {
            .Subtitle = L"Player controllers",
            .Description = L"Displays active APlayerController instances",
            .Function = ::ProfilePlayerControllers,
        },

        /* CPM_Network */
        CanvasProfilerRecord
        {
            .Subtitle = L"Network",
            .Description = L"Displays active channels and related networking info",
            .Function = ::ProfileNetwork
        },

        /* CPM_WorldInfo */
        CanvasProfilerRecord
        {
            .Subtitle = L"WorldInfo",
            .Description = L"Displays information about active world",
            .Function = ::ProfileWorldInfo
        },
    };

    void Initialize(::LESDK::Initializer& Context)
    {
        MILAN_UNUSED(Context);

        // Initialize commands for canvas profiler.
        // ----------------------------------------

        Console::RegisterCommand(L"dbg", L"nextprofile",
            [](wchar_t const*)
            {
                g_mode = (ECanvasProfilerMode)((int)g_mode + 1);
                if (g_mode >= CPM_Max) g_mode = CPM_None;
                return true;
            });

        Console::RegisterCommand(L"dbg", L"profile",
            [](wchar_t const* const Params)
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
                    g_mode = CPM_None;
                else if (!::_wcsicmp(Params, L"playercontrollers") || !::_wcsicmp(Params, L"pcs"))
                    g_mode = CPM_PlayerControllers;
                else if (!::_wcsicmp(Params, L"network") || !::_wcsicmp(Params, L"net"))
                    g_mode = CPM_Network;
                else if (!::_wcsicmp(Params, L"worldinfo") || !::_wcsicmp(Params, L"wi"))
                    g_mode = CPM_WorldInfo;
                else
                    MILAN_WARNW(L"unknown dbg.profile parameter: %s", Params);

                return true;
            });

        Console::RegisterCommand(L"repl", L"capture",
            [](wchar_t const*)
            {
                if (NetworkCapture::Global() != nullptr)
                {
                    MILAN_WARNW(L"netprof: global instance already started");
                    return true;
                }
 
                NetworkCapture::GlobalInitialize();
                return true;
            });

        Console::RegisterCommand(L"repl", L"endcapture",
            [](wchar_t const*)
            {
                if (NetworkCapture::Global() == nullptr)
                {
                    MILAN_WARNW(L"netprof: global instance not started");
                    return true;
                }

                NetworkCapture::GlobalClose();
                return true;
            });

        MILAN_INFOW(L"profilers system initialized");
    }

    void Terminate()
    {
        MILAN_INFOW(L"profilers system terminating");
        g_canvas.~MilanCanvasDrawer();

        if (NetworkCapture::Global() != nullptr)
            NetworkCapture::GlobalClose();
    }

    void On_VectoredException(void* const Address, DWORD const Code, _EXCEPTION_POINTERS* const Exceptions)
    {
        MILAN_WARNW(L"profilers: handling vectored exception, address = %p, code = 0x%08X", Address, Code);

        if (NetworkCapture::Global() != nullptr)
            NetworkCapture::Global()->FlushRecords();

        MILAN_UNUSED_3(Address, Code, Exceptions);
    }

    void On_UObject_ProcessEvent(UObject* const Context, UFunction* const Function, void* const Parms, void* const Result)
    {
        MILAN_UNUSED_4(Context, Function, Parms, Result);

        static FunctionInterceptLegacy GameViewportClient_PostRender{};
        if (GameViewportClient_PostRender.Applies(Function, L"Function Engine.GameViewportClient.PostRender"))
        {
            auto* const TParms = reinterpret_cast<UGameViewportClient_eventPostRender_Parms*>(Parms);

            g_canvas.UpdateCanvas(TParms->Canvas);
            g_canvas.UpdateGlobals((*GEngine)->Cast<USFXEngine>());
            g_canvas.Reset();

            UObject* const Target = nullptr;

            if (g_mode != CPM_None)
            {
                MILAN_CHECKW(g_mode >= 0 && g_mode < CPM_Max, L"");
                auto const& Profiler = g_profilers[g_mode];

                g_canvas.DrawHeader(Profiler.Subtitle, Profiler.Description, Target, MilanCanvasDrawer::k_warnColor);
                if (Profiler.Function) (*Profiler.Function)(g_canvas, Target);
            }
        }
    }
}
