/**
 * @file        MilanHost/Components/CProfilers.hpp
 * @header      Component for initializing canvas and network profilers.
 */

#pragma once

#include "Milan/Base.hpp"
#include "Milan/Hooks.hpp"
#include "Milan/Canvas.hpp"


class CProfilers final : public NonCopyable
{
public:

    static constexpr wchar_t const* k_typeName = L"profilers";

    bool Initialize(::LESDK::Initializer& Context);
    void Terminate(::LESDK::Initializer& Context);

    /** Executes some code when a vectored exception is handled by MilanHost. */
    void On_VectoredException(void* Address, DWORD Code, _EXCEPTION_POINTERS* Exceptions);
    /** Executes some code when UObject::ProcessEvent hook is triggered. */
    void On_UObject_ProcessEvent(UObject* Context, UFunction* Function, void* Parms, void* Result);

private:

    void SetupProfilerRecords();
    void RegisterCommands();

    MilanCanvasDrawer       m_activeCanvas{};
    ECanvasProfilerMode     m_profilerMode{ CPM_None };
    CanvasProfilerRecord    m_profilerRecords[CPM_Max] = {};

};

static_assert(ComponentType<CProfilers>);
