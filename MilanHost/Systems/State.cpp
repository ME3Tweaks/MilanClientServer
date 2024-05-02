#include <LESDK/Init.hpp>
#include <LESDK/Headers.hpp>

#include <milan_ecsm_ffi.h>

#include "Base.hpp"
#include "Intercept.hpp"
#include "MPProfile.hpp"
#include "Systems/Output.hpp"
#include "Systems/State.hpp"


void* ecsm_alloc(unsigned long long const size, unsigned long long const align)
{
    MILAN_UNUSED(align);
    return (*GMalloc)->Malloc(static_cast<DWORD>(size), UN_DEFAULT_ALIGNMENT);
}

void* ecsm_realloc(void* const ptr, unsigned long long const size, unsigned long long const align)
{
    MILAN_UNUSED(align);
    return (*GMalloc)->Realloc(ptr, static_cast<DWORD>(size), UN_DEFAULT_ALIGNMENT);
}

void ecsm_free(void* const ptr)
{
    (*GMalloc)->Free(ptr);
}

void ecsm_log(const char* const file, unsigned int const line, int const sev, const char* const msg)
{
    ::CodePlaceA const Place{ .Func = "", .File = file, .Line = static_cast<int>(line) };
    ::Milan::Output::GetMainLogger().WriteAnsi(Place, static_cast<ESeverity>(sev), msg);
}


namespace Milan::State
{
    void Initialize(::LESDK::Initializer& Context)
    {
        MILAN_UNUSED(Context);

        ::ecsm::init_allocator(::ecsm_alloc, ::ecsm_realloc, ::ecsm_free);
        ::ecsm::init_logger(::ecsm_log);
        ::ecsm::init_state();

        MILAN_INFOW(L"state system initialized");
    }

    void Terminate()
    {
        MILAN_INFOW(L"state system terminating");
        FunctionInterceptLegacy::CleanupRegistered();

        ::ecsm::uninit_state();
    }

    bool On_UObject_ProcessInternal(UObject* const Context, FFrame* const Stack, void* const Result)
    {
        MILAN_UNUSED_3(Context, Stack, Result);

        static FunctionInterceptLegacy SFXSaveManagerMP_Initialize{};
        if (SFXSaveManagerMP_Initialize.Applies(Stack->Node, L"Function SFXGame.SFXSaveManagerMP.Initialize"))
        {
            MILAN_WARNW(L"SFXGame.SFXSaveManagerMP.Initialize");
            //MILAN_INFOW(L"MP_Profile path: %s", MPProfile::GetProfilePath());
        }

        static FunctionInterceptLegacy SFXSaveManagerMP_SaveRecordsImpl{};
        if (SFXSaveManagerMP_SaveRecordsImpl.Applies(Stack->Node, L"Function SFXGame.SFXSaveManagerMP.SaveRecordsImpl"))
        {
            MILAN_WARNW(L"SFXGame.SFXSaveManagerMP.SaveRecordsImpl");
        }

        return true;
    }

    void On_UWorld_Tick(UWorld* const Context, int const Unknown, float const Delta)
    {
        MILAN_UNUSED_3(Context, Unknown, Delta);
        ::ecsm::world_tick(Delta);
    }
}
