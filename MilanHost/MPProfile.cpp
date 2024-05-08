#include <Shlobj.h>
#include "MPProfile.hpp"


// ! ProfileSetting implementation.
// ========================================

void ProfileSetting::Deserialize(IInputStream& Stream)
{
    Stream.ReadNumeric<BIG>(*(char*)&this->IdType);
    Stream.ReadNumeric<BIG>(this->Id);
    Stream.ReadNumeric<BIG>(*(char*)&this->DataType);

    // Actually not sure about this...
    MILAN_CHECKW(this->IdType == this->DataType, L"");

    switch (this->DataType)
    {
    case PST_INT:
        Stream.ReadNumeric<BIG>(this->CaseInt);
        break;
    case PST_STRING:
        Stream.ReadString(this->CaseString);
        break;
    default:
        MILAN_FATALW(L"unsupported setting type: %d", this->DataType);
    }

    char ExtraTerminator = 0;
    Stream.ReadNumeric<BIG>(ExtraTerminator);

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


// ! MPProfile implementation.
// ========================================

wchar_t const* MPProfile::GetProfilePath()
{
    // This will leak memory.
    static bool Initialized = false;
    static FStringView ProfilePath{};

    if (!Initialized)
    {
        WCHAR* DocumentPath = nullptr;
        ::SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, NULL, &DocumentPath);
        ProfilePath.AppendFormat(L"%s\\BioWare\\Mass Effect Legendary Edition\\Save\\ME3\\%s", DocumentPath, L"MP_Profile");
        ::CoTaskMemFree(DocumentPath);
    }

    return *ProfilePath;
}

MPProfile::MPProfile(USFXEngine* const Engine)
{
    MILAN_CHECKW(Engine != nullptr, L"");

    SaveManager = Engine->MPSaveManager;
    AccomplishmentManager = Engine->AccomplishmentManager;

    MILAN_CHECKW(SaveManager != nullptr, L"");
    MILAN_CHECKW(AccomplishmentManager != nullptr, L"");
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
    OnlineSave->ServerCharacterStrings.Resize(SGF_Character77 - SGF_Character00 + 1);

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
    }

    AccomplishmentStorage->ReadFromStorage(AccomplishmentManager);
    SaveManager->RefreshMPData();
}

void MPProfile::Serialize(IOutputStream& Stream) const
{
    USFXOnlineSaveGameMP* OnlineSave = SaveManager->OnlineSave;
    USFXAccomplishmentStorage* AccomplishmentStorage = AccomplishmentManager->AccomplishmentStorage;

    MILAN_CHECKW(OnlineSave != nullptr, L"");
    MILAN_CHECKW(AccomplishmentStorage != nullptr, L"");

    MILAN_UNUSED(Stream);
}
