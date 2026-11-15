#include <atomic>
#include <cstring>
#include <intrin.h>
#include <array>

#include <LESDK/Headers.hpp>
#include "Base/Logger.hpp"
#include "Engine/InputPromptFix.hpp"
#include "Engine/InputPromptPolicy.hpp"
#include "Engine/PcBindingPolicy.hpp"
#include "Shell/CmdArgs.hpp"

namespace InputPromptFix
{
    namespace
    {
        constexpr char k_hookName[] = "SharedInputPromptBindings";
        constexpr std::uintptr_t k_collectBindingsRva = 0xBAC4F0;
        constexpr char k_replaceHookName[] = "SharedPcBindingReplacement";
        constexpr char k_profileHookName[] = "SharedPcBindingProfile";
        constexpr std::uintptr_t k_replaceBindingsRva = 0xBB5B10;
        constexpr std::uintptr_t k_serializeBindingsRva = 0xBB4FF0;
        constexpr std::uintptr_t k_memoryReaderVtableRva = 0x107FBD0;
        constexpr std::uintptr_t k_serializeStringRva = 0x1169D0;

        // Native non-UFunction helper. The last two parameters are parallel output
        // arrays; both UI callers supply fresh temporary arrays, not live bindings.
        using CollectBindings = void(USFXGameModeManager*, BYTE, UBOOL, FString const&,
            TArray<FKeyBind>&, TArray<FString>&);
        CollectBindings* g_original = nullptr;
        using ReplaceBindings = void(USFXGameModeManager*, BYTE, FString const&, TArray<FKeyBind> const&);
        using SerializeBindings = void(USFXGameModeManager*, FArchive*);
        using SerializeString = void(FArchive*, FString&);
        ReplaceBindings* g_replaceOriginal = nullptr;
        SerializeBindings* g_serializeOriginal = nullptr;
        std::uintptr_t g_moduleBase = 0;
        std::atomic_uint g_traceCount{ 0 };

        static_assert(sizeof(FKeyBind) == 0x1C);
        static_assert(sizeof(FString) == 0x10);
        static_assert(sizeof(TArray<FKeyBind>) == 0x10);

        struct ProfileRead
        {
            SerializeString* ReadString;
            UBioSFHandler_PCOptions* Defaults;
            bool LegacyRow = false;
        };
        thread_local ProfileRead* g_profileRead = nullptr;

        Policy::Device ClassifyKey(SFXName const& Name)
        {
            // Physical input names are unnumbered. Ignore sentinel/invalid names.
            if (Name.Number != 0)
                return Policy::Device::Unbound;
            SFXNameEntry const* const Entry = Name.GetEntry();
            if (Entry == nullptr)
                return Policy::Device::Unbound;
            if (Entry->IsUnicode())
                return Policy::ClassifyKey(std::wstring_view(Entry->WideName));
            return Policy::ClassifyKey(std::string_view(Entry->AnsiName));
        }

        void ReplaceBindingsHook(USFXGameModeManager* Manager, BYTE GameMode,
            FString const& Alias, TArray<FKeyBind> const& Incoming)
        {
            // Skip every subordinate of a broken migrated row too (Aim also
            // propagates to the still-PC-only command-camera alias).
            if (Policy::RetainDefaultsForLegacyRow(g_profileRead != nullptr && g_profileRead->LegacyRow,
                    Incoming, [](FKeyBind const& Key) { return ClassifyKey(Key.Name); }))
            {
                return;
            }
            if (Manager == nullptr || GameMode >= Manager->GameModes.Count() ||
                Manager->GameModes(GameMode) == nullptr || !Alias.StartsWith(L"Shared_", true))
            {
                g_replaceOriginal(Manager, GameMode, Alias, Incoming);
                return;
            }

            TArray<FKeyBind> PcKeys;
            for (auto const& Key : Incoming)
            {
                auto const Device = ClassifyKey(Key.Name);
                if (Device != Policy::Device::Gamepad)
                {
                    PcKeys.Add(Key);
                }
            }
            auto& Bindings = Manager->GameModes(GameMode)->Bindings;
            Policy::WithGamepadBindingsPreserved(Bindings,
                [](FKeyBind const& Key) { return ClassifyKey(Key.Name); },
                [&] { g_replaceOriginal(Manager, GameMode, Alias, PcKeys); });
        }

        void ReadProfileAlias(FArchive* Archive, FString& Alias)
        {
            auto& State = *g_profileRead;
            State.ReadString(Archive, Alias);
            State.LegacyRow = false;
            if (Archive->ArIsError || Archive->ArIsCriticalError)
            {
                return;
            }
            auto const Current = Policy::MigratePcAlias(std::wstring_view(Alias.Chars(), Alias.Length()));
            if (!Current.empty())
            {
                for (auto const& Row : State.Defaults->AliasMap)
                {
                    if (Row.Alias.Equals(Current.data(), true))
                    {
                        Alias = FString(Current.data());
                        State.LegacyRow = true;
                        break;
                    }
                }
            }
        }

        void SerializeBindingsHook(USFXGameModeManager* Manager, FArchive* Archive)
        {
            auto* const Class = UBioSFHandler_PCOptions::StaticClass();
            auto* const Defaults = Class != nullptr && Class->ClassDefaultObject != nullptr
                ? Class->ClassDefaultObject->Cast<UBioSFHandler_PCOptions>() : nullptr;
            if (Archive == nullptr || !Archive->ArIsLoading || Defaults == nullptr ||
                Archive->_vftable != reinterpret_cast<void*>(g_moduleBase + k_memoryReaderVtableRva))
            {
                g_serializeOriginal(Manager, Archive);
                return;
            }

            // This stack reader has 29 virtual slots in the supported executable.
            // Interpose only its FString read, leaving native parsing/version/error
            // handling and the source blob intact. No global vtable is modified.
            auto** const OriginalTable = static_cast<void**>(Archive->_vftable);
            std::array<void*, 29> Table;
            std::copy_n(OriginalTable, Table.size(), Table.begin());
            ProfileRead State{reinterpret_cast<SerializeString*>(Table[25]), Defaults};
            struct RestoreReader
            {
                FArchive* Archive;
                void* Table;
                ProfileRead* Previous;
                ~RestoreReader()
                {
                    Archive->_vftable = Table;
                    g_profileRead = Previous;
                }
            } Guard{Archive, Archive->_vftable, g_profileRead};
            Table[25] = reinterpret_cast<void*>(&ReadProfileAlias);
            g_profileRead = &State;
            Archive->_vftable = Table.data();
            g_serializeOriginal(Manager, Archive);
        }

        __declspec(noinline) void CollectBindingsHook(USFXGameModeManager* Manager, BYTE GameMode,
            UBOOL Defaults, FString const& Alias, TArray<FKeyBind>& Bindings, TArray<FString>& Labels)
        {
            // Capture before calling the trampoline. These exact return sites gate
            // the change to TLK/control-token rendering and GetBoundKeyString.
            auto const Caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
            g_original(Manager, GameMode, Defaults, Alias, Bindings, Labels);

            // PC binding pages always edit PC keys, including Reset and Save.
            // This is independent of the device currently driving UI prompts.
            if (Policy::IsPcBindingLookup(Caller - g_moduleBase,
                    std::wstring_view(Alias.Chars(), Alias.Length())) && Bindings.Count() == Labels.Count())
            {
                Policy::KeepPcBindings(Bindings, Labels,
                    [](FKeyBind const& Key) { return ClassifyKey(Key.Name); });
                return;
            }

            if (!Policy::ShouldAdjust(Caller - g_moduleBase, Defaults != 0,
                    std::wstring_view(Alias.Chars(), Alias.Length())) ||
                Bindings.Count() != Labels.Count() || Bindings.Empty() || Manager == nullptr ||
                Manager->Outer == nullptr)
                return;

            auto* const PC = Manager->Outer->Cast<ABioPlayerController>();
            if (PC == nullptr || PC->PlayerInput == nullptr)
                return;

            bool const UsingGamepad = PC->PlayerInput->bUsingGamepad != 0;
            auto const Selected = Policy::SelectBinding(Bindings.Count(), UsingGamepad,
                [&](std::size_t Index) { return ClassifyKey(Bindings(static_cast<UINT>(Index)).Name); });

#if DEBUG
            if (g_traceCount.fetch_add(1, std::memory_order_relaxed) < 64)
            {
                FString const FirstKey = Bindings(0).Name.ToString();
                FString const SelectedKey = Selected < Bindings.Count()
                    ? Bindings(static_cast<UINT>(Selected)).Name.ToString() : FString(L"(unbound)");
                MILAN_DEBUGW(ELOG_EngineHooks,
                    L"shared prompt: %s alias=%s mode=%u device=%s first=%s selected=%s matches=%u",
                    Caller - g_moduleBase == Policy::k_controlTokenReturnRva ? L"token" : L"GetBoundKeyString",
                    Alias.Chars(), static_cast<unsigned>(GameMode), UsingGamepad ? L"gamepad" : L"keyboard/mouse",
                    FirstKey.Chars(), SelectedKey.Chars(), static_cast<unsigned>(Bindings.Count()));
            }
#endif
            if (Selected == Bindings.Count())
            {
                Bindings.Clear();
                Labels.Clear();
            }
            else if (Selected != 0)
            {
                Policy::PromoteBinding(std::span(Bindings.GetData(), Bindings.Count()),
                    std::span(Labels.GetData(), Labels.Count()), Selected);
            }
        }

        bool MatchesSupportedCode()
        {
            auto const* const Base = reinterpret_cast<unsigned char const*>(g_moduleBase);
            auto const* const Dos = reinterpret_cast<IMAGE_DOS_HEADER const*>(Base);
            auto const* const Nt = reinterpret_cast<IMAGE_NT_HEADERS64 const*>(Base + Dos->e_lfanew);
            if (Nt->OptionalHeader.SizeOfImage <= k_memoryReaderVtableRva + 29 * sizeof(void*))
                return false;

            // Verify the collector's prologue AND both call instructions before
            // relying on RVAs. Unknown/previously-detoured code keeps stock behavior.
            constexpr unsigned char Prologue[] = { 0x80, 0xFA, 0x19, 0x0F, 0x83, 0xAD, 0x02, 0x00, 0x00 };
            constexpr unsigned char GetBoundKeyCall[] = { 0xE8, 0xC7, 0xC6, 0xFE, 0xFF };
            constexpr unsigned char ControlTokenCall[] = { 0xE8, 0xE2, 0x85, 0xF8, 0xFF };
            constexpr unsigned char ReplacePrologue[] = { 0x80, 0xFA, 0x19, 0x0F, 0x83, 0x56, 0x06, 0x00, 0x00 };
            constexpr unsigned char SerializePrologue[] = { 0x40, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55 };
            constexpr unsigned char MenuCall[] = { 0xE8, 0x4A, 0x07, 0x13, 0x00 };
            constexpr unsigned char SaveCall[] = { 0xE8, 0xB7, 0x70, 0xFF, 0xFF };
            return std::memcmp(Base + k_collectBindingsRva, Prologue, sizeof Prologue) == 0 &&
                std::memcmp(Base + k_replaceBindingsRva, ReplacePrologue, sizeof ReplacePrologue) == 0 &&
                std::memcmp(Base + k_serializeBindingsRva, SerializePrologue, sizeof SerializePrologue) == 0 &&
                std::memcmp(Base + Policy::k_pcMenuReturnRva - 5, MenuCall, sizeof MenuCall) == 0 &&
                std::memcmp(Base + Policy::k_pcSaveReturnRva - 5, SaveCall, sizeof SaveCall) == 0 &&
                *reinterpret_cast<std::uintptr_t const*>(Base + k_memoryReaderVtableRva + 25 * sizeof(void*)) ==
                    g_moduleBase + k_serializeStringRva &&
                std::memcmp(Base + Policy::k_getBoundKeyReturnRva - 5, GetBoundKeyCall, sizeof GetBoundKeyCall) == 0 &&
                std::memcmp(Base + Policy::k_controlTokenReturnRva - 5, ControlTokenCall, sizeof ControlTokenCall) == 0;
        }
    }

    void Initialize(::LESDK::Initializer& Context)
    {
        g_moduleBase = reinterpret_cast<std::uintptr_t>(Context.ModuleBase);
        if (g_moduleBase == 0 || !MatchesSupportedCode())
        {
            MILAN_WARNW(ELOG_Startup, L"shared prompt fix: unsupported native code; hook not installed");
            return;
        }

        auto* const Target = reinterpret_cast<CollectBindings*>(g_moduleBase + k_collectBindingsRva);
        g_original = reinterpret_cast<CollectBindings*>(Context.InstallHookDeferred(
            k_hookName, reinterpret_cast<void*>(Target), reinterpret_cast<void*>(&CollectBindingsHook)));
        g_replaceOriginal = reinterpret_cast<ReplaceBindings*>(Context.InstallHookDeferred(
            k_replaceHookName, reinterpret_cast<void*>(g_moduleBase + k_replaceBindingsRva),
            reinterpret_cast<void*>(&ReplaceBindingsHook)));
        g_serializeOriginal = reinterpret_cast<SerializeBindings*>(Context.InstallHookDeferred(
            k_profileHookName, reinterpret_cast<void*>(g_moduleBase + k_serializeBindingsRva),
            reinterpret_cast<void*>(&SerializeBindingsHook)));
        if (g_original == nullptr || g_replaceOriginal == nullptr || g_serializeOriginal == nullptr)
        {
            Uninstall(Context);
            MILAN_ERRORW(ELOG_Startup, L"shared input fix: failed to create binding hooks");
            return;
        }
        Context.EnableHook(k_replaceHookName);
        Context.EnableHook(k_profileHookName);
        Context.EnableHook(k_hookName);
        MILAN_INFOW(ELOG_Startup, L"shared input fix: installed prompt, PC menu/reset, and profile binding hooks");
    }

    void Uninstall(::LESDK::Initializer& Context)
    {
        if (g_original != nullptr)
            Context.UninstallHook(k_hookName);
        if (g_serializeOriginal != nullptr)
            Context.UninstallHook(k_profileHookName);
        if (g_replaceOriginal != nullptr)
            Context.UninstallHook(k_replaceHookName);
        g_original = nullptr;
        g_serializeOriginal = nullptr;
        g_replaceOriginal = nullptr;
    }
}
