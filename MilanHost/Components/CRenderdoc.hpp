/**
 * @file        MilanHost/Components/CRenderdoc.hpp
 * @header      Component for integrating RenderDoc in-app API.
 */

#pragma once

#include <renderdoc/renderdoc_app.h>

#include "Base/Base.hpp"
#include "Engine/Hooks.hpp"


class CRenderdoc final : public NonCopyable
{
public:

    static constexpr wchar_t const* k_typeName = L"renderdoc";

    bool Initialize(::LESDK::Initializer& Context);
    void Terminate(::LESDK::Initializer& Context);

    void On_VectoredException(void* Address, DWORD Code, _EXCEPTION_POINTERS* Exceptions);
    void On_UObject_ProcessEvent(UObject* Context, UFunction* Function, void* Parms, void* Result);

private:

    void LoadAPI();
    void RegisterCommands();

    RENDERDOC_API_1_3_0*    mp_apiHandle{ nullptr };
    bool                    mb_disableRenderdoc{ false };

};

static_assert(ComponentType<CRenderdoc>);
