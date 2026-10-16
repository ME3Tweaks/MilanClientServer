#include <atomic>
#include <cstring>
#include <intrin.h>

#include <LESDK/Headers.hpp>
#include "Base/Logger.hpp"
#include "Engine/InputPromptFix.hpp"
#include "Engine/InputPromptPolicy.hpp"
#include "Shell/CmdArgs.hpp"

namespace InputPromptFix
{
    namespace
    {
        constexpr char k_hookName[] = "SharedInputPromptBindings";
        constexpr std::uintptr_t k_collectBindingsRva = 0xBAC4F0;

        // Native non-UFunction helper. The last two parameters are parallel output
        // arrays; both UI callers supply fresh temporary arrays, not live bindings.
        using CollectBindings = void(USFXGameModeManager*, BYTE, UBOOL, FString const&,
            TArray<FKeyBind>&, TArray<FString>&);
        CollectBindings* g_original = nullptr;
        std::uintptr_t g_moduleBase = 0;
        std::atomic_uint g_traceCount{ 0 };

        static_assert(sizeof(FKeyBind) == 0x1C);
        static_assert(sizeof(FString) == 0x10);
        static_assert(sizeof(TArray<FKeyBind>) == 0x10);

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

        __declspec(noinline) void CollectBindingsHook(USFXGameModeManager* Manager, BYTE GameMode,
            UBOOL Defaults, FString const& Alias, TArray<FKeyBind>& Bindings, TArray<FString>& Labels)
        {
            // Capture before calling the trampoline. These exact return sites gate
            // the change to TLK/control-token rendering and GetBoundKeyString.
            auto const Caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
            g_original(Manager, GameMode, Defaults, Alias, Bindings, Labels);

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
            if (Nt->OptionalHeader.SizeOfImage <= Policy::k_controlTokenReturnRva)
                return false;

            // Verify the collector's prologue AND both call instructions before
            // relying on RVAs. Unknown/previously-detoured code keeps stock behavior.
            constexpr unsigned char Prologue[] = { 0x80, 0xFA, 0x19, 0x0F, 0x83, 0xAD, 0x02, 0x00, 0x00 };
            constexpr unsigned char GetBoundKeyCall[] = { 0xE8, 0xC7, 0xC6, 0xFE, 0xFF };
            constexpr unsigned char ControlTokenCall[] = { 0xE8, 0xE2, 0x85, 0xF8, 0xFF };
            return std::memcmp(Base + k_collectBindingsRva, Prologue, sizeof Prologue) == 0 &&
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
        if (g_original == nullptr)
        {
            MILAN_ERRORW(ELOG_Startup, L"shared prompt fix: failed to create binding lookup hook");
            return;
        }
        Context.EnableHook(k_hookName);
        MILAN_INFOW(ELOG_Startup, L"shared prompt fix: installed for Shared_* token and GetBoundKeyString lookups");
    }

    void Uninstall(::LESDK::Initializer& Context)
    {
        if (g_original != nullptr)
            Context.UninstallHook(k_hookName);
    }
}
