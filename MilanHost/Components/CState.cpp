#include "Base/Streams.hpp"
#include "Components/CCommands.hpp"
#include "Components/CState.hpp"
#include "Engine/Intercept.hpp"
#include "Milan/Misc.hpp"
#include "Online/MPProfile.hpp"

#include <milan_ecsm_ffi.h>


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
    MILAN_CHECKW(gp_mainLogger != nullptr, L"");
    ::CodePlaceA const Place{ .Func = "", .File = file, .Line = (int)line };
    gp_mainLogger->WriteAnsi(Place, static_cast<ESeverity>(sev), ELOG_Client, msg);
}


// ! CState implementation.
// ========================================

bool CState::Initialize(::LESDK::Initializer& Context)
{
    ::ecsm::init_allocator(::ecsm_alloc, ::ecsm_realloc, ::ecsm_free);
    ::ecsm::init_logger(::ecsm_log);
    ::ecsm::init_state();

    MILAN_UNUSED(Context);
    MILAN_DEBUGW(ELOG_Startup, L"component '%s' initialized", k_typeName);
    return true;
}

void CState::Terminate(::LESDK::Initializer& Context)
{
    MILAN_UNUSED(Context);
    MILAN_DEBUGW(ELOG_Teardown, L"component '%s' terminating", k_typeName);

    FunctionInterceptLegacy::CleanupRegistered();
    ::ecsm::uninit_state();
}

void CState::On_VectoredException(void* const Address, DWORD const Code, _EXCEPTION_POINTERS* const Exceptions)
{
    MILAN_UNUSED_3(Address, Code, Exceptions);
}

bool CState::On_UObject_ProcessInternal_Pre(UObject* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);

    static FunctionInterceptLegacy SFXSaveManagerMP_SaveRecordsImpl{};
    if (SFXSaveManagerMP_SaveRecordsImpl.Applies(Stack->Node, L"Function SFXGame.SFXSaveManagerMP.SaveRecordsImpl"))
    {
        MILAN_TRACEW(ELOG_MultiProfile, L"intercepted SFXGame.SFXSaveManagerMP.SaveRecordsImpl:");

        MPProfile Profile{ (*GEngine)->Cast<USFXEngine>() };
        ::RelativeTimer ProfileTimer{};

        MemoryOutputStream ProfileStream{ 204800 };  // reserve 200kb
        Profile.Serialize(ProfileStream);

        MILAN_TRACEW(ELOG_MultiProfile, L"profile serialized in %.3f ms.", ProfileTimer.GetMilliseconds());

        ::WriteFileFull(static_cast<std::wstring_view>(Profile.GetProfilePath()), ProfileStream.View());
    }

    return true;
}

void CState::On_UObject_ProcessInternal_Post(UObject* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);

    // This needs to be done after ProcessInternal is called so that OnlineSave and friends will have been created.
    static FunctionInterceptLegacy SFXSaveManagerMP_Initialize{};
    if (SFXSaveManagerMP_Initialize.Applies(Stack->Node, L"Function SFXGame.SFXSaveManagerMP.Initialize"))
    {
        MILAN_TRACEW(ELOG_MultiProfile, L"intercepted SFXGame.SFXSaveManagerMP.Initialize:");

        MPProfile Profile{ (*GEngine)->Cast<USFXEngine>() };

        ::game::vector<BYTE> ProfileBuffer{};
        ::ReadFileFull(static_cast<std::wstring_view>(Profile.GetProfilePath()), ProfileBuffer);

        ::RelativeTimer ProfileTimer{};

        MemoryInputStream ProfileStream{ std::move(ProfileBuffer) };
        Profile.Deserialize(ProfileStream);

        MILAN_TRACEW(ELOG_MultiProfile, L"profile deserialized in %.3f ms.", ProfileTimer.GetMilliseconds());
    }
}

void CState::On_UWorld_Tick(UWorld* const Context, int const Unknown, float const Delta)
{
    MILAN_UNUSED_3(Context, Unknown, Delta);
    ::ecsm::world_tick(Delta);
}
