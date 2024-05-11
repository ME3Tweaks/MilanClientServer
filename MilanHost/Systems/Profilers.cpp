#include <LESDK/Headers.hpp>
#include <LESDK/Init.hpp>

#include "Base.hpp"
#include "Hooks.hpp"
#include "Intercept.hpp"
#include "Systems/Output.hpp"
#include "Systems/Profilers.hpp"


namespace Milan::Profilers
{
    void Initialize(::LESDK::Initializer& Context)
    {
        MILAN_UNUSED(Context);
        MILAN_INFOW(L"profilers system initialized");
    }

    void Terminate()
    {
        MILAN_INFOW(L"profilers system terminating");
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

            Canvas->SetDrawColor(255, 255, 255, 255);
            Canvas->SetPos(40.f, 40.f);

            int OutX = 0, OutY = 0;

            FFontRenderInfo const FontRenderInfo{};
            (*UCanvas_WrappedPrint)(Canvas, TRUE, &OutX, &OutY, Canvas->Font, 1.f, 1.f, FALSE, L"Hello there!", &FontRenderInfo);
        }
    }
}
