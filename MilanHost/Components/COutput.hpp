/**
 * @file        MilanHost/Components/COutput.hpp
 * @header      Component for controlling logs and other debug output.
 */

#pragma once
#include "Milan/Base.hpp"


/**
 * @def     MILAN_BUILD_LOGINTERNAL
 * @brief   Controls whether LogInternal/WarnInternal hooks are enabled.
 */
#define MILAN_BUILD_LOGINTERNAL 0


class COutput final : public NonCopyable
{
public:

    static constexpr wchar_t const* k_typeName = L"output";

    bool Initialize(::LESDK::Initializer& Context);
    void Terminate(::LESDK::Initializer& Context);

    /** Additional initialization code that needs to run after all other components' initialization. */
    void PostInitialize(::LESDK::Initializer& Context);

    /** Flushes all output devices initialized by this component. */
    void FlushEverything();

    /** Executes some code when a vectored exception is handled by MilanHost. */
    void On_VectoredException(void* Address, DWORD Code, _EXCEPTION_POINTERS* Exceptions);

#ifndef _FINAL_RELEASE
#if defined(MILAN_BUILD_LOGINTERNAL) && MILAN_BUILD_LOGINTERNAL

    friend void UObject_LogInternal(UObject* Context, FFrame* Stack, void* Result);
    friend void UObject_WarnInternal(UObject* Context, FFrame* Stack, void* Result);

    bool ToggleLogInternal() noexcept;
    bool ToggleWarnInternal() noexcept;

#endif
#endif

private:

    void InitConsole();
    void InitMainLogger();


    bool bUseAsyncLogger{ true };
    bool bUseVirtualTerminal{ true };

    bool bEnableLogInternal{ false };
    bool bEnableWarnInternal{ false };
};

static_assert(ComponentType<COutput>);
