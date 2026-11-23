#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <intrin.h>
#include <iterator>

#include <LESDK/Headers.hpp>
#include "Base/Logger.hpp"
#include "Engine/Hooks.hpp"
#include "Engine/InputGroupPolicy.hpp"
#include "Engine/InputPromptFix.hpp"
#include "Engine/InputPromptPolicy.hpp"
#include "Engine/PcBindingPolicy.hpp"
#include "Shell/CmdArgs.hpp"


// ! Hook patterns and RVAs.
// ========================================

// Native non-UFunction binding collector. The last two parameters are parallel output arrays
#define GLOBAL_USFXGAMEMODEMANAGER_COLLECTBINDINGS_PHOOK \
    ::LESDK::Address::FromPostHook(/* 80 FA 19 0F 83 */ "AD 02 00 00 48 8B C4 48 89 48 08 57")

// Replaces an alias's keys in a mode, removing the key from conflicting bindings
#define GLOBAL_USFXGAMEMODEMANAGER_REPLACEBINDINGS_PHOOK \
    ::LESDK::Address::FromPostHook(/* 80 FA 19 0F 83 */ "56 06 00 00 48 8B C4 55 41 54 41 55")

// Loads/saves the profile's bindings
#define GLOBAL_USFXGAMEMODEMANAGER_SERIALIZEBINDINGS_PHOOK \
    ::LESDK::Address::FromPostHook(/* 40 55 56 57 41 */ "54 41 55 41 56 41 57 48 8D 6C 24 C0 48 81 EC 40 01 00 00 48 C7 45 90")

// BioSFHandler_PCOptions.ProcessInput during key capture when assigning a keybinding
#define GLOBAL_UBIOSFHANDLER_PCOPTIONS_CAPTUREKEY_PHOOK \
    ::LESDK::Address::FromPostHook(/* 48 8B C4 4C 89 */ "40 18 55 41 54 41 55 41 56 41 57 48 8D 68 B1 48 81 EC C0")

// Stack FMemoryReader used for profile loads and reading its string
#define GLOBAL_FMEMORYREADER_VTABLE_RVA         ::LESDK::Address::FromOffset(0x107FBD0)
#define GLOBAL_FARCHIVE_SERIALIZESTRING_RVA     ::LESDK::Address::FromOffset(0x1169D0)
#define FMEMORYREADER_VTABLE_SLOTS              29
#define FMEMORYREADER_SERIALIZESTRING_SLOT      25

// `call rel32`, used to check that the gated return addresses follow a call to the bindings collector
#define X86_CALL_REL32_OPCODE                   0xE8
#define X86_CALL_REL32_LENGTH                   5

// DEBUG builds log at most this many prompt lookups
#define SHARED_PROMPT_TRACE_LIMIT               64


namespace InputPromptFix
{
    namespace
    {
        // Both UI callers supply fresh temporary output arrays, not live bindings
        using t_USFXGameModeManager_CollectBindings = void(USFXGameModeManager*, BYTE, UBOOL, FString const&,
            TArray<FKeyBind>&, TArray<FString>&);
        t_USFXGameModeManager_CollectBindings* USFXGameModeManager_CollectBindings_orig = nullptr;

        using t_USFXGameModeManager_ReplaceBindings = void(USFXGameModeManager*, BYTE, FString const&, TArray<FKeyBind> const&);
        t_USFXGameModeManager_ReplaceBindings* USFXGameModeManager_ReplaceBindings_orig = nullptr;

        using t_USFXGameModeManager_SerializeBindings = void(USFXGameModeManager*, FArchive*);
        t_USFXGameModeManager_SerializeBindings* USFXGameModeManager_SerializeBindings_orig = nullptr;

        // ProcessInput(ControllerId, Key, Event, AmountDepressed, bGamepad)
        using t_UBioSFHandler_PCOptions_CaptureKey = UBOOL(UBioSFHandler_PCOptions*, int, SFXName, BYTE, float, UBOOL);
        t_UBioSFHandler_PCOptions_CaptureKey* UBioSFHandler_PCOptions_CaptureKey_orig = nullptr;

        using SerializeString = void(FArchive*, FString&);

        std::uintptr_t g_moduleBase = 0;
        void const* g_memoryReaderVtable = nullptr;
        std::atomic_uint g_traceCount{ 0 };

        static_assert(sizeof(FKeyBind) == 0x1C);
        static_assert(sizeof(FString) == 0x10);
        static_assert(sizeof(TArray<FKeyBind>) == 0x10);

        struct ProfileRead
        {
            SerializeString* ReadString;
            UBioSFHandler_PCOptions* Defaults;
            bool ActionFanOut = false;
        };
        thread_local ProfileRead* g_profileRead = nullptr;

        Policy::Device ClassifyKey(SFXName const& Name)
        {
            // Physical input names are unnumbered. Ignore sentinel/invalid names.
            if (Name.Number != 0)
            {
                return Policy::Device::Unbound;
            }
            SFXNameEntry const* const Entry = Name.GetEntry();
            if (Entry == nullptr)
            {
                return Policy::Device::Unbound;
            }
            if (Entry->IsUnicode())
            {
                return Policy::ClassifyKey(std::wstring_view(Entry->WideName));
            }
            return Policy::ClassifyKey(std::string_view(Entry->AnsiName));
        }

        Policy::Device ClassifyBinding(FKeyBind const& Key)
        {
            return ClassifyKey(Key.Name);
        }

        std::wstring_view View(FString const& Text)
        {
            return std::wstring_view(Text.Chars(), Text.Length());
        }

        bool SameKey(FKeyBind const& Left, FKeyBind const& Right)
        {
            return Left.Name == Right.Name && Left.Control == Right.Control &&
                Left.Shift == Right.Shift && Left.Alt == Right.Alt;
        }

        // Other action group aliases keep a key they share with the target.
        void ReplaceGroupBindings(USFXGameModeManager* Manager, BYTE GameMode, FString const& Alias, TArray<FKeyBind> const& Incoming)
        {
            auto const SharesIncomingKey = [&](FKeyBind const& Existing)
            {
                return !Existing.Command.Equals(Alias, true) && Policy::IsActionGroupAlias(View(Existing.Command)) &&
                    std::any_of(Incoming.begin(), Incoming.end(), [&](FKeyBind const& Key) { return SameKey(Existing, Key); });
            };
            Policy::WithBindingsHidden(Manager->GameModes(GameMode)->Bindings, SharesIncomingKey, [&]
            {
                USFXGameModeManager_ReplaceBindings_orig(Manager, GameMode, Alias, Incoming);
            });
        }

        void USFXGameModeManager_ReplaceBindings_hook(USFXGameModeManager* Manager, BYTE GameMode,
            FString const& Alias, TArray<FKeyBind> const& Incoming)
        {
            bool const ValidMode = Manager != nullptr && GameMode < Manager->GameModes.Count() && Manager->GameModes(GameMode) != nullptr;
            if (ValidMode && Policy::IsActionGroupAlias(View(Alias)))
            {
                ReplaceGroupBindings(Manager, GameMode, Alias, Incoming);
                if (g_profileRead != nullptr && g_profileRead->ActionFanOut && Policy::EqualsAsciiNoCase(View(Alias), Policy::k_actionCoverRow))
                {
                    for (auto const Member : Policy::k_actionGroup)
                    {
                        if (!Policy::EqualsAsciiNoCase(Member, Policy::k_actionCoverRow))
                        {
                            ReplaceGroupBindings(Manager, GameMode, FString(Member.data()), Incoming);
                        }
                    }
                }
                return;
            }
            if (!ValidMode || !Alias.StartsWith(L"Shared_", true))
            {
                USFXGameModeManager_ReplaceBindings_orig(Manager, GameMode, Alias, Incoming);
                return;
            }

            TArray<FKeyBind> PcKeys;
            for (auto const& Key : Incoming)
            {
                if (ClassifyBinding(Key) != Policy::Device::Gamepad)
                {
                    PcKeys.Add(Key);
                }
            }
            Policy::WithGamepadBindingsPreserved(Manager->GameModes(GameMode)->Bindings, ClassifyBinding, [&]
            {
                USFXGameModeManager_ReplaceBindings_orig(Manager, GameMode, Alias, PcKeys);
            });
        }

        // Replaces the reader's FString read while profile bindings load.
        void ReadProfileAlias(FArchive* Archive, FString& Alias)
        {
            auto& State = *g_profileRead;
            State.ReadString(Archive, Alias);
            State.ActionFanOut = false;
            if (Archive->ArIsError || Archive->ArIsCriticalError || !Policy::EqualsAsciiNoCase(View(Alias), Policy::k_sharedAction))
            {
                return;
            }
            // The single omnikey binding now loads into the cover row and also populates the others if not set
            for (auto const& Row : State.Defaults->AliasMap)
            {
                if (Row.Alias.Equals(Policy::k_actionCoverRow.data(), true))
                {
                    State.ActionFanOut = true;
                    Alias = FString(Policy::k_actionCoverRow.data());
                    return;
                }
            }
        }

        // Other action group bindings may be assigned the same captured key,
        // so it needs to be ignored when checking if it overwrites the same key
        // This is the part that actually lets you bind multiple keys in the UI component
        UBOOL UBioSFHandler_PCOptions_CaptureKey_hook(UBioSFHandler_PCOptions* Menu, int ControllerId, SFXName Key, BYTE Event,
            float AmountDepressed, UBOOL bGamepad)
        {
            struct Hidden
            {
                FGuiBind* Row;
                std::size_t Slot;
                SFXName Name;
            };
            struct Restore
            {
                std::vector<Hidden> Saved;
                ~Restore()
                {
                    for (auto const& Entry : Saved)
                    {
                        Entry.Row->Keys[Entry.Slot].Name = Entry.Name;
                    }
                }
            } Guard;

            auto const Index = Menu != nullptr ? Menu->CaptureBindIndex : -1;
            if (Event == Policy::k_inputReleased && Index >= 0 && static_cast<UINT>(Index) < Menu->AliasMap.Count() && Policy::IsActionGroupAlias(View(Menu->AliasMap(static_cast<UINT>(Index)).Alias)))
            {
                static SFXName const None(L"None", 0);
                auto const GameMode = Menu->AliasMap(static_cast<UINT>(Index)).GameMode;
                for (UINT Row = 0; Row < Menu->AliasMap.Count(); ++Row)
                {
                    auto& Bind = Menu->AliasMap(Row);
                    if (Row == static_cast<UINT>(Index) || Bind.GameMode != GameMode || !Policy::IsActionGroupAlias(View(Bind.Alias)))
                    {
                        // Not a group alias or this game mode
                        continue;
                    }
                    for (std::size_t Slot = 0; Slot < std::size(Bind.Keys); ++Slot)
                    {
                        if (Bind.Keys[Slot].Name == Key)
                        {

                            Guard.Saved.push_back({ &Bind, Slot, Key });
                            Bind.Keys[Slot].Name = None;
                        }
                    }
                }
            }
            return UBioSFHandler_PCOptions_CaptureKey_orig(Menu, ControllerId, Key, Event, AmountDepressed, bGamepad);
        }

        void USFXGameModeManager_SerializeBindings_hook(USFXGameModeManager* Manager, FArchive* Archive)
        {
            auto* const Class = UBioSFHandler_PCOptions::StaticClass();
            auto* const Defaults = Class != nullptr && Class->ClassDefaultObject != nullptr
                ? Class->ClassDefaultObject->Cast<UBioSFHandler_PCOptions>() : nullptr;
            if (Archive == nullptr || !Archive->ArIsLoading || Defaults == nullptr || Archive->_vftable != g_memoryReaderVtable)
            {
                USFXGameModeManager_SerializeBindings_orig(Manager, Archive);
                return;
            }

            // Swap in a copy of this reader's vtable with only its FString read replaced
            auto** const OriginalTable = static_cast<void**>(Archive->_vftable);
            std::array<void*, FMEMORYREADER_VTABLE_SLOTS> Table;
            std::copy_n(OriginalTable, Table.size(), Table.begin());
            ProfileRead State{ reinterpret_cast<SerializeString*>(Table[FMEMORYREADER_SERIALIZESTRING_SLOT]), Defaults };
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
            } Guard{ Archive, Archive->_vftable, g_profileRead };
            Table[FMEMORYREADER_SERIALIZESTRING_SLOT] = reinterpret_cast<void*>(&ReadProfileAlias);
            g_profileRead = &State;
            Archive->_vftable = Table.data();
            USFXGameModeManager_SerializeBindings_orig(Manager, Archive);
        }

        __declspec(noinline) void USFXGameModeManager_CollectBindings_hook(USFXGameModeManager* Manager, BYTE GameMode,
            UBOOL Defaults, FString const& Alias, TArray<FKeyBind>& Bindings, TArray<FString>& Labels)
        {
            // Capture before calling the trampoline. These exact return sites gate
            // the change to TLK/control-token rendering and GetBoundKeyString.
            auto const CallerRva = reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - g_moduleBase;
            USFXGameModeManager_CollectBindings_orig(Manager, GameMode, Defaults, Alias, Bindings, Labels);

            // PC binding pages always edit PC keys, including Reset and Save.
            // This is independent of the device currently driving UI prompts.
            if (Policy::IsPcBindingLookup(CallerRva, View(Alias)) && Bindings.Count() == Labels.Count())
            {
                Policy::KeepPcBindings(Bindings, Labels, ClassifyBinding);
                return;
            }

            if (!Policy::IsPromptLookup(CallerRva, Defaults != 0) || Bindings.Count() != Labels.Count() ||
                Manager == nullptr || Manager->Outer == nullptr)
            {
                return;
            }

            auto* const PC = Manager->Outer->Cast<ABioPlayerController>();
            if (PC == nullptr || PC->PlayerInput == nullptr)
            {
                return;
            }

            bool const UsingGamepad = PC->PlayerInput->bUsingGamepad != 0;
            auto const Fallback = Policy::PromptFallbackAlias(View(Alias), UsingGamepad);
            if (!Policy::ShouldAdjust(CallerRva, Defaults != 0, View(Alias)) && Fallback.empty())
            {
                return;
            }

            auto const Select = [&]
            {
                return Policy::SelectBinding(Bindings.Count(), UsingGamepad, [&](std::size_t Index)
                {
                    return ClassifyBinding(Bindings(static_cast<UINT>(Index)));
                });
            };
            auto Selected = Select();
            if (Selected == Bindings.Count() && !Fallback.empty())
            {
                Bindings.Clear();
                Labels.Clear();
                USFXGameModeManager_CollectBindings_orig(Manager, GameMode, Defaults, FString(Fallback.data()), Bindings, Labels);
                if (Bindings.Count() != Labels.Count())
                {
                    return;
                }
                Selected = Select();
            }

#if DEBUG
            if (!Bindings.Empty() && g_traceCount.fetch_add(1, std::memory_order_relaxed) < SHARED_PROMPT_TRACE_LIMIT)
            {
                FString const FirstKey = Bindings(0).Name.ToString();
                FString const SelectedKey = Selected < Bindings.Count()
                    ? Bindings(static_cast<UINT>(Selected)).Name.ToString() : FString(L"(unbound)");
                MILAN_DEBUGW(ELOG_EngineHooks,
                    L"shared prompt: %s alias=%s mode=%u device=%s first=%s selected=%s matches=%u",
                    CallerRva == GLOBAL_COLLECTBINDINGS_CONTROLTOKEN_RETURN_RVA ? L"token" : L"GetBoundKeyString",
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

        // True when the instruction before ReturnRva is a `call rel32` to Target.
        bool CallsTarget(std::uintptr_t ReturnRva, void const* Target)
        {
            auto const* const Call = reinterpret_cast<unsigned char const*>(g_moduleBase + ReturnRva - X86_CALL_REL32_LENGTH);
            if (Call[0] != X86_CALL_REL32_OPCODE)
            {
                return false;
            }
            std::int32_t Displacement;
            std::memcpy(&Displacement, Call + 1, sizeof Displacement);
            return reinterpret_cast<unsigned char const*>(g_moduleBase + ReturnRva) + Displacement == Target;
        }

        // The caller gates compare return addresses, so check those call sites and the reader vtable.
        bool MatchesCallSites(::LESDK::Initializer& Context, void const* Collector)
        {
            auto const* const Dos = reinterpret_cast<IMAGE_DOS_HEADER const*>(g_moduleBase);
            auto const* const Nt = reinterpret_cast<IMAGE_NT_HEADERS64 const*>(g_moduleBase + Dos->e_lfanew);
            auto* const* const Vtable = static_cast<void* const*>(Context.Resolve(GLOBAL_FMEMORYREADER_VTABLE_RVA));
            if (reinterpret_cast<std::uintptr_t>(Vtable + FMEMORYREADER_VTABLE_SLOTS) > g_moduleBase + Nt->OptionalHeader.SizeOfImage)
            {
                return false;
            }
            if (!CallsTarget(GLOBAL_COLLECTBINDINGS_PCMENU_RETURN_RVA, Collector) ||
                !CallsTarget(GLOBAL_COLLECTBINDINGS_PCSAVE_RETURN_RVA, Collector) ||
                !CallsTarget(GLOBAL_COLLECTBINDINGS_GETBOUNDKEY_RETURN_RVA, Collector) ||
                !CallsTarget(GLOBAL_COLLECTBINDINGS_CONTROLTOKEN_RETURN_RVA, Collector) ||
                Vtable[FMEMORYREADER_SERIALIZESTRING_SLOT] != Context.Resolve(GLOBAL_FARCHIVE_SERIALIZESTRING_RVA))
            {
                return false;
            }

            g_memoryReaderVtable = Vtable;
            return true;
        }
    }

    void Initialize(::LESDK::Initializer& Context)
    {
        g_moduleBase = reinterpret_cast<std::uintptr_t>(Context.ModuleBase);
        void* const CollectAddress = Context.Resolve(GLOBAL_USFXGAMEMODEMANAGER_COLLECTBINDINGS_PHOOK);
        void* const ReplaceAddress = Context.Resolve(GLOBAL_USFXGAMEMODEMANAGER_REPLACEBINDINGS_PHOOK);
        void* const SerializeAddress = Context.Resolve(GLOBAL_USFXGAMEMODEMANAGER_SERIALIZEBINDINGS_PHOOK);
        void* const CaptureAddress = Context.Resolve(GLOBAL_UBIOSFHANDLER_PCOPTIONS_CAPTUREKEY_PHOOK);
        if (g_moduleBase == 0 || CollectAddress == nullptr || ReplaceAddress == nullptr || SerializeAddress == nullptr ||
            CaptureAddress == nullptr || !MatchesCallSites(Context, CollectAddress))
        {
            MILAN_WARNW(ELOG_Startup, L"shared input fix: unsupported native code; hooks not installed");
            return;
        }

        // The collector goes last: it is the only hook that runs outside the menus.
        INSTALL_HOOK_TWO_ADDR(USFXGameModeManager, ReplaceBindings, ReplaceAddress);
        INSTALL_HOOK_TWO_ADDR(USFXGameModeManager, SerializeBindings, SerializeAddress);
        INSTALL_HOOK_TWO_ADDR(UBioSFHandler_PCOptions, CaptureKey, CaptureAddress);
        INSTALL_HOOK_TWO_ADDR(USFXGameModeManager, CollectBindings, CollectAddress);
        if (USFXGameModeManager_CollectBindings_orig == nullptr || USFXGameModeManager_ReplaceBindings_orig == nullptr ||
            USFXGameModeManager_SerializeBindings_orig == nullptr || UBioSFHandler_PCOptions_CaptureKey_orig == nullptr)
        {
            Uninstall(Context);
            MILAN_ERRORW(ELOG_Startup, L"shared input fix: failed to create binding hooks");
            return;
        }
        MILAN_INFOW(ELOG_Startup, L"shared input fix: installed prompt, PC menu/reset/capture, and profile binding hooks");
    }

    void Uninstall(::LESDK::Initializer& Context)
    {
        if (USFXGameModeManager_CollectBindings_orig != nullptr)
        {
            Context.UninstallHook("USFXGameModeManager::CollectBindings");
        }
        if (UBioSFHandler_PCOptions_CaptureKey_orig != nullptr)
        {
            Context.UninstallHook("UBioSFHandler_PCOptions::CaptureKey");
        }
        if (USFXGameModeManager_SerializeBindings_orig != nullptr)
        {
            Context.UninstallHook("USFXGameModeManager::SerializeBindings");
        }
        if (USFXGameModeManager_ReplaceBindings_orig != nullptr)
        {
            Context.UninstallHook("USFXGameModeManager::ReplaceBindings");
        }
        USFXGameModeManager_CollectBindings_orig = nullptr;
        UBioSFHandler_PCOptions_CaptureKey_orig = nullptr;
        USFXGameModeManager_SerializeBindings_orig = nullptr;
        USFXGameModeManager_ReplaceBindings_orig = nullptr;
    }
}
