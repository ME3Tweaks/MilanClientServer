/**
 * @file        MilanHost/Systems/Profilers.hpp
 * @header      Subsystem that implements custom canvas-based profilers.
 */

#pragma once

#include <LESDK/Init.hpp>

#include "Canvas.hpp"
#include "Proxies/Engine.hpp"


namespace Milan::Profilers
{
    struct CCanvasProfiler
    {
        wchar_t const* Subtitle;
        wchar_t const* Description;
        void (*Function)(MilanCanvasDrawer& Drawer, UObject* Target);
    };

    enum EMilanProfilerMode : int
    {
        MPM_None = 0,
        MPM_Network = 1,
        MPM_WorldInfo = 2,

        MPM_Max
    };


    // ! Global subsystem functions.
    // ========================================

    void Initialize(::LESDK::Initializer& Context);
    void Terminate();


    // ! Public interface.
    // ========================================

    void On_UObject_ProcessEvent(UObject* Context, UFunction* Function, void* Parms, void* Result);

    void ProfileNetwork(MilanCanvasDrawer& Drawer, UObject* Target);
    void ProfileWorldInfo(MilanCanvasDrawer& Drawer, UObject* Target);


    // ! Internal logic.
    // ========================================

    void DrawActorChannel(MilanCanvasDrawer& Drawer, UActorChannel_Proxy* Channel, unsigned int Index);
}
