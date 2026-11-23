#include <LESDK/Headers.hpp>
#include "Base/Logger.hpp"
#include "Engine/GuiMovieGuard.hpp"
#include "Engine/Hooks.hpp"


// ! Hook patterns.
// ========================================

// USFXGUIMovie vtable slot 119, called every frame by Update(). Reads GetPC()->PlayerInput->bUsingGamepad with no null checks which can cause null pointer access
#define GLOBAL_USFXGUIMOVIE_USINGGAMEPAD_PHOOK \
    ::LESDK::Address::FromPostHook(/* 48 83 EC 28 E8 */ "?? ?? ?? ?? 48 8B 88 DC 06 00 00 F6 81 FC 01 00 00 01")

// GEngine->GamePlayers(LocalPlayerIndex)->Actor, or null
#define GLOBAL_USFXGUIMOVIE_GETMOVIEPLAYER \
    ::LESDK::Address::FromPattern("8B 81 B8 01 00 00 85 C0 79 08 33 C0 89 81 B8 01 00 00 48 8B 15 ?? ?? ?? ?? 3B 82 A0 04 00 00 7D 18")


namespace GuiMovieGuard
{
    namespace
    {
        using t_USFXGUIMovie_UsingGamepad = UBOOL(USFXGUIMovie*);
        t_USFXGUIMovie_UsingGamepad* USFXGUIMovie_UsingGamepad_orig = nullptr;

        using t_USFXGUIMovie_GetMoviePlayer = APlayerController*(USFXGUIMovie*);
        t_USFXGUIMovie_GetMoviePlayer* USFXGUIMovie_GetMoviePlayer = nullptr;

        APlayerController* g_lastReportedPlayer = nullptr;

        // During level transition a player controller may be destroyed and be recreated. This can
        // allow a small gap where the UI can crash the game
        UBOOL USFXGUIMovie_UsingGamepad_hook(USFXGUIMovie* Movie)
        {
            APlayerController* const Player = USFXGUIMovie_GetMoviePlayer(Movie);
            if (Player == nullptr || Player->bDeleteMe || Player->PlayerInput == nullptr)
            {
                if (Player != g_lastReportedPlayer)
                {
                    // Cache player object
                    g_lastReportedPlayer = Player;
                    MILAN_INFOW(ELOG_EngineHooks, L"GUI movie input check: local controller %p is %s", static_cast<void*>(Player), Player == nullptr ? L"missing" : L"destroyed");
                }
                // Use cached value instead of doing the original lookup
                return Movie->bUsingGamepad;
            }

            // Use the original lookup
            g_lastReportedPlayer = nullptr;
            return USFXGUIMovie_UsingGamepad_orig(Movie);
        }
    }

    void Initialize(::LESDK::Initializer& Context)
    {
        void* const UsingGamepadAddress = Context.Resolve(GLOBAL_USFXGUIMOVIE_USINGGAMEPAD_PHOOK);
        FIND_VAR_TWO(USFXGUIMovie, GetMoviePlayer, GLOBAL_USFXGUIMOVIE_GETMOVIEPLAYER);
        if (UsingGamepadAddress == nullptr || USFXGUIMovie_GetMoviePlayer == nullptr)
        {
            MILAN_WARNW(ELOG_Startup, L"GUI movie guard: hook not installed");
            return;
        }

        INSTALL_HOOK_TWO_ADDR(USFXGUIMovie, UsingGamepad, UsingGamepadAddress);
        if (USFXGUIMovie_UsingGamepad_orig == nullptr)
        {
            MILAN_ERRORW(ELOG_Startup, L"GUI movie guard: failed to create the input check hook");
            return;
        }
        MILAN_INFOW(ELOG_Startup, L"GUI movie guard: installed input check hook");
    }

    void Uninstall(::LESDK::Initializer& Context)
    {
        if (USFXGUIMovie_UsingGamepad_orig != nullptr)
        {
            Context.UninstallHook("USFXGUIMovie::UsingGamepad");
        }
        USFXGUIMovie_UsingGamepad_orig = nullptr;
    }
}
