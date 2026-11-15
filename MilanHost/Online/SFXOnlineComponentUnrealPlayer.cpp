#include <cstring>

#include "Base/Base.hpp"
#include "Engine/Intercept.hpp"
#include "Online/LocalPlayerIds.hpp"
#include "Online/MPProfile.hpp"
#include "Online/SFXOnlineHijacks.hpp"

namespace SFXOnlineComponentUnrealPlayer
{
    DECLARE_HIJACK(USFXOnlineComponentUnrealPlayer, GetUniquePlayerId);
    DECLARE_HIJACK(USFXOnlineComponentUnrealPlayer, GetPlayerNickname);
}

void SFXOnlineHijacks::InstallComponentUnrealPlayer()
{
    MILAN_DEBUGW(L"Installing hijacks for: %s...", L"SFXOnlineComponentUnrealPlayer");
    UClass* const SearchedClass = HijackUtils::FindClass(SFXName(L"SFXOnlineComponentUnrealPlayer", 0));
    using namespace SFXOnlineComponentUnrealPlayer;
    INSTALL_HIJACK(SearchedClass, GetUniquePlayerId);
    INSTALL_HIJACK(SearchedClass, GetPlayerNickname);
}

void SFXOnlineComponentUnrealPlayer::GetPlayerNickname_Hook(USFXOnlineComponentUnrealPlayer* const Context,
    FFrame* const Stack, void* const Result)
{
    GetPlayerNickname_Original(Context, Stack, Result);
    if (Result == nullptr) {
        return;
    }

    // IsInvalidName and SetName both use this nickname when updating the replicated PlayerName
    FString const& ProfileName = MPProfile::GetPlayerName();
    FString const& Name = ProfileName.Empty() ? MPProfile::GetDefaultPlayerName() : ProfileName;
    if (!Name.Empty()) {
        *static_cast<FString*>(Result) = Name;
    }
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
