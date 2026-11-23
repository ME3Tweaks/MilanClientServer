/**
 * @file        MilanHost/Online/MPProfile.hpp
 * @brief       Handling of the multiplayer player profile.
 */

#pragma once

#include <string_view>

#include <LESDK/Headers.hpp>

#include "Base/Base.hpp"
#include "Base/Streams.hpp"


enum EProfileSettingType : char
{
    PST_NONE = 0,
    PST_INT = 1,
    PST_INT64 = 2,  // Serialized as 32 bits.
    PST_DOUBLE = 3,
    PST_STRING = 4,
    PST_FLOAT = 5,
    PST_BLOB = 6,
    PST_DATETIME = 7,
};

enum EStorageField : int
{
    SGF_Base = 0,
    SGF_Class00 = 1,
    SGF_Class01 = 2,
    SGF_Class02 = 3,
    SGF_Class03 = 4,
    SGF_Class04 = 5,
    SGF_Class05 = 6,
    SGF_Class06 = 7,
    SGF_Class07 = 8,
    SGF_Character00 = 9,
    SGF_Character01 = 10,
    SGF_Character02 = 11,
    SGF_Character03 = 12,
    SGF_Character04 = 13,
    SGF_Character05 = 14,
    SGF_Character06 = 15,
    SGF_Character07 = 16,
    SGF_Character08 = 17,
    SGF_Character09 = 18,
    SGF_Character10 = 19,
    SGF_Character11 = 20,
    SGF_Character12 = 21,
    SGF_Character13 = 22,
    SGF_Character14 = 23,
    SGF_Character15 = 24,
    SGF_Character16 = 25,
    SGF_Character17 = 26,
    SGF_Character18 = 27,
    SGF_Character19 = 28,
    SGF_Character20 = 29,
    SGF_Character21 = 30,
    SGF_Character22 = 31,
    SGF_Character23 = 32,
    SGF_Character24 = 33,
    SGF_Character25 = 34,
    SGF_Character26 = 35,
    SGF_Character27 = 36,
    SGF_Character28 = 37,
    SGF_Character29 = 38,
    SGF_Character30 = 39,
    SGF_Character31 = 40,
    SGF_Character32 = 41,
    SGF_Character33 = 42,
    SGF_Character34 = 43,
    SGF_Character35 = 44,
    SGF_Character36 = 45,
    SGF_Character37 = 46,
    SGF_Character38 = 47,
    SGF_Character39 = 48,
    SGF_Character40 = 49,
    SGF_Character41 = 50,
    SGF_Character42 = 51,
    SGF_Character43 = 52,
    SGF_Character44 = 53,
    SGF_Character45 = 54,
    SGF_Character46 = 55,
    SGF_Character47 = 56,
    SGF_Character48 = 57,
    SGF_Character49 = 58,
    SGF_Character50 = 59,
    SGF_Character51 = 60,
    SGF_Character52 = 61,
    SGF_Character53 = 62,
    SGF_Character54 = 63,
    SGF_Character55 = 64,
    SGF_Character56 = 65,
    SGF_Character57 = 66,
    SGF_Character58 = 67,
    SGF_Character59 = 68,
    SGF_Character60 = 69,
    SGF_Character61 = 70,
    SGF_Character62 = 71,
    SGF_Character63 = 72,
    SGF_Character64 = 73,
    SGF_Character65 = 74,
    SGF_Character66 = 75,
    SGF_Character67 = 76,
    SGF_Character68 = 77,
    SGF_Character69 = 78,
    SGF_Character70 = 79,
    SGF_Character71 = 80,
    SGF_Character72 = 81,
    SGF_Character73 = 82,
    SGF_Character74 = 83,
    SGF_Character75 = 84,
    SGF_Character76 = 85,
    SGF_Character77 = 86,
    SGF_FaceCodes = 87,
    SGF_NewReinforcements = 88,
    ASF_Completion = 89,
    ASF_Progress = 90,

    CSF_RewardCompletion = 91,
    CSF_CurrentReward = 92,
    CSF_CompletionTimes = 93,
    CSF_CompletionTimes2 = 94,
    CSF_CompletionTimes3 = 95,

    EStorageField_MAX = 96,
};

// Keep the original profile setting IDs for 78 kits and reserve IDs 96-217 for kits 78-199.
// Match this capacity in SFXOnlineSaveGameMP/UpdateCharacterValues2.uc.
inline constexpr int MP_PROFILE_CHARACTER_CAPACITY = 200;
inline constexpr int MP_PROFILE_LEGACY_CHARACTER_COUNT = SGF_Character77 - SGF_Character00 + 1;
inline constexpr int MP_PROFILE_EXTENDED_CHARACTER_FIRST_ID = 96;
inline constexpr int MP_PROFILE_EXTENDED_CHARACTER_LAST_ID =
    MP_PROFILE_EXTENDED_CHARACTER_FIRST_ID + MP_PROFILE_CHARACTER_CAPACITY - MP_PROFILE_LEGACY_CHARACTER_COUNT - 1;
static_assert(MP_PROFILE_EXTENDED_CHARACTER_FIRST_ID >= EStorageField_MAX);

// Native accomplishment storage only exposes three 250-value timestamp strings.
// The local profile holds another three strings without changing the legacy field IDs.
inline constexpr int MP_PROFILE_COMPLETION_TIMES_PER_FIELD = 250;
inline constexpr int MP_PROFILE_LEGACY_COMPLETION_TIME_COUNT =
    3 * MP_PROFILE_COMPLETION_TIMES_PER_FIELD;
inline constexpr int MP_PROFILE_COMPLETION_TIME_CAPACITY = 1500;
// IDs 96-1023 are reserved for kit records, allowing 1,006 total kit slots
// without changing timestamp field IDs in an existing profile.
inline constexpr int MP_PROFILE_EXTENDED_COMPLETION_FIRST_ID = 1024;
inline constexpr int MP_PROFILE_EXTENDED_COMPLETION_FIELD_COUNT =
    (MP_PROFILE_COMPLETION_TIME_CAPACITY - MP_PROFILE_LEGACY_COMPLETION_TIME_COUNT) /
    MP_PROFILE_COMPLETION_TIMES_PER_FIELD;
inline constexpr int MP_PROFILE_EXTENDED_COMPLETION_LAST_ID =
    MP_PROFILE_EXTENDED_COMPLETION_FIRST_ID + MP_PROFILE_EXTENDED_COMPLETION_FIELD_COUNT - 1;
static_assert(MP_PROFILE_COMPLETION_TIME_CAPACITY % MP_PROFILE_COMPLETION_TIMES_PER_FIELD == 0);
static_assert(MP_PROFILE_EXTENDED_COMPLETION_FIRST_ID > MP_PROFILE_EXTENDED_CHARACTER_LAST_ID);

// MilanHost-only settings with no game storage start at 4096.
// The player name replaces the offline "Player" placeholder before the Windows account name.
inline constexpr int MP_PROFILE_PLAYER_NAME_ID = 4096;
static_assert(MP_PROFILE_PLAYER_NAME_ID > MP_PROFILE_EXTENDED_COMPLETION_LAST_ID);


struct ProfileSetting final : public NonCopyable
{
    int                     Id;
    EProfileSettingType     IdType;
    EProfileSettingType     DataType;

    FString                 CaseString;
    int                     CaseInt;

    void Deserialize(IInputStream& Stream);
    void Serialize(IOutputStream& Stream) const;

    static ProfileSetting Construct(int InId, int& InValue);
    static ProfileSetting Construct(int InId, FString& InValue);
    static ProfileSetting Construct(int InId, FString const& InValue);
};


class MPProfile final : public NonCopyable
{
    USFXSaveManagerMP* SaveManager{ nullptr };
    USFXAccomplishmentManager* AccomplishmentManager{ nullptr };
    FString ProfilePath{};

public:

    MPProfile(USFXEngine* Engine);

    void PrepareCharacterStorage() const;
    void Deserialize(IInputStream& Stream);
    void Serialize(IOutputStream& Stream) const;

    FString GetRealBaseString(USFXOnlineSaveGameMP* OnlineSave, FString const& Fallback) const;
    FString GetRealClassString(USFXOnlineSaveGameMP* OnlineSave, int nClassIndex, FString const& Fallback) const;
    FString GetRealCharacterString(USFXOnlineSaveGameMP* OnlineSave, int nCharIndex, FString const& Fallback) const;
    FString GetRealFacesString(USFXOnlineSaveGameMP* OnlineSave, FString const& Fallback) const;
    FString GetRealReinforcementsString(USFXOnlineSaveGameMP* OnlineSave, FString const& Fallback) const;

    FString GetRealCompletionString(USFXAccomplishmentStorage* AccomplishmentStorage, FString const& Fallback) const;
    FString GetRealProgressString(USFXAccomplishmentStorage* AccomplishmentStorage, FString const& Fallback) const;
    FString GetRealRewardLevelsString(USFXAccomplishmentStorage* AccomplishmentStorage, FString const& Fallback) const;
    FString GetRealCompletionTimesString(USFXAccomplishmentStorage* AccomplishmentStorage, int nIndex, FString const& Fallback) const;

    // Where this game instance saves: MP_Profile, or MP_Profile-<ordinal> for extra instances.
    static FString GetProfilePathStatic();
    // Where this game instance loads from: its own file, or MP_Profile before its first save.
    static FString GetLoadProfilePathStatic();
    FString const& GetProfilePath() const noexcept;

    // Reads MP_Profile directly if SFXSaveManagerMP.Initialize has not loaded it yet. Sanitized, including older saved names
    static FString const& GetPlayerName();
    // Windows account name used when the profile has no custom name. Sanitized
    static FString const& GetDefaultPlayerName();
    // The name the next Serialize writes. Sanitized before it is stored
    static void SetPlayerName(FString const& Name);
    // Applies PlayerNames::Policy::SanitizePlayerName. Empty if nothing usable is left
    static FString SanitizePlayerName(std::wstring_view Name);

    static bool IsProfileLoaded();
    static void MarkProfileLoaded();
};
