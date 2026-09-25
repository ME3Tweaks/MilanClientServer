#include <Shlobj.h>
#include <cerrno>
#include <climits>
#include <cwchar>

#include "Online/MPProfile.hpp"

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

FString MPProfile::GetProfilePathStatic()
{
    FString ProfilePath{};
    WCHAR* DocumentPath = nullptr;

    ::SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, NULL, &DocumentPath);
    ProfilePath.AppendFormat(L"%s\\BioWare\\Mass Effect Legendary Edition\\Save\\LMP\\%s", DocumentPath, L"MP_Profile");

    ::CoTaskMemFree(DocumentPath);
    return *ProfilePath;
}

FString const& MPProfile::GetProfilePath() const noexcept
{
    return ProfilePath;
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
        OnlineSave->ServerCharacterStrings.Resize(MP_PROFILE_CHARACTER_CAPACITY);
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
            ProfileSetting::Construct(i, CurrentProfileString(
                OnlineSave->CachedOnlinePlayerStorage, i, OnlineSave->ServerBaseString)).Serialize(Stream);
            SettingsCount++;
        }
        else if (i >= SGF_Class00 && i <= SGF_Class07)
        {
            auto const ClassIndex = i - SGF_Class00;
            if (static_cast<UINT>(ClassIndex) >= OnlineSave->ServerClassStrings.Count())
                continue;

            FString ClassString = CurrentProfileString(
                OnlineSave->CachedOnlinePlayerStorage, i, OnlineSave->ServerClassStrings(ClassIndex));
            if (ClassString.Empty())
                continue;

            ProfileSetting::Construct(i, ClassString).Serialize(Stream);
            SettingsCount++;
        }
        else if (i >= SGF_Character00 && i <= SGF_Character77)
        {
            auto const CharIndex = i - SGF_Character00;
            if (static_cast<UINT>(CharIndex) >= OnlineSave->ServerCharacterStrings.Count())
                continue;

            FString CharString = CurrentProfileString(
                OnlineSave->CachedOnlinePlayerStorage, i, OnlineSave->ServerCharacterStrings(CharIndex));
            if (CharString.Empty())
                continue;

            ProfileSetting::Construct(i, CharString).Serialize(Stream);
            SettingsCount++;
        }
        else if (i == SGF_FaceCodes)
        {
            ProfileSetting::Construct(i, CurrentProfileString(
                OnlineSave->CachedOnlinePlayerStorage, i, OnlineSave->ServerFaceCodesString)).Serialize(Stream);
            SettingsCount++;
        }
        else if (i == SGF_NewReinforcements)
        {
            ProfileSetting::Construct(i, CurrentProfileString(
                OnlineSave->CachedOnlinePlayerStorage, i, OnlineSave->ServerNewReinforcementsString)).Serialize(Stream);
            SettingsCount++;
        }
        else if (i == ASF_Completion)
        {
            ProfileSetting::Construct(i, CurrentProfileString(
                AccomplishmentStorage->CachedOnlinePlayerStorage, i,
                AccomplishmentStorage->ServerCompletionString)).Serialize(Stream);
            SettingsCount++;
        }
        else if (i == ASF_Progress)
        {
            ProfileSetting::Construct(i, CurrentProfileString(
                AccomplishmentStorage->CachedOnlinePlayerStorage, i,
                AccomplishmentStorage->ServerProgressString)).Serialize(Stream);
            SettingsCount++;
        }
        else if (i == CSF_RewardCompletion)
        {
            ProfileSetting::Construct(i, CurrentProfileString(
                AccomplishmentStorage->CachedOnlinePlayerStorage, i,
                AccomplishmentStorage->ServerRewardLevelString)).Serialize(Stream);
            SettingsCount++;
        }
        else if (i == CSF_CurrentReward)
        {
            // Mgamerz:  I don't think levels are actually used. If they were,
            //   it was for the timed operation missions that had tiers for banners.

            int Reward = 0, RewardLevel = 0;
            AccomplishmentStorage->GetCurrentReward(&Reward, &RewardLevel);

            ProfileSetting::Construct(i, Reward).Serialize(Stream);
            SettingsCount++;
        }
        else if (i == CSF_CompletionTimes)
        {
            ProfileSetting::Construct(i, CurrentProfileString(
                AccomplishmentStorage->CachedOnlinePlayerStorage, i,
                AccomplishmentStorage->ServerCompletionTimesString)).Serialize(Stream);
            SettingsCount++;
        }
        else if (i == CSF_CompletionTimes2)
        {
            ProfileSetting::Construct(i, CurrentProfileString(
                AccomplishmentStorage->CachedOnlinePlayerStorage, i,
                AccomplishmentStorage->ServerCompletionTimes2String)).Serialize(Stream);
            SettingsCount++;
        }
        else if (i == CSF_CompletionTimes3)
        {
            ProfileSetting::Construct(i, CurrentProfileString(
                AccomplishmentStorage->CachedOnlinePlayerStorage, i,
                AccomplishmentStorage->ServerCompletionTimes3String)).Serialize(Stream);
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

        auto const& CharString = OnlineSave->ServerCharacterStrings(CharIndex);
        if (CharString.Empty())
            continue;

        int const SettingId = MP_PROFILE_EXTENDED_CHARACTER_FIRST_ID +
                              CharIndex - MP_PROFILE_LEGACY_CHARACTER_COUNT;
        ProfileSetting::Construct(SettingId, CharString).Serialize(Stream);
        SettingsCount++;
    }

    // Store only timestamp blocks that contain a completed accomplishment.
    for (int Field = 0; Field < MP_PROFILE_EXTENDED_COMPLETION_FIELD_COUNT; ++Field)
    {
        int const FirstIndex = MP_PROFILE_LEGACY_COMPLETION_TIME_COUNT +
                               Field * MP_PROFILE_COMPLETION_TIMES_PER_FIELD;
        FString const Values = ExtendedCompletionTimeString(
            AccomplishmentManager->AccomplishmentCompletionTimes, FirstIndex);
        if (Values.Empty())
            continue;

        ProfileSetting::Construct(MP_PROFILE_EXTENDED_COMPLETION_FIRST_ID + Field,
                                  Values).Serialize(Stream);
        SettingsCount++;
    }

    Stream.Seek(SeekOrigin::Begin, CountOffset);
    Stream.WriteNumeric<BIG, INT32>(SettingsCount);
}
