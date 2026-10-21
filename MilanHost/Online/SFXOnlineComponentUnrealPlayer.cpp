#include <cstring>

#include "Base/Base.hpp"
#include "Engine/Intercept.hpp"
#include "Online/LocalPlayerIds.hpp"
#include "Online/SFXOnlineHijacks.hpp"

// Hooks for setting the user id so kick votes work
namespace SFXOnlineComponentUnrealPlayer
{
    DECLARE_HIJACK(USFXOnlineComponentUnrealPlayer, GetUniquePlayerId);
}

void SFXOnlineHijacks::InstallComponentUnrealPlayer()
{
    MILAN_DEBUGW(L"Installing hijacks for: %s...", L"SFXOnlineComponentUnrealPlayer");
    UClass* const SearchedClass = HijackUtils::FindClass(SFXName(L"SFXOnlineComponentUnrealPlayer", 0));
    using namespace SFXOnlineComponentUnrealPlayer;
    INSTALL_HIJACK(SearchedClass, GetUniquePlayerId);
}

void SFXOnlineComponentUnrealPlayer::GetUniquePlayerId_Hook(USFXOnlineComponentUnrealPlayer* const Context,
    FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED(Context);
    NATIVE_GET_BYTE(LocalUserNum);
    NATIVE_GET_STRUCT_BYREF(FUniqueNetId, PlayerID);
    NATIVE_FINISH();

    static LocalPlayerIds Ids{};
    std::uint64_t Id{};
    bool const Success = Ids.Get(LocalUserNum, Id);
    static_assert(sizeof(PlayerID) == sizeof(Id));
    std::memcpy(&PlayerID, &Id, sizeof(Id));
    *static_cast<UBOOL*>(Result) = Success ? TRUE : FALSE;
    if (!Success)
    {
        MILAN_ERRORW(ELOG_SFXOnline, L"GetUniquePlayerId: failed to generate local user %d identity", LocalUserNum);
    }
}
