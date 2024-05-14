#include <LESDK/Init.hpp>
#include <LESDK/Headers.hpp>

#include <milan_ecsm_ffi.h>

#include "Base.hpp"
#include "Hooks.hpp"
#include "Intercept.hpp"
#include "Misc.hpp"
#include "MPProfile.hpp"
#include "Streams.hpp"
#include "Systems/Console.hpp"
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

#ifndef _FINAL_RELEASE

        Console::RegisterCommand(L"net", L"logactorrepl",
            [](wchar_t const*)
            {
                gb_logReplicateActor = !gb_logReplicateActor;
                MILAN_INFOW(L"actor replication log %s", gb_logReplicateActor ? L"ENABLED" : L"DISABLED");
                return true;
            });

#endif

        MILAN_INFOW(L"state system initialized");
    }

    void Terminate()
    {
        MILAN_INFOW(L"state system terminating");
        FunctionInterceptLegacy::CleanupRegistered();

        ::ecsm::uninit_state();
    }

    bool On_UObject_ProcessInternal_Pre(UObject* const Context, FFrame* const Stack, void* const Result)
    {
        MILAN_UNUSED_3(Context, Stack, Result);

        static FunctionInterceptLegacy SFXSaveManagerMP_SaveRecordsImpl{};
        if (SFXSaveManagerMP_SaveRecordsImpl.Applies(Stack->Node, L"Function SFXGame.SFXSaveManagerMP.SaveRecordsImpl"))
        {
            MILAN_TRACEW(L"intercepted SFXGame.SFXSaveManagerMP.SaveRecordsImpl:");

            MPProfile Profile{ (*GEngine)->Cast<USFXEngine>() };
            ::RelativeTimer ProfileTimer{};

            MemoryOutputStream ProfileStream{ 204800 };  // reserve 200kb
            Profile.Serialize(ProfileStream);

            MILAN_TRACEW(L"profile serialized in %.3f ms.", ProfileTimer.GetMilliseconds());

            ::WriteFileFull(static_cast<std::wstring_view>(Profile.GetProfilePath()), ProfileStream.View());
        }

        return true;
    }

    void On_UObject_ProcessInternal_Post(UObject* const Context, FFrame* const Stack, void* const Result)
    {
        MILAN_UNUSED_3(Context, Stack, Result);

        // This needs to be done after ProcessInternal is called so that OnlineSave and friends will have been created.
        static FunctionInterceptLegacy SFXSaveManagerMP_Initialize{};
        if (SFXSaveManagerMP_Initialize.Applies(Stack->Node, L"Function SFXGame.SFXSaveManagerMP.Initialize"))
        {
            MILAN_TRACEW(L"intercepted SFXGame.SFXSaveManagerMP.Initialize:");

            MPProfile Profile{ (*GEngine)->Cast<USFXEngine>() };

            ::game::vector<BYTE> ProfileBuffer{};
            ::ReadFileFull(static_cast<std::wstring_view>(Profile.GetProfilePath()), ProfileBuffer);

            ::RelativeTimer ProfileTimer{};

            MemoryInputStream ProfileStream{ std::move(ProfileBuffer) };
            Profile.Deserialize(ProfileStream);

            MILAN_TRACEW(L"profile deserialized in %.3f ms.", ProfileTimer.GetMilliseconds());
        }
    }

    void On_UWorld_Tick(UWorld* const Context, int const Unknown, float const Delta)
    {
        MILAN_UNUSED_3(Context, Unknown, Delta);
        ::ecsm::world_tick(Delta);
    }
}
