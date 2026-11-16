#include "Components/CCommands.hpp"
#include "Components/CNetworkOverlay.hpp"
#include "Engine/Intercept.hpp"

#include <array>
#include <cstring>


namespace
{

bool gb_netOverlayActive = true;

// ! Packet loss history.
// ----------------------------------------

constexpr double k_packetLossHistorySeconds = 9.0;
constexpr double k_packetLossSampleInterval = 0.1;

struct PacketLossSample
{
    double  Time{};
    int     Total{};
};

std::array<PacketLossSample, 128>   g_packetLossSamples{};
std::size_t                         g_packetLossSampleCount = 0;
double                              g_packetLossLastSampleTime = 0.0;

void ResetPacketLossHistory()
{
    g_packetLossSampleCount = 0;
    g_packetLossLastSampleTime = 0.0;
}

void DropOldestPacketLossSample()
{
    if (g_packetLossSampleCount > 1)
    {
        std::memmove(g_packetLossSamples.data(), g_packetLossSamples.data() + 1,
            (g_packetLossSampleCount - 1) * sizeof(PacketLossSample));
        --g_packetLossSampleCount;
    }
}

void UpdatePacketLossHistory(double const Now, int const Total)
{
    if (g_packetLossSampleCount > 0 && Total < g_packetLossSamples[g_packetLossSampleCount - 1].Total)
    {
        ResetPacketLossHistory();
    }

    bool const bTakeSample = g_packetLossSampleCount == 0 ||
        Now - g_packetLossLastSampleTime >= k_packetLossSampleInterval;
    if (!bTakeSample)
    {
        return;
    }

    if (g_packetLossSampleCount == g_packetLossSamples.size())
    {
        DropOldestPacketLossSample();
    }

    g_packetLossSamples[g_packetLossSampleCount++] = PacketLossSample{ Now, Total };
    g_packetLossLastSampleTime = Now;

    while (g_packetLossSampleCount > 1 && Now - g_packetLossSamples[0].Time > k_packetLossHistorySeconds)
    {
        DropOldestPacketLossSample();
    }
}

int PacketsLostInWindow(double const Now, int const Total, double const Window)
{
    double const Start = Now - Window;

    for (std::size_t i = 0; i < g_packetLossSampleCount; ++i)
    {
        if (g_packetLossSamples[i].Time >= Start)
        {
            int const Lost = Total - g_packetLossSamples[i].Total;
            return Lost > 0 ? Lost : 0;
        }
    }

    return 0;
}

}


// ! CNetworkOverlay implementation.
// ========================================

bool CNetworkOverlay::Initialize(::LESDK::Initializer& Context)
{
    MILAN_UNUSED(Context);

    COMPONENT(CCommands).RegisterCommand(L"netstat", L"toggle",
        [](wchar_t const*)
        {
            gb_netOverlayActive = !gb_netOverlayActive;
            return true;
        });

#ifdef FINAL_RELEASE
    gb_netOverlayActive = false;
#endif

    MILAN_DEBUGW(ELOG_Startup, L"component '%s' initialized", k_typeName);
    return true;
}

void CNetworkOverlay::Terminate(::LESDK::Initializer& Context)
{
    MILAN_UNUSED(Context);
    MILAN_DEBUGW(ELOG_Teardown, L"component '%s' terminating", k_typeName);
}

void CNetworkOverlay::On_UObject_ProcessInternal_Post(UObject* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);

    if (!gb_netOverlayActive)
        return;

    static FunctionStructIntercept SFXHUDMP_PostRender{};
    if (SFXHUDMP_PostRender.Applies(Stack->Node, L"Function SFXGameMPContent.SFXHUDMP.PostRender"))
    {
        auto const HUD = Context->Cast<ABioHUD>();
        auto const Canvas = HUD ? HUD->Canvas : nullptr;

        if (Canvas != nullptr)
        {
            auto const WorldInfo = (*GEngine)->Cast<USFXEngine>()->GetRealWorldInfo();
            auto const PlayerController = WorldInfo ? WorldInfo->GetLocalPlayerController() : nullptr;
            auto const PlayerReplicationInfo = PlayerController ? PlayerController->PlayerReplicationInfo : nullptr;

            if (PlayerReplicationInfo != nullptr)
            {
                double const Now = static_cast<double>(WorldInfo->TimeSeconds);
                UpdatePacketLossHistory(Now, PlayerReplicationInfo->StatPKLTotal);

                auto const NetStatLine = FString::Printf(
                    L"PING = %.3f    L/3 = %d    L/9 = %d    L/TOTAL = %d",
                    PlayerReplicationInfo->ExactPing * 1000.f,
                    PacketsLostInWindow(Now, PlayerReplicationInfo->StatPKLTotal, 3.0),
                    PacketsLostInWindow(Now, PlayerReplicationInfo->StatPKLTotal, 9.0),
                    PlayerReplicationInfo->StatPKLTotal
                );

                Canvas->Font = (*GEngine)->SmallFont;
                Canvas->SetDrawColor(UINT8_MAX, UINT8_MAX, UINT8_MAX, UINT8_MAX);
                Canvas->SetPos(10.f, static_cast<float>(Canvas->SizeY) - 20.f);
                Canvas->DrawTextScaled(NetStatLine, 0.9f, 0.9f);
            }
        }
    }
}
