#include "Base/Base.hpp"
#include "Engine/Intercept.hpp"
#include "Online/SFXOnlineHijacks.hpp"


// ! Forward-declarations.
// ========================================

namespace SFXOnlineComponentBlazeLogin
{
    DECLARE_HIJACK(USFXOnlineComponentBlazeLogin, IsActiveUser);
    DECLARE_HIJACK(USFXOnlineComponentBlazeLogin, IsConnected);
    DECLARE_HIJACK(USFXOnlineComponentBlazeLogin, IsSignedIn);
    DECLARE_HIJACK(USFXOnlineComponentBlazeLogin, HasInternetConnection);
    DECLARE_HIJACK(USFXOnlineComponentBlazeLogin, CanPlayOnline);
}


// ! Public interface.
// ========================================

void SFXOnlineHijacks::InstallComponentBlazeLogin()
{
    MILAN_DEBUGW(L"Installing hijacks for: %s...", L"SFXOnlineComponentBlazeLogin");
    UClass* const SearchedClass = HijackUtils::FindClass(SFXName(L"SFXOnlineComponentBlazeLogin", 0));

    using namespace SFXOnlineComponentBlazeLogin;
    INSTALL_HIJACK(SearchedClass, IsActiveUser);
    INSTALL_HIJACK(SearchedClass, IsConnected);
    INSTALL_HIJACK(SearchedClass, IsSignedIn);
    INSTALL_HIJACK(SearchedClass, HasInternetConnection);
    INSTALL_HIJACK(SearchedClass, CanPlayOnline);
}


// ! Hijacked function bodies.
// ========================================

void SFXOnlineComponentBlazeLogin::IsActiveUser_Hook(USFXOnlineComponentBlazeLogin* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    MILAN_BREAK_ATTACHED();

    IsActiveUser_Original(Context, Stack, Result);
}

void SFXOnlineComponentBlazeLogin::IsConnected_Hook(USFXOnlineComponentBlazeLogin* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    MILAN_BREAK_ATTACHED();

    IsConnected_Original(Context, Stack, Result);
}

void SFXOnlineComponentBlazeLogin::IsSignedIn_Hook(USFXOnlineComponentBlazeLogin* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //IsSignedIn_Original(Context, Stack, Result);

    NATIVE_FINISH();
    *(UBOOL*)Result = TRUE;
}

void SFXOnlineComponentBlazeLogin::HasInternetConnection_Hook(USFXOnlineComponentBlazeLogin* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    MILAN_BREAK_ATTACHED();

    HasInternetConnection_Original(Context, Stack, Result);
}

// Enum Engine.OnlineSubsystem.EFeaturePrivilegeLevel
enum EFeaturePrivilegeLevel
{
    FPL_Disabled                                       = 0,
    FPL_EnabledFriendsOnly                             = 1,
    FPL_Enabled                                        = 2,
    FPL_MAX                                            = 3
};

void SFXOnlineComponentBlazeLogin::CanPlayOnline_Hook(USFXOnlineComponentBlazeLogin* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //CanPlayOnline_Original(Context, Stack, Result);

    NATIVE_GET_INT_OPTIONAL(nUserIndex, -1);
    NATIVE_FINISH();

    *(EFeaturePrivilegeLevel*)Result = FPL_Enabled;
}
