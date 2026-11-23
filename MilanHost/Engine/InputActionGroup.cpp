#include <string>

#include <LESDK/Headers.hpp>
#include "Base/Logger.hpp"
#include "Engine/Hooks.hpp"
#include "Engine/InputActionGroup.hpp"
#include "Engine/InputGroupPolicy.hpp"


// ! Hook patterns.
// ========================================

// UBioPlayerInput vtable slot 95 - we re-invoke this to chain aliases together
#define GLOBAL_UBIOPLAYERINPUT_EXECINPUTCOMMANDS_PHOOK \
    ::LESDK::Address::FromPostHook(/* 48 8B C4 55 56 */ "57 41 54 41 55 41 56 41 57 48 8D A8 68 FD FF FF 48 81 EC 60")

// Runs an `Exclusive` command and returns the bool marking it consumed (or not)
#define GLOBAL_UBIOPLAYERINPUT_EXECEXCLUSIVE_PHOOK \
    ::LESDK::Address::FromPostHook(/* 48 8B C4 48 89 */ "50 10 57 41 56 41 57 48 83 EC 60 48 C7 40 C0")


namespace InputActionGroup
{
    namespace
    {
        namespace Policy = InputPromptFix::Policy;

        using t_UBioPlayerInput_ExecInputCommands = void(UBioPlayerInput*, wchar_t const*, void*);
        t_UBioPlayerInput_ExecInputCommands* UBioPlayerInput_ExecInputCommands_orig = nullptr;

        using t_UBioPlayerInput_ExecExclusive = UBOOL(UBioPlayerInput*, wchar_t const*, void*, void*);
        t_UBioPlayerInput_ExecExclusive* UBioPlayerInput_ExecExclusive_orig = nullptr;

        thread_local int g_depth = 0;
        thread_local bool g_inGroupEntry = false;
        thread_local bool g_consumed = false;

        // Allows grouped input chains to run one after another, allowing multiple actions to bind to a single key
        void UBioPlayerInput_ExecInputCommands_hook(UBioPlayerInput* Input, wchar_t const* Commands, void* Ar)
        {
            // We need to track the depth of the chain we are in
            bool const TopLevel = g_depth == 0;
            ++g_depth;
            struct Leave
            {
                ~Leave()
                {
                    --g_depth;
                }
            } const Guard;

            if (!TopLevel || Input == nullptr || Commands == nullptr || (Input->CurrentEvent != Policy::k_inputPressed && Input->CurrentEvent != Policy::k_inputReleased))
            {
                UBioPlayerInput_ExecInputCommands_orig(Input, Commands, Ar);
                return;
            }
            unsigned char const Event = Input->CurrentEvent;

            // A single group alias needs no reordering.
            auto Entries = Policy::SplitCommands(Commands);
            if (Policy::OrderActionGroup(Entries) < 2)
            {
                UBioPlayerInput_ExecInputCommands_orig(Input, Commands, Ar);
                return;
            }

            g_consumed = false;
            for (auto const Entry : Entries)
            {
                bool const Group = Policy::IsActionGroupAlias(Entry);
                if (!Policy::ShouldRunCommand(Group, g_consumed, Event))
                {
                    continue;
                }
                std::wstring const Line(Entry);
                g_inGroupEntry = Group;
                UBioPlayerInput_ExecInputCommands_orig(Input, Line.c_str(), Ar);
                g_inGroupEntry = false;
            }
            g_consumed = false;
        }

        // Hook for exclusive that reports if it consumed the input so a future input chain does not execute
        UBOOL UBioPlayerInput_ExecExclusive_hook(UBioPlayerInput* Input, wchar_t const* Command, void* Ar, void* Extra)
        {
            UBOOL const Handled = UBioPlayerInput_ExecExclusive_orig(Input, Command, Ar, Extra);
            if (Handled != 0 && g_inGroupEntry)
            {
                g_consumed = true;
            }
            return Handled;
        }
    }

    void Initialize(::LESDK::Initializer& Context)
    {
        void* const ExecAddress = Context.Resolve(GLOBAL_UBIOPLAYERINPUT_EXECINPUTCOMMANDS_PHOOK);
        void* const ExclusiveAddress = Context.Resolve(GLOBAL_UBIOPLAYERINPUT_EXECEXCLUSIVE_PHOOK);
        if (ExecAddress == nullptr || ExclusiveAddress == nullptr)
        {
            MILAN_WARNW(ELOG_Startup, L"action group: input functions not found, not installing omni-key split hooks");
            return;
        }

        INSTALL_HOOK_TWO_ADDR(UBioPlayerInput, ExecExclusive, ExclusiveAddress);
        INSTALL_HOOK_TWO_ADDR(UBioPlayerInput, ExecInputCommands, ExecAddress);
        if (UBioPlayerInput_ExecInputCommands_orig == nullptr || UBioPlayerInput_ExecExclusive_orig == nullptr)
        {
            Uninstall(Context);
            MILAN_ERRORW(ELOG_Startup, L"action group: failed to create input hooks");
            return;
        }
        MILAN_INFOW(ELOG_Startup, L"action group: installed shared-key input hooks");
    }

    void Uninstall(::LESDK::Initializer& Context)
    {
        if (UBioPlayerInput_ExecInputCommands_orig != nullptr)
        {
            Context.UninstallHook("UBioPlayerInput::ExecInputCommands");
        }
        if (UBioPlayerInput_ExecExclusive_orig != nullptr)
        {
            Context.UninstallHook("UBioPlayerInput::ExecExclusive");
        }
        UBioPlayerInput_ExecInputCommands_orig = nullptr;
        UBioPlayerInput_ExecExclusive_orig = nullptr;
    }
}
