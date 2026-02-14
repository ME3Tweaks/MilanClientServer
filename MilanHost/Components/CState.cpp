#include "Base/IOUtils.hpp"
#include "Base/Streams.hpp"
#include "Components/CCommands.hpp"
#include "Components/CState.hpp"
#include "Engine/Intercept.hpp"
#include "Online/MPProfile.hpp"


// ! CState implementation.
// ========================================

bool CState::Initialize(::LESDK::Initializer& Context)
{
    MILAN_UNUSED(Context);
    MILAN_DEBUGW(ELOG_Startup, L"component '%s' initialized", k_typeName);
    return true;
}

void CState::Terminate(::LESDK::Initializer& Context)
{
    MILAN_UNUSED(Context);
    MILAN_DEBUGW(ELOG_Teardown, L"component '%s' terminating", k_typeName);

    FunctionStructIntercept::CleanupRegistered();
}

void CState::On_VectoredException(void* const Address, DWORD const Code, _EXCEPTION_POINTERS* const Exceptions)
{
    MILAN_UNUSED_3(Address, Code, Exceptions);
}

bool CState::On_UObject_ProcessInternal_Pre(UObject* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);

    static FunctionStructIntercept SFXSaveManagerMP_SaveRecordsImpl{};
    if (SFXSaveManagerMP_SaveRecordsImpl.Applies(Stack->Node, L"Function SFXGame.SFXSaveManagerMP.SaveRecordsImpl"))
    {
        MILAN_TRACEW(ELOG_MultiProfile, L"intercepted SFXGame.SFXSaveManagerMP.SaveRecordsImpl:");
        [[maybe_unused]] auto const Object = Context->Cast<USFXSaveManagerMP>();
        [[maybe_unused]] auto const Params = (USFXSaveManagerMP_execSaveRecordsImpl_Parms*)Stack->Locals;

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
    static FunctionStructIntercept SFXSaveManagerMP_Initialize{};
    if (SFXSaveManagerMP_Initialize.Applies(Stack->Node, L"Function SFXGame.SFXSaveManagerMP.Initialize"))
    {
        MILAN_TRACEW(ELOG_MultiProfile, L"intercepted SFXGame.SFXSaveManagerMP.Initialize:");

        MPProfile Profile{ (*GEngine)->Cast<USFXEngine>() };
        if (Profile.CachedProfileExists()) {
            ::game::vector<BYTE> ProfileBuffer{};
            ::ReadFileFull(static_cast<std::wstring_view>(Profile.GetProfilePath()), ProfileBuffer);

            ::RelativeTimer ProfileTimer{};

            MemoryInputStream ProfileStream{ std::move(ProfileBuffer) };
            Profile.Deserialize(ProfileStream);
            MILAN_TRACEW(ELOG_MultiProfile, L"profile deserialized in %.3f ms.", ProfileTimer.GetMilliseconds());
         }
        else {
            MILAN_TRACEW(ELOG_MultiProfile, L"No local cached MP_Profile found");
        }
    }
}

void CState::On_UWorld_Tick(UWorld* const Context, int const Unknown, float const Delta)
{
    MILAN_UNUSED_3(Context, Unknown, Delta);
}
