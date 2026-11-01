#include <cfloat>
#include <cstring>
#include <cwchar>
#include <string>

#include "Base/Logger.hpp"
#include "Components/CCommands.hpp"
#include "Engine/DLCConfigReload.hpp"
#include "Engine/Hooks.hpp"


// RVAs
#define GLOBAL_GCONTENTMANAGER_RVA                      ::LESDK::Address::FromOffset(0x17D5630)
#define GLOBAL_GFILEMANAGER_RVA                         ::LESDK::Address::FromOffset(0x17CD5D0)
// Removes config file layer
#define GLOBAL_FCONFIGCACHEINI_REMOVECONFIGFILE_RVA     ::LESDK::Address::FromOffset(0x9F3E0)
// Reloads config and applies
#define GLOBAL_FCONFIGCACHEINI_RELOADQUEUEDCLASSES_RVA  ::LESDK::Address::FromOffset(0x9B550)
// Loads file from disk to array
#define GLOBAL_APPLOADFILETOARRAY_RVA                   ::LESDK::Address::FromOffset(0xAFD100)


namespace DLCConfigReload
{
    // Creates a config layer
    // FConfigCacheIni::CreateConfigFile(GConfig, RawFileBytes, DisplayName, Key, bQueueClassReload)
    using t_CreateConfigFile = void* (void* Config, void* RawFileBytes, wchar_t const* DisplayName, unsigned long long Key, int bQueueReload);

    // Removes a config layer
    // FConfigCacheIni::RemoveConfigFile(GConfig, Layer, bQueueClassReload)
    using t_RemoveConfigFile = void(void* Config, void* Layer, int bQueueReload);

    // Reloads config
    using t_ReloadQueuedClasses = void(void* Config, float Budget);

    // appLoadFileToArray(TArray<BYTE>&, Path)
    using t_LoadFileToArray = int(void* OutBytes, wchar_t const* Path);

    // TOC lookup
    // FFileManager::GetFilePath(this, FileName, OutPath), vtable +0xE8
    using t_GetFilePath = int(void* FileManager, wchar_t const* FileName, FString* OutPath);
    static constexpr size_t k_getFilePathVtableOffset = 0xE8;

    static void** gp_config = nullptr;
    static void** gp_contentManager = nullptr;
    static void** gp_fileManager = nullptr;
    static t_CreateConfigFile* CreateConfigFile = nullptr;
    static t_RemoveConfigFile* RemoveConfigFile = nullptr;
    static t_ReloadQueuedClasses* ReloadQueuedClasses = nullptr;
    static t_LoadFileToArray* LoadFileToArray = nullptr;

    // Content manager: TArray<SFXAdditionalContent*> at +0x18 (AdditionalContent_MountContentForMode).
    static constexpr size_t k_managerContentsOffset = 0x18;

    // SFXAdditionalContent fields
    static constexpr size_t k_contentMountPriorityOffset = 0x08;    // Mount Priority
    static constexpr size_t k_contentFolderNameOffset = 0x64;       // DLC folder path, e.g. "..\..\BIOGame\DLC\DLC_MOD_Foo\"
    static constexpr size_t k_contentConfigBytesOffset = 0x84;      // TArray<BYTE>, the whole .bin
    static constexpr size_t k_contentFlagsOffset = 0xC8;            // DWORD
    static constexpr size_t k_contentConfigLayerOffset = 0xD4;      // FConfigFile*, null when unmounted

    static constexpr unsigned k_flagHasConfigFile = 0x10;   // Config has been found
    static constexpr unsigned k_flagWanted = 0x20;          // Should be mounted in the current mode.
    static constexpr unsigned k_flagConfigMounted = 0x40;   // Layer is in GConfig.

    // FConfigFile keeps its parsed representation at +0x28
    static constexpr size_t k_layerParsedDataOffset = 0x28;

    // The low half of every DLC layer's sort key
    static constexpr unsigned long long k_dlcLayerKeyLow = 2ull;

    template<typename T>
    static T& Field(void* const Object, size_t const Offset)
    {
        return *reinterpret_cast<T*>(reinterpret_cast<BYTE*>(Object) + Offset);
    }

    static wchar_t const* FolderName(void* const Content)
    {
        return Field<FString>(Content, k_contentFolderNameOffset).Chars();
    }

    // The folder field is a relative path ("..\..\BIOGame\DLC\DLC_MOD_Foo\"). Like the engine's
    // SFXAdditionalContent::GetDLCName, drop trailing separators and keep the last segment.
    static std::wstring DLCName(void* const Content)
    {
        std::wstring Path = FolderName(Content);
        while (!Path.empty() && (Path.back() == L'\\' || Path.back() == L'/'))
        {
            Path.pop_back();
        }
        size_t const Separator = Path.find_last_of(L"\\/");
        return Separator == std::wstring::npos ? Path : Path.substr(Separator + 1);
    }

    static bool IsReady()
    {
        return gp_config != nullptr && *gp_config != nullptr
            && gp_contentManager != nullptr && *gp_contentManager != nullptr
            && gp_fileManager != nullptr && *gp_fileManager != nullptr
            && CreateConfigFile != nullptr && RemoveConfigFile != nullptr
            && ReloadQueuedClasses != nullptr && LoadFileToArray != nullptr;
    }

    static TArray<void**> const& Contents()
    {
        return Field<TArray<void**>>(*gp_contentManager, k_managerContentsOffset);
    }

    static void* FindContent(wchar_t const* const Name)
    {
        TArray<void**> const& List = Contents();
        for (UINT i = 0; i < List.Count(); ++i)
        {
            void* const Content = List.GetData()[i];
            if (Content != nullptr && _wcsicmp(DLCName(Content).c_str(), Name) == 0) {
                return Content;
            }
        }
        return nullptr;
    }

    static void LogContents()
    {
        TArray<void**> const& List = Contents();
        MILAN_INFOW(ELOG_DLCConfig, L"%d DLC folder(s) discovered:", List.Count());
        for (UINT i = 0; i < List.Count(); ++i)
        {
            void* const Content = List.GetData()[i];
            if (Content == nullptr)
            {
                continue;
            }

            unsigned const Flags = Field<unsigned>(Content, k_contentFlagsOffset);
            [[maybe_unused]] wchar_t const* const State = (Flags & k_flagConfigMounted) != 0 ? L"config mounted"
                : (Flags & k_flagHasConfigFile) != 0 ? L"config not mounted in this mode"
                : L"no config file";
            MILAN_INFOW(ELOG_DLCConfig, L"  %s (priority %d, %s)", DLCName(Content).c_str(),
                Field<int>(Content, k_contentMountPriorityOffset), State);
        }
    }

    /** Determines the path of the given DLC folder .bin file */
    static bool ResolveConfigPath(wchar_t const* const Name, FString& OutPath)
    {
        std::wstring const FileName = std::wstring(L"Default_") + Name + L".bin";
        void* const FileManager = *gp_fileManager;
        auto* const GetFilePath = *reinterpret_cast<t_GetFilePath**>(*reinterpret_cast<BYTE**>(FileManager) + k_getFilePathVtableOffset);
        return GetFilePath(FileManager, FileName.c_str(), &OutPath) != 0 && OutPath.Any();
    }


    void Initialize(::LESDK::Initializer& Context)
    {
        gp_config = Context.ResolveTyped<void*>(GLOBAL_GCONFIG_RVA);
        gp_contentManager = Context.ResolveTyped<void*>(GLOBAL_GCONTENTMANAGER_RVA);
        gp_fileManager = Context.ResolveTyped<void*>(GLOBAL_GFILEMANAGER_RVA);
        CreateConfigFile = Context.ResolveTyped<t_CreateConfigFile>(GLOBAL_FCONFIGCACHEINI_CREATECONFIGFILE_RVA);
        RemoveConfigFile = Context.ResolveTyped<t_RemoveConfigFile>(GLOBAL_FCONFIGCACHEINI_REMOVECONFIGFILE_RVA);
        ReloadQueuedClasses = Context.ResolveTyped<t_ReloadQueuedClasses>(GLOBAL_FCONFIGCACHEINI_RELOADQUEUEDCLASSES_RVA);
        LoadFileToArray = Context.ResolveTyped<t_LoadFileToArray>(GLOBAL_APPLOADFILETOARRAY_RVA);

        LOG_SDK_GLOBAL(gp_contentManager, gp_contentManager);
        LOG_SDK_GLOBAL(gp_fileManager, gp_fileManager);
        LOG_SDK_GLOBAL(RemoveConfigFile, RemoveConfigFile);
        LOG_SDK_GLOBAL(ReloadQueuedClasses, ReloadQueuedClasses);
        LOG_SDK_GLOBAL(LoadFileToArray, LoadFileToArray);
    }

    bool Reload(wchar_t const* const DLCName)
    {
        if (!IsReady())
        {
            MILAN_WARNW(ELOG_DLCConfig, L"DLC reload system not fully initialized");
            return false;
        }

        void* const Content = FindContent(DLCName);
        if (Content == nullptr)
        {
            MILAN_WARNW(ELOG_DLCConfig, L"No DLC folder named '%s'", DLCName);
            LogContents();
            return false;
        }

        std::wstring const Name = DLCConfigReload::DLCName(Content);

        FString Path{};
        if (!ResolveConfigPath(Name.c_str(), Path))
        {
            MILAN_WARNW(ELOG_DLCConfig, L"%s: Couldn't resolve config path", Name.c_str());
            return false;
        }

        unsigned& Flags = Field<unsigned>(Content, k_contentFlagsOffset);
        void*& Layer = Field<void*>(Content, k_contentConfigLayerOffset);
        TArray<BYTE>& Bytes = Field<TArray<BYTE>>(Content, k_contentConfigBytesOffset);
        void* const Config = *gp_config;

        bool const bWasMounted = (Flags & k_flagConfigMounted) != 0 && Layer != nullptr;

        // Unmount first
        if (bWasMounted)
        {
            RemoveConfigFile(Config, Layer, 1);
            Layer = nullptr;
            Flags &= ~k_flagConfigMounted;
        }

        bool const bRead = LoadFileToArray(&Bytes, *Path) != 0;
        if (!bRead)
        {
            MILAN_WARNW(ELOG_DLCConfig, L"%s: could not read %s", Name.c_str(), *Path);
        }
        else if (Bytes.Count() < 4)
        {
            MILAN_WARNW(ELOG_DLCConfig, L"%s: Invalid config file", Name.c_str());
        }
        else
        {
            Flags |= k_flagHasConfigFile;
        }

        if (!bWasMounted)
        {
            // Will reload on next mount
            return bRead;
        }

        // Remount the DLC config
        if (Bytes.GetData() == nullptr || Bytes.Count() < 4)
        {
            MILAN_ERRORW(ELOG_DLCConfig, L"%s: no usable config bytes, leaving its config unmounted", Name.c_str());
            ReloadQueuedClasses(Config, FLT_MAX);
            return false;
        }

        // Biower pls
        unsigned long long const Key = (static_cast<unsigned long long>(static_cast<unsigned>(Field<int>(Content, k_contentMountPriorityOffset))) << 32) | k_dlcLayerKeyLow;
        void* const NewLayer = CreateConfigFile(Config, Bytes.GetData(), Name.c_str(), Key, 1);
        if (NewLayer == nullptr || Field<void*>(NewLayer, k_layerParsedDataOffset) == nullptr)
        {
            MILAN_ERRORW(ELOG_DLCConfig, L"%s: %s failed to parse", Name.c_str(), *Path);
            if (NewLayer != nullptr)
            {
                RemoveConfigFile(Config, NewLayer, 0);
            }
            ReloadQueuedClasses(Config, FLT_MAX);
            return false;
        }

        Layer = NewLayer;
        Flags |= k_flagConfigMounted;

        // Reload config in-game
        ReloadQueuedClasses(Config, FLT_MAX);

        MILAN_INFOW(ELOG_DLCConfig, L"%s: reloaded %s", Name.c_str(), *Path);
        return true;
    }

    void RegisterCommands(CCommands& Commands)
    {
        Commands.RegisterCommand(L"dlc", L"list",
            [](wchar_t const*)
            {
                if (!IsReady())
                {
                    MILAN_WARNW(ELOG_DLCConfig, L"Content manager isn't initialized yet");
                    return true;
                }
                LogContents();
                return true;
            });

        Commands.RegisterCommand(L"dlc", L"reloadconfig",
            [](wchar_t const* const Params)
            {
                std::wstring Name = Params != nullptr ? Params : L"";
                size_t const First = Name.find_first_not_of(L" \t\"");
                size_t const Last = Name.find_last_not_of(L" \t\"");
                Name = First == std::wstring::npos ? std::wstring{} : Name.substr(First, Last - First + 1);

                if (Name.empty())
                {
                    MILAN_INFOW(ELOG_DLCConfig, L"usage: dlc.reloadconfig <DLC folder name>, e.g. dlc.reloadconfig DLC_MOD_Foo");
                    if (IsReady())
                    {
                        LogContents();
                    }
                    return true;
                }

                Reload(Name.c_str());
                return true;
            });
    }
}
