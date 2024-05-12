#include <LESDK/Headers.hpp>
#include <LESDK/Init.hpp>

#include "Base.hpp"
#include "Canvas.hpp"
#include "Hooks.hpp"
#include "Intercept.hpp"
#include "Systems/Output.hpp"
#include "Systems/Profilers.hpp"


namespace Milan::Profilers
{
    MilanCanvasDrawer g_canvas{};

    void Initialize(::LESDK::Initializer& Context)
    {
        MILAN_UNUSED(Context);
        MILAN_INFOW(L"profilers system initialized");
    }

    void Terminate()
    {
        MILAN_INFOW(L"profilers system terminating");
        g_canvas.~MilanCanvasDrawer();
    }

    void On_UObject_ProcessEvent(UObject* const Context, UFunction* const Function, void* const Parms, void* const Result)
    {
        MILAN_UNUSED_4(Context, Function, Parms, Result);

        static FunctionInterceptLegacy GameViewportClient_PostRender{};
        if (GameViewportClient_PostRender.Applies(Function, L"Function Engine.GameViewportClient.PostRender"))
        {
            auto* const TParms = reinterpret_cast<UGameViewportClient_eventPostRender_Parms*>(Parms);
            UCanvas* const Canvas = TParms->Canvas;
            MILAN_CHECKW(Canvas != nullptr, L"");

            g_canvas.UpdateCanvas(Canvas);
            g_canvas.UpdateGlobals((*GEngine)->Cast<USFXEngine>());

            g_canvas.Reset();

            //g_canvas.DrawHeader(L"Network", L"Shows active channels and related networking items");
        }
    }
}
