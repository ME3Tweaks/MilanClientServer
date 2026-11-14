/**
 * @file        MilanHost/Components/CState.hpp
 * @header      Component for managing client state.
 */

#pragma once

#include "Base/Base.hpp"
#include "Engine/Hooks.hpp"


class CState final : public NonCopyable
{
public:

    static constexpr wchar_t const* k_typeName = L"state";

    bool Initialize(::LESDK::Initializer& Context);
    void Terminate(::LESDK::Initializer& Context);

    /** Executes some code when a vectored exception is handled by MilanHost. */
    void On_VectoredException(void* Address, DWORD Code, _EXCEPTION_POINTERS* Exceptions);
    /** Executes some code when a UObject::ProcessInternal (pre) hook is triggered. */
    bool On_UObject_ProcessInternal_Pre(UObject* Context, FFrame* Stack, void* Result);
    /** Executes some code when a UObject::ProcessInternal (post) hook is triggered. */
    void On_UObject_ProcessInternal_Post(UObject* Context, FFrame* Stack, void* Result);
    /** Executes some code when a UWorld::Tick hook is triggered. */
    void On_UWorld_Tick(UWorld* Context, int Unknown, float Delta);

    static void CommitProfileImmediately();
};

static_assert(ComponentType<CState>);
