#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <LESDK/Headers.hpp>
#include "Base/Logger.hpp"
#include "Engine/Intercept.hpp"
#include "Online/TeamBetrayal.hpp"
#include "Online/TeamBetrayalPolicy.hpp"

namespace TeamBetrayal
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        // damage value that is guaranteed to kill a player
        constexpr float k_killDamage = 1000000.0f;

        // Lifetime for someone being marked as a cheater
        constexpr std::chrono::seconds k_markLifetime{ 20 };

        // DO NOT CHANGE
        constexpr int k_betrayStrRef = 49875829;
        // Used as fallback - shouldn't be used in final shipping release
        constexpr wchar_t const* k_betrayEnglish = L"{0} attempted to betray the team";

        // Mark that suppressed a player died so we can replace it with betrayal message
        struct SuppressMark
        {
            std::wstring        PlayerName;
            Clock::time_point   Expires;
        };

        std::vector<SuppressMark>   g_marks{};
        bool                        g_bShowingTicker = false;

        SFXName const& MessageType()
        {
            static SFXName const Name{ L"MilanBetray", 0 };
            return Name;
        }

        AWorldInfo* CurrentWorldInfo()
        {
            UEngine* const Engine = (GEngine != nullptr) ? *GEngine : nullptr;
            return Engine != nullptr ? Engine->GetCurrentWorldInfo() : nullptr;
        }

        // Is this system a client?
        bool IsClient()
        {
            UWorld* const World = (GWorld != nullptr) ? *GWorld : nullptr;
            return World != nullptr && World->NetDriver != nullptr && World->NetDriver->ServerConnection != nullptr;
        }

        // Walks the class chain the way the script VM resolves an event
        // So an override wins over the base declaration
        // Essentially a vtable entry
        UFunction* FindVirtualFunction(UObject* const Object, SFXName const FunctionName)
        {
            for (UStruct* Struct = Object != nullptr ? Object->Class : nullptr; Struct != nullptr;
                Struct = static_cast<UStruct*>(Struct->SuperField))
            {
                for (UField* Child = Struct->Children; Child != nullptr; Child = Child->Next)
                {
                    UFunction* const Function = Child->Cast<UFunction>();
                    if (Function != nullptr && Function->Name == FunctionName)
                    {
                        return Function;
                    }
                }
            }
            return nullptr;
        }

        // Maybe in SDK V3?
        // SFXGameMPContent is not in the SDK, so read the replicated GRI int by name
        // Returns false if it cannot be read
        bool ReadPrivacySetting(AWorldInfo* const WorldInfo, int& Privacy)
        {
            static SFXName const k_privacySetting{ L"PrivacySetting", 0 };
            AGameReplicationInfo* const GRI = WorldInfo != nullptr ? WorldInfo->GRI : nullptr;
            if (GRI == nullptr || GRI->Class == nullptr)
            {
                // No Lobby GRI or standard GRIMP
                return false;
            }

            // Walk class probe to find PrivacySetting int property
            for (UProperty* Property = GRI->Class->PropertyLink; Property != nullptr; Property = Property->PropertyLinkNext)
            {
                if (Property->Name == k_privacySetting && Property->Class == UIntProperty::StaticClass())
                {
                    Privacy = *reinterpret_cast<int const*>(reinterpret_cast<BYTE const*>(GRI) + Property->Offset);
                    return true;
                }
            }
            return false;
        }

        // Looks the string up in the loaded TLKs the way script does (SFXGame.GetSimpleString).
        // A copy of CMatchmaking's helper, which lives in that file's anonymous namespace.
        std::wstring LocalizeBetrayal(std::wstring const& PlayerName)
        {
            std::wstring Result{};
            UClass* const GameClass = ASFXGame::StaticClass();
            if (auto* const Game = GameClass != nullptr ? static_cast<ASFXGame*>(GameClass->ClassDefaultObject) : nullptr) {
                Result = Game->GetSimpleString(k_betrayStrRef, FALSE).Chars();
            }
            // Missing string starts with $ when fetched
            if (Result.empty() || Result == L"$" + std::to_wstring(k_betrayStrRef))
            {
                Result = k_betrayEnglish;
            }

            // Install the token for the name
            if (std::size_t const Slot = Result.find(L"{0}"); Slot != std::wstring::npos)
            {
                Result.replace(Slot, 3, PlayerName);
            }

            return Result;
        }

        void PruneMarks(Clock::time_point const Now)
        {
            std::erase_if(g_marks, [Now](SuppressMark const& Mark) { return Mark.Expires <= Now; });
        }

        // Consumes the oldest live mark whose player the entry mentions
        bool ConsumeMark(std::wstring_view const Entry)
        {
            PruneMarks(Clock::now());
            auto const Found = std::find_if(g_marks.begin(), g_marks.end(), [Entry](SuppressMark const& Mark)
                {
                    return Policy::EntryMentions(Entry, Mark.PlayerName);
                });
            if (Found == g_marks.end())
            {
                return false;
            }

            g_marks.erase(Found);
            return true;
        }

        // Show on all players screen
        // Drops the death's ticker line, then puts betrayal one instead
        void ShowBetrayal(std::wstring const& PlayerName)
        {
            g_marks.push_back({ PlayerName, Clock::now() + k_markLifetime });

            AWorldInfo* const WorldInfo = CurrentWorldInfo();
            ASFXGRI* const GRI = WorldInfo != nullptr && WorldInfo->GRI != nullptr ? WorldInfo->GRI->Cast<ASFXGRI>() : nullptr;
            if (GRI == nullptr)
            {
                return;
            }

            // GetEventTicker is overridden in SFXGRIMP, so it must be resolved from the object's class
            UFunction* const GetTicker = FindVirtualFunction(GRI, SFXName(L"GetEventTicker", 0));
            if (GetTicker == nullptr)
            {
                return;
            }

            // Get the ticker
            ASFXGRI_execGetEventTicker_Parms TickerParms{};
            GRI->ProcessEvent(GetTicker, &TickerParms);
            if (TickerParms.ReturnValue == nullptr)
            {
                return;
            }

            FString const Text{ LocalizeBetrayal(PlayerName).c_str() };

            // Re-entry guard so we don't prune our own message
            g_bShowingTicker = true;
            TickerParms.ReturnValue->AddTickerEntry(Text);
            g_bShowingTicker = false;
        }

        // Send betrayal from host to clients
        void BroadcastBetrayal(std::wstring const& PlayerName)
        {
            ShowBetrayal(PlayerName);

            UWorld* const World = (GWorld != nullptr) ? *GWorld : nullptr;
            UNetDriver* const Driver = World != nullptr ? World->NetDriver : nullptr;
            if (Driver == nullptr)
            {
                return;
            }

            for (UNetConnection* const Connection : Driver->ChildConnections)
            {
                if (Connection == nullptr || Connection->Actor == nullptr)
                {
                    continue;
                }
                Connection->Actor->eventClientMessage(FString{ PlayerName.c_str() }, MessageType(), 0.0f);
            }
        }

        void Punish(APlayerController* const Controller)
        {
            APlayerReplicationInfo* const PRI = Controller->PlayerReplicationInfo;
            std::wstring const PlayerName = PRI != nullptr ? PRI->PlayerName.Chars() : L"";
            APawn* const Pawn = Controller->Pawn;

            // Lobbies and spectators have nothing to kill, but changeteam still must not run.
            if (Pawn == nullptr || Pawn->Health <= 0 || PlayerName.empty())
            {
                MILAN_INFOW(ELOG_EngineHooks, L"team betrayal: blocked changeteam from %s (no live pawn)", PlayerName.c_str());
                return;
            }

            // Find damage type and TakeDamage virtual function on the pawn
            static SFXName const k_takeDamage{ L"TakeDamage", 0 };
            UClass* const DamageType = UObject::FindObject<UClass>(L"Class SFXGameMPContent.SFXDamageType_TeamBetrayal");
            UFunction* const TakeDamage = FindVirtualFunction(Pawn, k_takeDamage);
            if (DamageType == nullptr || TakeDamage == nullptr)
            {
                MILAN_ERRORW(ELOG_EngineHooks, L"team betrayal: blocked changeteam from %s but cannot kill (damage type %s, TakeDamage %s)",
                    PlayerName.c_str(), DamageType != nullptr ? L"found" : L"missing", TakeDamage != nullptr ? L"found" : L"missing");
                return;
            }

            MILAN_INFOW(ELOG_EngineHooks, L"team betrayal: %s used changeteam in a public lobby; killing them", PlayerName.c_str());

            // Posted first so player is marked and ready to suppress the death line
            BroadcastBetrayal(PlayerName);

            // No instigator as it makes it execution instead
            AActor_eventTakeDamage_Parms Parms{};
            Parms.DamageAmount = k_killDamage;
            Parms.EventInstigator = nullptr;
            Parms.DamageType = DamageType;
            Pawn->ProcessEvent(TakeDamage, &Parms);
        }

        bool OnServerChangeTeam(UObject* const Context, UStruct* const Node)
        {
            static FunctionNamedIntercept s_serverChangeTeam{};
            if (!s_serverChangeTeam.Applies(Node, L"Engine", L"PlayerController", L"ServerChangeTeam"))
            {
                // Mark handled.
                return true;
            }

            // Only run as authority
            auto* const Controller = Context != nullptr ? Context->Cast<APlayerController>() : nullptr;
            if (Controller == nullptr || Controller->Role != ROLE_Authority)
            {
                return true;
            }

            // Unreadable privacy runs the original: private lobbies must never be affected.
            AWorldInfo* const WorldInfo = CurrentWorldInfo();
            int Privacy = 0;
            if (!ReadPrivacySetting(WorldInfo, Privacy) || !Policy::ShouldPunish(Privacy))
            {
                // We're in a private lobby
                return true;
            }

            // Public lobbies
            Punish(Controller);
            return false;
        }

        bool OnAddTickerEntry(FFrame* const Stack)
        {
            static FunctionNamedIntercept s_addTickerEntry{};
            if (g_bShowingTicker || g_marks.empty() || !s_addTickerEntry.Applies(Stack->Node, L"SFXGame", L"SFXMPEventTicker", L"AddTickerEntry"))
            {
                // normal
                return true;
            }

            auto const* const Params = reinterpret_cast<USFXMPEventTicker_execAddTickerEntry_Parms const*>(Stack->Locals);
            if (Params == nullptr || Params->Text.Length() == 0)
            {
                // Do nothing.
                return true;
            }

            std::wstring_view const Entry{ Params->Text.Chars(), static_cast<std::size_t>(Params->Text.Length()) };
            return !ConsumeMark(Entry);
        }
    }

    bool OnScriptCall(UObject* const Context, FFrame* const Stack)
    {
        static SFXName const k_serverChangeTeam{ L"ServerChangeTeam", 0 };
        static SFXName const k_addTickerEntry{ L"AddTickerEntry", 0 };

        if (Stack->Node->Name == k_serverChangeTeam)
        {
            return OnServerChangeTeam(Context, Stack->Node);
        }
        if (Stack->Node->Name == k_addTickerEntry)
        {
            return OnAddTickerEntry(Stack);
        }
        return true;
    }

    bool OnProcessEvent(UObject* const Context, UFunction* const Function, void* const Parms)
    {
        static SFXName const k_clientMessage{ L"ClientMessage", 0 };
        if (Function->Name != k_clientMessage)
        {
            return false;
        }

        // Host sends via client message, client receives via client message.
        // Only the client should run this logic
        if (!IsClient() || !Context->IsA<APlayerController>())
        {
            return false;
        }

        auto const* const Message = static_cast<APlayerController_eventClientMessage_Parms const*>(Parms);
        if (!(Message->Type == MessageType()))
        {
            return false;
        }

        ShowBetrayal(std::wstring{ Message->S.Chars(), static_cast<std::size_t>(Message->S.Length()) });
        return true;
    }
}
