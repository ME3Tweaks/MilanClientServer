#include <Shlobj.h>
#include <cerrno>
#include <climits>
#include <cwchar>

#include "Base/IOUtils.hpp"
#include "Online/MPProfile.hpp"

// The game has no profile field for the player name, so it is cached here between load and save.
static FString& CachedPlayerName()
{
    static FString PlayerName{};
    return PlayerName;
}
static bool bPlayerNameLoaded = false;

// Save pending online-storage changes together with the last acknowledged server value.
// The original storage uses -1 as GetProfileSettingValue's default value-map ID.
static FString CurrentProfileString(USFXOnlinePlayerStorage* const Storage,
                                    int const SettingId, FString const& ServerString)
{
    FString PendingString{};
    if (Storage != nullptr &&
        Storage->GetProfileSettingValue(SettingId, -1, &PendingString) &&
        !PendingString.Empty())
    {
        return PendingString;
    }
    return ServerString;
}

// The native timestamp serializer stops after three blocks. Encode the remaining
// blocks in MP_Profile, with the same comma-separated integer layout.
static FString ExtendedCompletionTimeString(TArray<int> const& CompletionTimes,
                                            int const FirstIndex)
{
    int LastIndex = FirstIndex + MP_PROFILE_COMPLETION_TIMES_PER_FIELD - 1;
    if (static_cast<UINT>(FirstIndex) >= CompletionTimes.Count())
        return {};
    if (static_cast<UINT>(LastIndex) >= CompletionTimes.Count())
        LastIndex = static_cast<int>(CompletionTimes.Count()) - 1;

    while (LastIndex >= FirstIndex && CompletionTimes(LastIndex) == 0)
        --LastIndex;
    if (LastIndex < FirstIndex)
        return {};

    FString Result{};
    for (int Index = FirstIndex; Index <= LastIndex; ++Index)
    {
        if (Index != FirstIndex)
            Result.Append(L',');
        Result.AppendFormat(L"%d", CompletionTimes(Index));
    }
    return Result;
}

static bool RestoreExtendedCompletionTimes(FString const& Values, int const FirstIndex,
                                           TArray<int>& CompletionTimes)
{
    int Parsed[MP_PROFILE_COMPLETION_TIMES_PER_FIELD]{};
    int ParsedCount = 0;
    wchar_t const* Cursor = Values.Chars();
    while (*Cursor != L'\0')
    {
        if (ParsedCount == MP_PROFILE_COMPLETION_TIMES_PER_FIELD)
            return false;

        wchar_t* End = nullptr;
        errno = 0;
        long const Value = std::wcstol(Cursor, &End, 10);
        if (End == Cursor || errno == ERANGE || Value < INT_MIN || Value > INT_MAX ||
            (*End != L',' && *End != L'\0'))
            return false;

        Parsed[ParsedCount++] = static_cast<int>(Value);
        Cursor = End;
        if (*Cursor == L',')
        {
            ++Cursor;
            if (*Cursor == L'\0')
                return false;
        }
    }

    if (static_cast<UINT>(FirstIndex + ParsedCount) > CompletionTimes.Count())
        CompletionTimes.Resize(FirstIndex + ParsedCount);
    for (int Index = 0; Index < ParsedCount; ++Index)
        CompletionTimes(FirstIndex + Index) = Parsed[Index];
    return true;
}

// ! ProfileSetting implementation.
// ========================================

void ProfileSetting::Deserialize(IInputStream& Stream)
{
    Stream.ReadNumeric<BIG, CHAR>(*(char*)&this->IdType);
    Stream.ReadNumeric<BIG, INT32>(this->Id);
    Stream.ReadNumeric<BIG, CHAR>(*(char*)&this->DataType);

    // Actually not sure about this...
    MILAN_CHECKW(this->IdType == this->DataType, L"");

    switch (this->DataType)
    {
    case PST_INT:
        Stream.ReadNumeric<BIG, INT>(this->CaseInt);
        break;
    case PST_STRING:
        Stream.ReadString(this->CaseString);
        break;
    default:
        MILAN_FATALW(L"unsupported setting type: %d", this->DataType);
    }

    char ExtraTerminator = 0;
    Stream.ReadNumeric<BIG, CHAR>(ExtraTerminator);

    MILAN_VERIFYW(ExtraTerminator == 0, L"extra terminator not zero: %d", ExtraTerminator);
}

void ProfileSetting::Serialize(IOutputStream& Stream) const
{
    Stream.WriteNumeric<BIG, CHAR>(this->IdType);
    Stream.WriteNumeric<BIG, INT32>(this->Id);
    Stream.WriteNumeric<BIG, CHAR>(this->DataType);

    switch (this->DataType)
    {
    case PST_INT:
        Stream.WriteNumeric<BIG, INT>(this->CaseInt);
        break;
    case PST_STRING:
        Stream.WriteString(this->CaseString);
        break;
    default:
        MILAN_FATALW(L"unsupported setting type: %d", this->DataType);
    }

    Stream.WriteNumeric<BIG, CHAR>(0);
}

ProfileSetting ProfileSetting::Construct(int const InId, int& InValue)
{
    return ProfileSetting {
        .Id = InId,
        .IdType = PST_INT,
        .DataType = PST_INT,
        .CaseString = {},
        .CaseInt = InValue,
    };
}

ProfileSetting ProfileSetting::Construct(int const InId, FString& InValue)
{
    return ProfileSetting {
        .Id = InId,
        .IdType = PST_STRING,
        .DataType = PST_STRING,
        .CaseString = InValue,
        .CaseInt = {},
    };
}

ProfileSetting ProfileSetting::Construct(int const InId, FString const& InValue)
{
    return ProfileSetting {
        .Id = InId,
        .IdType = PST_STRING,
        .DataType = PST_STRING,
        .CaseString = InValue,
        .CaseInt = {},
    };
}


// ! MPProfile implementation.
// ========================================

static FString GetSharedProfilePath()
{
    FString ProfilePath{};
    ProfilePath.AppendFormat(L"%s\\%s", ::GetLMPSaveFolder().c_str(), L"MP_Profile");
    return ProfilePath;
}

// Each game rewrites the whole profile from memory, so concurrent instances sharing
// one file overwrite each other's saves. Extra instances write their own file.
FString MPProfile::GetProfilePathStatic()
{
    FString ProfilePath = GetSharedProfilePath();
    int const Ordinal = ::GetGameInstanceOrdinal();
    if (Ordinal > 0)
        ProfilePath.AppendFormat(L"-%d", Ordinal);
    return ProfilePath;
}

// Extra instances start from the shared profile until they have saved their own.
FString MPProfile::GetLoadProfilePathStatic()
{
    FString ProfilePath = GetProfilePathStatic();
    if (::GetGameInstanceOrdinal() > 0 &&
        ::GetFileAttributesW(*ProfilePath) == INVALID_FILE_ATTRIBUTES)
        return GetSharedProfilePath();
    return ProfilePath;
}

FString const& MPProfile::GetProfilePath() const noexcept
{
    return ProfilePath;
}

FString const& MPProfile::GetPlayerName()
{
    if (bPlayerNameLoaded)
        return CachedPlayerName();
    bPlayerNameLoaded = true;

    ::game::vector<BYTE> ProfileBuffer{};
    if (!::ReadFileFull(static_cast<std::wstring_view>(GetLoadProfilePathStatic()), ProfileBuffer))
        return CachedPlayerName();

    MemoryInputStream ProfileStream{ std::move(ProfileBuffer) };
    int SettingsCount = 0;
    ProfileStream.ReadNumeric<BIG>(SettingsCount);
    for (int i = 0; i < SettingsCount; ++i)
    {
        ProfileSetting Setting{};
        Setting.Deserialize(ProfileStream);
        if (Setting.Id == MP_PROFILE_PLAYER_NAME_ID && Setting.DataType == PST_STRING)
            CachedPlayerName() = Setting.CaseString;
    }
    return CachedPlayerName();
}

void MPProfile::SetPlayerName(FString const& Name)
{
    CachedPlayerName() = Name;
    bPlayerNameLoaded = true;
}

MPProfile::MPProfile(USFXEngine* const Engine)
{
    MILAN_CHECKW(Engine != nullptr, L"");

    SaveManager = Engine->MPSaveManager;
    AccomplishmentManager = Engine->AccomplishmentManager;

    MILAN_CHECKW(SaveManager != nullptr, L"");
    MILAN_CHECKW(AccomplishmentManager != nullptr, L"");

    ProfilePath = GetProfilePathStatic();

    static bool bLoggedProfilePath = false;
    if (!bLoggedProfilePath)
    {
        bLoggedProfilePath = true;
        MILAN_DEBUGW(ELOG_MultiProfile, L"MPProfile resolved path = %s", *ProfilePath);
    }
}

// Allocate all local kit slots even when a player does not yet have an MP_Profile file.
void MPProfile::PrepareCharacterStorage() const
{
    USFXOnlineSaveGameMP* OnlineSave = SaveManager->OnlineSave;
    MILAN_CHECKW(OnlineSave != nullptr, L"");

    if (OnlineSave->ServerCharacterStrings.Count() < MP_PROFILE_CHARACTER_CAPACITY)
    {
        OnlineSave->ServerCharacterStrings.Resize(MP_PROFILE_CHARACTER_CAPACITY);
    }

    static constexpr uint32_t MP_PROFILE_CLASS_CAPACITY = SGF_Class07 - SGF_Class00 + 1;
    if (OnlineSave->ServerClassStrings.Count() < MP_PROFILE_CLASS_CAPACITY)
    {
        OnlineSave->ServerClassStrings.Resize(MP_PROFILE_CLASS_CAPACITY);
    }
}

void MPProfile::Deserialize(IInputStream& Stream)
{
    USFXOnlineSaveGameMP* OnlineSave = SaveManager->OnlineSave;
    USFXAccomplishmentStorage* AccomplishmentStorage = AccomplishmentManager->AccomplishmentStorage;

    MILAN_CHECKW(OnlineSave != nullptr, L"");
    MILAN_CHECKW(AccomplishmentStorage != nullptr, L"");

    int SettingsCount = 0;
    Stream.ReadNumeric<BIG>(SettingsCount);

    MILAN_CHECKW(SettingsCount > 0, L"");

    OnlineSave->ServerClassStrings.Clear();
    OnlineSave->ServerClassStrings.Resize(SGF_Class07 - SGF_Class00 + 1);
    OnlineSave->ServerCharacterStrings.Clear();
    // Missing extension records in a legacy profile become empty kit slots.
    OnlineSave->ServerCharacterStrings.Resize(MP_PROFILE_CHARACTER_CAPACITY);

    CachedPlayerName().Reset();
    bPlayerNameLoaded = true;

    // Apply extended timestamps after native ReadFromStorage fills the first 750.
    FString ExtendedCompletionStrings[MP_PROFILE_EXTENDED_COMPLETION_FIELD_COUNT]{};

    for (int i = 0; i < SettingsCount; ++i)
    {
        ProfileSetting Setting{};
        Setting.Deserialize(Stream);

        if (Setting.Id == SGF_Base)
        {
            MILAN_CHECKW(Setting.DataType == PST_STRING, L"");
            OnlineSave->ServerBaseString = Setting.CaseString;
        }
        else if (Setting.Id >= SGF_Class00 && Setting.Id <= SGF_Class07)
        {
            MILAN_CHECKW(Setting.DataType == PST_STRING, L"");
            OnlineSave->ServerClassStrings(Setting.Id - SGF_Class00) = Setting.CaseString;
        }
        else if (Setting.Id >= SGF_Character00 && Setting.Id <= SGF_Character77)
        {
            MILAN_CHECKW(Setting.DataType == PST_STRING, L"");
            OnlineSave->ServerCharacterStrings(Setting.Id - SGF_Character00) = Setting.CaseString;
        }
        // Extended kits follow the original fields without shifting IDs 87-95.
        else if (Setting.Id >= MP_PROFILE_EXTENDED_CHARACTER_FIRST_ID &&
                 Setting.Id <= MP_PROFILE_EXTENDED_CHARACTER_LAST_ID)
        {
            MILAN_CHECKW(Setting.DataType == PST_STRING, L"");
            int const CharIndex = MP_PROFILE_LEGACY_CHARACTER_COUNT +
                                  Setting.Id - MP_PROFILE_EXTENDED_CHARACTER_FIRST_ID;
            OnlineSave->ServerCharacterStrings(CharIndex) = Setting.CaseString;
        }
        else if (Setting.Id == SGF_FaceCodes)
        {
            MILAN_CHECKW(Setting.DataType == PST_STRING, L"");
            OnlineSave->ServerFaceCodesString = Setting.CaseString;
        }
        else if (Setting.Id == SGF_NewReinforcements)
        {
            MILAN_CHECKW(Setting.DataType == PST_STRING, L"");
            OnlineSave->ServerNewReinforcementsString = Setting.CaseString;
        }
        else if (Setting.Id == ASF_Completion)
        {
            MILAN_CHECKW(Setting.DataType == PST_STRING, L"");
            AccomplishmentStorage->ServerCompletionString = Setting.CaseString;
        }
        else if (Setting.Id == ASF_Progress)
        {
            MILAN_CHECKW(Setting.DataType == PST_STRING, L"");
            AccomplishmentStorage->ServerProgressString = Setting.CaseString;
        }
        else if (Setting.Id == CSF_RewardCompletion)
        {
            MILAN_CHECKW(Setting.DataType == PST_STRING, L"");
            AccomplishmentStorage->ServerRewardLevelString = Setting.CaseString;
        }
        else if (Setting.Id == CSF_CurrentReward)
        {
            MILAN_CHECKW(Setting.DataType == PST_INT, L"");
            AccomplishmentStorage->SetCurrentReward(Setting.CaseInt);
        }
        else if (Setting.Id == CSF_CompletionTimes)
        {
            MILAN_CHECKW(Setting.DataType == PST_STRING, L"");
            AccomplishmentStorage->ServerCompletionTimesString = Setting.CaseString;
        }
        else if (Setting.Id == CSF_CompletionTimes2)
        {
            MILAN_CHECKW(Setting.DataType == PST_STRING, L"");
            AccomplishmentStorage->ServerCompletionTimes2String = Setting.CaseString;
        }
        else if (Setting.Id == CSF_CompletionTimes3)
        {
            MILAN_CHECKW(Setting.DataType == PST_STRING, L"");
            AccomplishmentStorage->ServerCompletionTimes3String = Setting.CaseString;
        }
        else if (Setting.Id >= MP_PROFILE_EXTENDED_COMPLETION_FIRST_ID &&
                 Setting.Id <= MP_PROFILE_EXTENDED_COMPLETION_LAST_ID)
        {
            MILAN_CHECKW(Setting.DataType == PST_STRING, L"");
            ExtendedCompletionStrings[Setting.Id - MP_PROFILE_EXTENDED_COMPLETION_FIRST_ID] =
                Setting.CaseString;
        }
        else if (Setting.Id == MP_PROFILE_PLAYER_NAME_ID)
        {
            MILAN_CHECKW(Setting.DataType == PST_STRING, L"");
            CachedPlayerName() = Setting.CaseString;
        }
        else
        {
            MILAN_WARNW(ELOG_MultiProfile, L"unknown profile setting id = %d", Setting.Id);
        }
    }

    AccomplishmentStorage->ReadFromStorage(AccomplishmentManager);
    // Native SetCompletionTimes reads only the original three fields.
    for (int Field = 0; Field < MP_PROFILE_EXTENDED_COMPLETION_FIELD_COUNT; ++Field)
    {
        FString const& Values = ExtendedCompletionStrings[Field];
        if (Values.Empty())
            continue;

        int const FirstIndex = MP_PROFILE_LEGACY_COMPLETION_TIME_COUNT +
                               Field * MP_PROFILE_COMPLETION_TIMES_PER_FIELD;
        if (!RestoreExtendedCompletionTimes(Values, FirstIndex,
                                            AccomplishmentManager->AccomplishmentCompletionTimes))
            MILAN_WARNW(ELOG_MultiProfile, L"invalid extended completion times in profile setting %d",
                        MP_PROFILE_EXTENDED_COMPLETION_FIRST_ID + Field);
    }
    SaveManager->RefreshMPData();
}

void MPProfile::Serialize(IOutputStream& Stream) const
{
    USFXOnlineSaveGameMP* OnlineSave = SaveManager->OnlineSave;
    USFXAccomplishmentStorage* AccomplishmentStorage = AccomplishmentManager->AccomplishmentStorage;

    MILAN_CHECKW(OnlineSave != nullptr, L"");
    MILAN_CHECKW(AccomplishmentStorage != nullptr, L"");

    int SettingsCount = 0;

    // Write empty count now, update later.
    auto const CountOffset = Stream.Position();
    Stream.WriteNumeric<BIG, INT32>(0);

    for (int i = 0; i < EStorageField_MAX; ++i)
    {
        if (i == SGF_Base)
        {
            FString BaseString = CurrentProfileString(OnlineSave->CachedOnlinePlayerStorage, i, OnlineSave->ServerBaseString);
            BaseString = GetRealBaseString(OnlineSave, BaseString);

            OnlineSave->ServerBaseString = BaseString;
            if (OnlineSave->CachedOnlinePlayerStorage != nullptr)
            {
                OnlineSave->CachedOnlinePlayerStorage->SetProfileSettingValue(i, &BaseString);
            }

            ProfileSetting::Construct(i, BaseString).Serialize(Stream);
            SettingsCount++;
        }
        else if (i >= SGF_Class00 && i <= SGF_Class07)
        {
            auto const ClassIndex = i - SGF_Class00;
            if (static_cast<UINT>(ClassIndex) >= OnlineSave->ServerClassStrings.Count())
                continue;

            FString ClassString = CurrentProfileString(OnlineSave->CachedOnlinePlayerStorage, i, OnlineSave->ServerClassStrings(ClassIndex));
            ClassString = GetRealClassString(OnlineSave, ClassIndex, ClassString);

            if (ClassString.Empty())
                continue;

            OnlineSave->ServerClassStrings(ClassIndex) = ClassString;
            if (OnlineSave->CachedOnlinePlayerStorage != nullptr) [[likely]]
            {
                OnlineSave->CachedOnlinePlayerStorage->SetProfileSettingValue(i, &ClassString);
            }

            ProfileSetting::Construct(i, ClassString).Serialize(Stream);
            SettingsCount++;
        }
        else if (i >= SGF_Character00 && i <= SGF_Character77)
        {
            auto const CharIndex = i - SGF_Character00;
            if (static_cast<UINT>(CharIndex) >= OnlineSave->ServerCharacterStrings.Count())
                continue;

            FString CharString = CurrentProfileString(OnlineSave->CachedOnlinePlayerStorage, i, OnlineSave->ServerCharacterStrings(CharIndex));
            CharString = GetRealCharacterString(OnlineSave, CharIndex, CharString);

            if (CharString.Empty())
                continue;

            OnlineSave->ServerCharacterStrings(CharIndex) = CharString;
            if (OnlineSave->CachedOnlinePlayerStorage != nullptr) [[likely]]
            {
                OnlineSave->CachedOnlinePlayerStorage->SetProfileSettingValue(i, &CharString);
            }

            ProfileSetting::Construct(i, CharString).Serialize(Stream);
            SettingsCount++;
        }
        else if (i == SGF_FaceCodes)
        {
            FString FaceString = CurrentProfileString(OnlineSave->CachedOnlinePlayerStorage, i, OnlineSave->ServerFaceCodesString);
            FaceString = GetRealFacesString(OnlineSave, FaceString);

            OnlineSave->ServerFaceCodesString = FaceString;
            if (OnlineSave->CachedOnlinePlayerStorage != nullptr)
            {
                OnlineSave->CachedOnlinePlayerStorage->SetProfileSettingValue(i, &FaceString);
            }

            ProfileSetting::Construct(i, FaceString).Serialize(Stream);
            SettingsCount++;
        }
        else if (i == SGF_NewReinforcements)
        {
            FString ReinforcementString = CurrentProfileString(OnlineSave->CachedOnlinePlayerStorage, i, OnlineSave->ServerNewReinforcementsString);
            ReinforcementString = GetRealReinforcementsString(OnlineSave, ReinforcementString);

            OnlineSave->ServerNewReinforcementsString = ReinforcementString;
            if (OnlineSave->CachedOnlinePlayerStorage != nullptr)
            {
                OnlineSave->CachedOnlinePlayerStorage->SetProfileSettingValue(i, &ReinforcementString);
            }

            ProfileSetting::Construct(i, ReinforcementString).Serialize(Stream);
            SettingsCount++;
        }
        else if (i == ASF_Completion)
        {
            FString CompletionString = CurrentProfileString(
                AccomplishmentStorage->CachedOnlinePlayerStorage, i,
                AccomplishmentStorage->ServerCompletionString);
            CompletionString = GetRealCompletionString(AccomplishmentStorage, CompletionString);

            AccomplishmentStorage->ServerCompletionString = CompletionString;
            if (AccomplishmentStorage->CachedOnlinePlayerStorage != nullptr)
            {
                AccomplishmentStorage->CachedOnlinePlayerStorage->SetProfileSettingValue(i, &CompletionString);
            }

            ProfileSetting::Construct(i, CompletionString).Serialize(Stream);
            SettingsCount++;
        }
        else if (i == ASF_Progress)
        {
            FString ProgressString = CurrentProfileString(
                AccomplishmentStorage->CachedOnlinePlayerStorage, i,
                AccomplishmentStorage->ServerProgressString);
            ProgressString = GetRealProgressString(AccomplishmentStorage, ProgressString);

            AccomplishmentStorage->ServerProgressString = ProgressString;
            if (AccomplishmentStorage->CachedOnlinePlayerStorage != nullptr)
            {
                AccomplishmentStorage->CachedOnlinePlayerStorage->SetProfileSettingValue(i, &ProgressString);
            }

            ProfileSetting::Construct(i, ProgressString).Serialize(Stream);
            SettingsCount++;
        }
        else if (i == CSF_RewardCompletion)
        {
            FString RewardLevelString = CurrentProfileString(
                AccomplishmentStorage->CachedOnlinePlayerStorage, i,
                AccomplishmentStorage->ServerRewardLevelString);
            RewardLevelString = GetRealRewardLevelsString(AccomplishmentStorage, RewardLevelString);

            AccomplishmentStorage->ServerRewardLevelString = RewardLevelString;
            if (AccomplishmentStorage->CachedOnlinePlayerStorage != nullptr)
            {
                AccomplishmentStorage->CachedOnlinePlayerStorage->SetProfileSettingValue(i, &RewardLevelString);
            }

            ProfileSetting::Construct(i, RewardLevelString).Serialize(Stream);
            SettingsCount++;
        }
        else if (i == CSF_CurrentReward)
        {
            // Mgamerz:  I don't think levels are actually used. If they were,
            //   it was for the timed operation missions that had tiers for banners.

            int Reward = 0, RewardLevel = 0;
            AccomplishmentStorage->GetCurrentReward(&Reward, &RewardLevel);

            // Uncomment this if banners breaks?.. - d00t

            //if (AccomplishmentStorage->CachedOnlinePlayerStorage != nullptr)
            //{
            //    AccomplishmentStorage->SetCurrentReward(Reward);
            //}

            ProfileSetting::Construct(i, Reward).Serialize(Stream);
            SettingsCount++;
        }
        else if (i == CSF_CompletionTimes)
        {
            FString CompletionTimesString = CurrentProfileString(
                AccomplishmentStorage->CachedOnlinePlayerStorage, i,
                AccomplishmentStorage->ServerCompletionTimesString);
            CompletionTimesString = GetRealCompletionTimesString(AccomplishmentStorage, 0, CompletionTimesString);

            AccomplishmentStorage->ServerCompletionTimesString = CompletionTimesString;
            if (AccomplishmentStorage->CachedOnlinePlayerStorage != nullptr)
            {
                AccomplishmentStorage->CachedOnlinePlayerStorage->SetProfileSettingValue(i, &CompletionTimesString);
            }

            ProfileSetting::Construct(i, CompletionTimesString).Serialize(Stream);
            SettingsCount++;
        }
        else if (i == CSF_CompletionTimes2)
        {
            FString CompletionTimesString = CurrentProfileString(
                AccomplishmentStorage->CachedOnlinePlayerStorage, i,
                AccomplishmentStorage->ServerCompletionTimes2String);
            CompletionTimesString = GetRealCompletionTimesString(AccomplishmentStorage, 1, CompletionTimesString);

            AccomplishmentStorage->ServerCompletionTimes2String = CompletionTimesString;
            if (AccomplishmentStorage->CachedOnlinePlayerStorage != nullptr)
            {
                AccomplishmentStorage->CachedOnlinePlayerStorage->SetProfileSettingValue(i, &CompletionTimesString);
            }

            ProfileSetting::Construct(i, CompletionTimesString).Serialize(Stream);
            SettingsCount++;
        }
        else if (i == CSF_CompletionTimes3)
        {
            FString CompletionTimesString = CurrentProfileString(
                AccomplishmentStorage->CachedOnlinePlayerStorage, i,
                AccomplishmentStorage->ServerCompletionTimes3String);
            CompletionTimesString = GetRealCompletionTimesString(AccomplishmentStorage, 2, CompletionTimesString);

            AccomplishmentStorage->ServerCompletionTimes3String = CompletionTimesString;
            if (AccomplishmentStorage->CachedOnlinePlayerStorage != nullptr)
            {
                AccomplishmentStorage->CachedOnlinePlayerStorage->SetProfileSettingValue(i, &CompletionTimesString);
            }

            ProfileSetting::Construct(i, CompletionTimesString).Serialize(Stream);
            SettingsCount++;
        }
        else
        {
            // This should never be hit I think.
            MILAN_WARNW(ELOG_MultiProfile, L"invalid profile setting id = %d", i);
        }
    }

    // The profile is sparse: write occupied extension slots and keep legacy IDs unchanged.
    for (int CharIndex = MP_PROFILE_LEGACY_CHARACTER_COUNT;
         CharIndex < MP_PROFILE_CHARACTER_CAPACITY; ++CharIndex)
    {
        if (static_cast<UINT>(CharIndex) >= OnlineSave->ServerCharacterStrings.Count())
            break;

        FString CharString = OnlineSave->ServerCharacterStrings(CharIndex);
        CharString = GetRealCharacterString(OnlineSave, CharIndex, CharString);

        if (CharString.Empty())
            continue;

        OnlineSave->ServerCharacterStrings(CharIndex) = CharString;

        int const SettingId = MP_PROFILE_EXTENDED_CHARACTER_FIRST_ID + CharIndex - MP_PROFILE_LEGACY_CHARACTER_COUNT;
        ProfileSetting::Construct(SettingId, CharString).Serialize(Stream);
        SettingsCount++;
    }

    // Store only timestamp blocks that contain a completed accomplishment.
    for (int Field = 0; Field < MP_PROFILE_EXTENDED_COMPLETION_FIELD_COUNT; ++Field)
    {
        int const FirstIndex = MP_PROFILE_LEGACY_COMPLETION_TIME_COUNT + Field * MP_PROFILE_COMPLETION_TIMES_PER_FIELD;
        FString const Values = ExtendedCompletionTimeString(AccomplishmentManager->AccomplishmentCompletionTimes, FirstIndex);

        if (Values.Empty())
            continue;

        ProfileSetting::Construct(MP_PROFILE_EXTENDED_COMPLETION_FIRST_ID + Field, Values).Serialize(Stream);
        SettingsCount++;
    }

    // Saves rebuild the file from game memory, so keep the name an external editor wrote.
    FString const& PlayerName = GetPlayerName();
    if (!PlayerName.Empty())
    {
        ProfileSetting::Construct(MP_PROFILE_PLAYER_NAME_ID, PlayerName).Serialize(Stream);
        SettingsCount++;
    }

    Stream.Seek(SeekOrigin::Begin, CountOffset);
    Stream.WriteNumeric<BIG, INT32>(SettingsCount);
}

FString MPProfile::GetRealBaseString(USFXOnlineSaveGameMP* const OnlineSave, FString const& Fallback) const
{
    if (OnlineSave != nullptr && OnlineSave->CachedOnlinePlayerStorage != nullptr)
    {
        USFXSaveManagerMP* Manager = SaveManager;
        FString const RealString = OnlineSave->GetBase2(&Manager);

        if (!RealString.Empty())
        {
            return RealString;
        }
    }

    return Fallback;
}

FString MPProfile::GetRealClassString(USFXOnlineSaveGameMP* const OnlineSave, int const nClassIndex, FString const& Fallback) const
{
    if (OnlineSave == nullptr || OnlineSave->CachedOnlinePlayerStorage == nullptr) { return Fallback; }
    if (nClassIndex < 0 || nClassIndex >= static_cast<int>(SaveManager->Classes.Count())) { return Fallback; }

    USFXMPClassRecord* Record = SaveManager->Classes(nClassIndex);

    if (Record != nullptr)
    {
        FString const RealString = OnlineSave->GetClass2(&Record);

        if (!RealString.Empty())
        {
            //if (Fallback != RealString)
            //{
            //    MILAN_WARNW(ELOG_MultiProfile, L"GetRealClassString - changed for nClassIndex = %d", nClassIndex);
            //}

            return RealString;
        }
    }

    return Fallback;
}

FString MPProfile::GetRealCharacterString(USFXOnlineSaveGameMP* const OnlineSave, int const nCharIndex, FString const& Fallback) const
{
    if (OnlineSave == nullptr || OnlineSave->CachedOnlinePlayerStorage == nullptr) { return Fallback; }
    if (nCharIndex < 0 || nCharIndex >= static_cast<int>(SaveManager->Characters.Count())) { return Fallback; }

    USFXMPCharacterRecord* Record = SaveManager->Characters(nCharIndex);

    if (Record != nullptr)
    {
        USFXSaveManagerMP* Manager = SaveManager;
        FString const RealString = OnlineSave->GetCharacter2(nCharIndex, &Record, &Manager);

        if (!RealString.Empty())
        {
            //if (Fallback != RealString)
            //{
            //    MILAN_WARNW(ELOG_MultiProfile, L"GetRealCharacterString - changed for nCharIndex = %d", nCharIndex);
            //}

            return RealString;
        }
    }

    return Fallback;
}

FString MPProfile::GetRealFacesString(USFXOnlineSaveGameMP* const OnlineSave, FString const& Fallback) const
{
    if (OnlineSave != nullptr && OnlineSave->CachedOnlinePlayerStorage != nullptr)
    {
        USFXSaveManagerMP* Manager = SaveManager;
        FString const RealString = OnlineSave->GetFaceCodes(&Manager);

        if (!RealString.Empty())
        {
            return RealString;
        }
    }

    return Fallback;
}

FString MPProfile::GetRealReinforcementsString(USFXOnlineSaveGameMP* const OnlineSave, FString const& Fallback) const
{
    if (OnlineSave != nullptr && OnlineSave->CachedOnlinePlayerStorage != nullptr)
    {
        USFXSaveManagerMP* Manager = SaveManager;
        FString const RealString = OnlineSave->GetNewReinforcements(&Manager);

        if (!RealString.Empty())
        {
            return RealString;
        }
    }

    return Fallback;
}

FString MPProfile::GetRealCompletionString(USFXAccomplishmentStorage* const AccomplishmentStorage, FString const& Fallback) const
{
    if (AccomplishmentStorage != nullptr && AccomplishmentStorage->CachedOnlinePlayerStorage != nullptr)
    {
        FString const RealString = AccomplishmentStorage->GetCompletion(&AccomplishmentManager->AccomplishmentIsComplete);

        if (!RealString.Empty())
        {
            return RealString;
        }
    }

    return Fallback;
}

FString MPProfile::GetRealProgressString(USFXAccomplishmentStorage* const AccomplishmentStorage, FString const& Fallback) const
{
    if (AccomplishmentStorage != nullptr && AccomplishmentStorage->CachedOnlinePlayerStorage != nullptr)
    {
        FString const RealString = AccomplishmentStorage->GetProgress();

        if (!RealString.Empty())
        {
            return RealString;
        }
    }

    return Fallback;
}

FString MPProfile::GetRealRewardLevelsString(USFXAccomplishmentStorage* const AccomplishmentStorage, FString const& Fallback) const
{
    if (AccomplishmentStorage != nullptr && AccomplishmentStorage->CachedOnlinePlayerStorage != nullptr)
    {
        FString const RealString = AccomplishmentStorage->GetRewardLevels();

        if (!RealString.Empty())
        {
            return RealString;
        }
    }

    return Fallback;
}

FString MPProfile::GetRealCompletionTimesString(USFXAccomplishmentStorage* const AccomplishmentStorage, int const nIndex, FString const& Fallback) const
{
    if (AccomplishmentStorage != nullptr && AccomplishmentStorage->CachedOnlinePlayerStorage != nullptr)
    {
        TArray<FString> CompletionStrings{};
        AccomplishmentStorage->GetCompletionTimes(&CompletionStrings, &AccomplishmentManager->AccomplishmentCompletionTimes);

        if (nIndex >= 0 && nIndex < static_cast<int>(CompletionStrings.Count()) && !CompletionStrings(nIndex).Empty())
        {
            return CompletionStrings(nIndex);
        }
    }

    return Fallback;
}
