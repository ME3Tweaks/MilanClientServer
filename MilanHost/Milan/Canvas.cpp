#include "Milan/Base.hpp"
#include "Milan/Canvas.hpp"
#include "Milan/Hooks.hpp"
#include "Milan/Misc.hpp"
#include "Systems/Output.hpp"


// ! CanvasDrawer implementation.
// ========================================

void CanvasDrawer::UpdateCanvas(UCanvas* const InCanvas)
{
    MILAN_CHECKW(InCanvas != nullptr, L"");
    Canvas = InCanvas;
}

void CanvasDrawer::Reset()
{
    SetPos(0.f, 0.f);
    SetColor(FColor{ 0xFF, 0xFF, 0xFF, 0xFF });
}

void CanvasDrawer::DrawText(std::wstring_view Text, bool const bNewLine, ETextAlign const Align, float const Scale)
{
    //MILAN_CHECKW(!Text.empty(), L"");
    MILAN_CHECKW(IsValidAlign(Align), L"");
    MILAN_CHECKW(Scale > 0.f, L"");
    MILAN_CHECKW(Canvas->Font != nullptr, L"");

    int SizeX = 0, SizeY = 0;
    FFontRenderInfo const FontRenderInfo{};

    if (Align == ETA_Right)
    {
        // Get size of the rendered text without actually drawing it, use it to calculate right-aligned position.
        auto const [DX, DY] = GetTextSize(Text, Scale);
        AdjustPos(-(float)DX, 0.f);
    }

    if (Text.empty())
    {
        // Get size of bogus rendered text to skip an empty line.
        AdjustPos(0.f, GetTextSize(L"---", Scale).Y);
    }
    else
    {
        (*UCanvas_WrappedPrint)(Canvas, TRUE, &SizeX, &SizeY, Canvas->Font, Scale, Scale, Align == ETA_Center, Text.data(), &FontRenderInfo);
    }

    if (!bNewLine)
    {
        AdjustPos((float)SizeX, 0.f);
    }
    else
    {
        AdjustPos(0.f, (float)SizeY);
    }
}

void CanvasDrawer::DrawTextColored(std::wstring_view const Text, FColor const Color, bool const bNewLine, ETextAlign const Align, float const Scale)
{
    PushColor(Color);
    DrawText(Text, bNewLine, Align, Scale);
    PopColor();
}

FVector2D CanvasDrawer::GetTextSize(std::wstring_view const Text, float const Scale)
{
    MILAN_CHECKW(!Text.empty(), L"");
    MILAN_CHECKW(Scale > 0.f, L"");

    CStringSizeParams Params{};
    std::memset(&Params, 0, sizeof Params);

    Params.Scaling = FVector2D{ .X = Scale, .Y = Scale };
    Params.DrawFont = Canvas->Font;

    (*UUIString_StringSize)(&Params, Text.data(), nullptr, TRUE);
    return Params.OutSize;
}

void CanvasDrawer::PushColor(FColor const Color)
{
    ColorStack.Add(Canvas->DrawColor);
    Canvas->DrawColor = Color;
}

void CanvasDrawer::PushColor(FLinearColor const Color)
{
    FColor const Converted
    {
        .B = static_cast<BYTE>(Color.B * 255.f),
        .G = static_cast<BYTE>(Color.G * 255.f),
        .R = static_cast<BYTE>(Color.R * 255.f),
        .A = static_cast<BYTE>(Color.A * 255.f),
    };
    PushColor(Converted);
}

void CanvasDrawer::PushColor(byte const R, byte const G, byte const B, byte const A)
{
    PushColor(FColor{ R, G, B, A });
}

FColor CanvasDrawer::PopColor()
{
    MILAN_CHECKW(!ColorStack.Empty(), L"");
    Canvas->DrawColor = ColorStack(ColorStack.Count() - 1);
    ColorStack.Erase(ColorStack.Count() - 1);
    return Canvas->DrawColor;
}

void CanvasDrawer::SetBackground(FColor const Color)
{
    PushColor(Color);
    FVector2D const Stored = GetPos();
    SetPos(0.f, 0.f);
    Canvas->DrawRect((float)Canvas->SizeX, (float)Canvas->SizeY, Canvas->DefaultTexture);
    SetPos(Stored.X, Stored.Y);
    PopColor();
}

void CanvasDrawer::SetColor(FColor const Color)
{
    ColorStack.Clear();
    Canvas->DrawColor = Color;
}

FVector2D CanvasDrawer::GetPos() const
{
    return FVector2D{ .X = Canvas->CurX, .Y = Canvas->CurY };
}

void CanvasDrawer::AdjustPos(float const DX, float const DY)
{
    Canvas->CurX += DX;
    Canvas->CurY += DY;
}

void CanvasDrawer::SetPos(float const X, float const Y)
{
    SetPosX(X);
    SetPosY(Y);
}

void CanvasDrawer::SetPosX(float X)
{
    if (X < 0.f) X += Canvas->SizeX;
    Canvas->CurX = X;
}

void CanvasDrawer::SetPosY(float Y)
{
    if (Y < 0.f) Y += Canvas->SizeY;
    Canvas->CurY = Y;
}

void CanvasDrawer::DrawLine(float const X1, float const Y1, float const X2, float const Y2)
{
    Canvas->Draw2DLine(X1, Y1, X2, Y2, Canvas->DrawColor);
}

void CanvasDrawer::DrawHorizontalLine(float X1, float X2)
{
    float const Y = Canvas->CurY;
    if (X1 < 0.f) X1 += Canvas->SizeX;
    if (X2 < 0.f) X2 += Canvas->SizeX;
    Canvas->Draw2DLine(X1, Y, X2, Y, Canvas->DrawColor);
}

void CanvasDrawer::DrawBoldHorizontalLine(float X1, float X2, unsigned char const Factor)
{
    if (X1 < 0.f) X1 += Canvas->SizeX;
    if (X2 < 0.f) X2 += Canvas->SizeX;

    MILAN_CHECKW(Factor > 0, L"");
    for (unsigned char i = 0; i < Factor; ++i)
    {
        Canvas->Draw2DLine(X1, Canvas->CurY, X2, Canvas->CurY, Canvas->DrawColor);
        Canvas->CurY += 1.f;
    }
}


// ! MilanCanvasDrawer implementation.
// ========================================

void MilanCanvasDrawer::UpdateGlobals(USFXEngine* const InEngine)
{
    MILAN_CHECKW(InEngine != nullptr, L"");
    Engine = InEngine;
}

void MilanCanvasDrawer::PushIndent()
{
    Indent += k_indentSize;
}

void MilanCanvasDrawer::PopIndent()
{
    Indent = std::max<float>(Indent - k_indentSize, 0.f);
}

void MilanCanvasDrawer::DrawHeader(std::wstring_view const Subtitle,
    std::wstring_view const Description, UObject* const Target, FColor const Color)
{
    MILAN_CHECKW(!Subtitle.empty(), L"");
    MILAN_CHECKW(!Description.empty(), L"");

    PushColor(Color);

    SetPos(k_canvasPadding, k_canvasPadding);
    DrawText(L"Milan Debug HUD / ", false, ETA_Left);
    DrawText(Subtitle, false, ETA_Left);

    if (Target != nullptr)
    {
        SetPosX(0.f);
        DrawText(FString::Printf(L"<< %s >>", *FormatObjectName(Target)), false, ETA_Center);
    }

    SetPosX(-k_canvasPadding);
    DrawText(Description, true, ETA_Right);

    AdjustPos(0.f, k_canvasPadding / 2);
    DrawHorizontalLine(k_canvasPadding, -k_canvasPadding);

    AdjustPos(0.f, k_canvasPadding / 2);

    PopColor();
}

void MilanCanvasDrawer::DrawString(std::wstring_view const String)
{
    SetPosX(k_canvasPadding + Indent);
    DrawText(String, true);
}

void MilanCanvasDrawer::DrawProperty(std::wstring_view const Name, std::wstring_view const String)
{
    FColor NameColor = Canvas->DrawColor;
    if (NameColor.A > 250) NameColor.A = static_cast<unsigned char>(NameColor.A * 0.8);

    PushColor(NameColor);

    SetPosX(k_canvasPadding + Indent);
    DrawText(Name, false);
    DrawText(L": ", false);

    PopColor();

    SetPosX(k_canvasPadding + 220.f);
    DrawText(String, true);
}

void MilanCanvasDrawer::DrawSeparator()
{
    float const LineHeight = GetTextSize(L"---", 1.f).Y;
    AdjustPos(0.f, LineHeight);
}

FString const& MilanCanvasDrawer::FormatObjectName(UObject* const Object)
{
    MILAN_CHECKW(Object != nullptr, L"");
    TargetName.Clear();

    if (Object->Class != nullptr)
    {
        ::LESDK::AppendObjectName(Object->Class, TargetName, SFXName::k_formatBasic);
        TargetName.Append(L" ");

        if (Object->Outer != nullptr)
        {
            if (Object->Outer->Outer != nullptr)
            {
                ::LESDK::AppendObjectName(Object->Outer->Outer, TargetName, SFXName::k_formatBasic);
                TargetName.Append(L".");
            }

            ::LESDK::AppendObjectName(Object->Outer, TargetName, SFXName::k_formatBasic);
            TargetName.Append(L".");
        }

        ::LESDK::AppendObjectName(Object, TargetName, SFXName::k_formatInstanced);
    }
    else
    {
        TargetName.Append(L"(none)");
    }

    return TargetName;
}


// ! Implementation of canvas profilers.
// ========================================

wchar_t const* NetModeString(unsigned char const Mode)
{
    if (Mode == NM_Standalone)          return L"Standalone";
    if (Mode == NM_Client)              return L"Client";
    if (Mode == NM_ListenServer)        return L"ListenServer";
    if (Mode == NM_DedicatedServer)     return L"DedicatedServer";

    return L"???";
}

wchar_t const* NetRoleString(unsigned char const Role)
{
    if (Role == ROLE_None)              return L"ROLE_None";
    if (Role == ROLE_SimulatedProxy)    return L"ROLE_SimulatedProxy";
    if (Role == ROLE_AutonomousProxy)   return L"ROLE_AutonomousProxy";
    if (Role == ROLE_Authority)         return L"ROLE_Authority";

    return L"???";
}

void ProfilePendingPackages(MilanCanvasDrawer& Drawer, UObject* Target)
{
    MILAN_UNUSED_2(Drawer, Target);

    auto const World = reinterpret_cast<UWorld_Proxy*>(*GWorld);
    auto const NetDriver = reinterpret_cast<UNetDriver_Proxy*>(World->NetDriver);
    auto const ServerConnection = reinterpret_cast<UNetConnection_Proxy*>(NetDriver->ServerConnection);

    for (FPackageInfo const& Pending : ServerConnection->PendingPackageInfos)
    {
        Drawer.DrawString(Pending.PackageName.ToString());
        Drawer.PushIndent();
        Drawer.DrawProperty(L"Guid", *FormatGuid(Pending.Guid));
        Drawer.DrawProperty(L"Parent", Pending.Parent ? Pending.Parent->GetFullName() : L"n/a");
        Drawer.DrawProperty(L"ForcedBase", Pending.ForcedExportBasePackageName.ToString());
        Drawer.PopIndent();
    }
}

void ProfilePlayerControllers(MilanCanvasDrawer& Drawer, UObject* Target)
{
    MILAN_UNUSED_2(Drawer, Target);

    Drawer.SetBackground(FColor{ .B = 0, .G = 0, .R = 0, .A = 25 });

    for (TypedObjectIterator<APlayerController, true, false> Iter; Iter; ++Iter)
    {
        APlayerController* const Controller = *Iter;
        Drawer.DrawString(Controller->StaticFullName());
        Drawer.PushIndent();
        Drawer.DrawProperty(L"Pawn", Controller->Pawn ? Controller->Pawn->StaticFullName() : L"n/a");
        Drawer.DrawProperty(L"PRI", Controller->PlayerReplicationInfo ? Controller->PlayerReplicationInfo->StaticFullName() : L"n/a");
        if (Controller->PlayerReplicationInfo != nullptr)
        {
            Drawer.DrawProperty(L"PRI.Owner", Controller->PlayerReplicationInfo->Owner ? Controller->PlayerReplicationInfo->Owner->StaticFullName() : L"n/a");
            Drawer.DrawProperty(L"PRI.UniqueId", FString::Printf(L"%u:%u", Controller->PlayerReplicationInfo->UniqueId.Uid.A, Controller->PlayerReplicationInfo->UniqueId.Uid.B));
            Drawer.DrawProperty(L"PRI.SavedAddress", Controller->PlayerReplicationInfo->SavedNetworkAddress);
        }
        Drawer.DrawProperty(L"Actor", FString::Printf(L"bDeleteMe=%d, bStatic=%d, bGameRelevant=%d",
            !!Controller->bDeleteMe, !!Controller->bStatic, !!Controller->bGameRelevant));
        Drawer.DrawProperty(L"Actor.Owner", Controller->Owner ? Controller->Owner->StaticFullName() : L"n/a");
        Drawer.DrawProperty(L"Actor.WI", Controller->WorldInfo ? Controller->WorldInfo->StaticFullName() : L"n/a");
        Drawer.DrawProperty(L"Net", FString::Printf(L"Role=%s, RemoteRole=%s",
            NetRoleString(Controller->Role), NetRoleString(Controller->RemoteRole)));
        Drawer.DrawProperty(L"Net", FString::Printf(L"NetTag=%d, NetUpdateTime=%f, NetUpdateFrequency=%f, NetPriority=%f, LastNetUpdateTime=%f",
            Controller->NetTag, Controller->NetUpdateTime, Controller->NetUpdateFrequency, Controller->NetPriority, Controller->LastNetUpdateTime));

        Drawer.PopIndent();
    }

}

void ProfileNetwork(MilanCanvasDrawer& Drawer, UObject* const Target)
{
    MILAN_UNUSED_2(Drawer, Target);

    auto* const Engine = (*GEngine)->Cast<USFXEngine>();
    auto* const WorldInfo = Engine->GetRealWorldInfo();

    if (WorldInfo == nullptr)
    {
        Drawer.PushColor(Drawer.k_warnColor);
        Drawer.DrawString(L"WorldInfo not available, aborting...");
        Drawer.PopColor();
        return;
    }

    Drawer.SetBackground(FColor{ .B = 0, .G = 0, .R = 0, .A = 25 });

    // WorldInfo

    Drawer.DrawString(FString::Printf(L"WorldInfo: %s", *WorldInfo->StaticName()));

    Drawer.PushIndent();
    Drawer.DrawProperty(L"URL", WorldInfo->GetLocalURL());
    Drawer.DrawProperty(L"World", (*GWorld)->StaticFullName());
    Drawer.DrawProperty(L"GameInfo", WorldInfo->Game ? WorldInfo->Game->StaticName() : L"n/a");
    Drawer.DrawProperty(L"ReplicationInfo", WorldInfo->GRI ? WorldInfo->GRI->StaticName() : L"n/a");
    Drawer.DrawProperty(L"NetMode", FString::Printf(L"%d (%s)", WorldInfo->NetMode, NetModeString(WorldInfo->NetMode)));
    Drawer.PopIndent();

    Drawer.DrawSeparator();

    // NetDriver

    auto* const World = reinterpret_cast<UWorld_Proxy*>(*GWorld);
    auto* const NetDriver = reinterpret_cast<UNetDriver_Proxy*>(World->NetDriver);

    Drawer.DrawString(FString::Printf(L"World: %s", *World->StaticName()));
    Drawer.DrawString(FString::Printf(L"NetDriver: %s", NetDriver ? *NetDriver->StaticName() : L"n/a"));

    if (NetDriver && WorldInfo->NetMode == NM_ListenServer)
    {
        Drawer.PushIndent();

        for (UINT i = 0u; i < NetDriver->ChildConnections.Count(); ++i)
        {
            //MILAN_CHECKW(ChildConnIter->IsA<UChildConnection>(), L"");

            auto* const ChildConnIter = NetDriver->ChildConnections(i);
            auto* const ChildConn = reinterpret_cast<UNetConnection_Proxy*>(ChildConnIter);

            Drawer.DrawString(FString::Printf(L"ChildConnections(%d): %s", i, *ChildConnIter->StaticName()));

            Drawer.PushIndent();
            Drawer.DrawProperty(L"Viewer", ChildConn->Viewer ? ChildConn->Viewer->StaticFullName() : L"n/a");
            Drawer.DrawProperty(L"ClientWorldPackageName", ChildConn->ClientWorldPackageName.ToString());
            Drawer.DrawString(L"Actor channels");
            unsigned int ChannelIndex = 0;
            for (auto const& [Actor, Channel] : ChildConn->ActorChannels)
            {
                Drawer.PushIndent();
                MILAN_CHECKW(Channel != nullptr, L"");
                auto* const ChannelProxy = reinterpret_cast<UActorChannel_Proxy*>(Channel);
                DrawActorChannel(Drawer, ChannelProxy, ChannelIndex++);
                Drawer.PopIndent();

                if (Actor != ChannelProxy->Actor)
                {
                    MILAN_WARNW(L"server has actor channel with actor != channel->actor");
                    MILAN_WARNW(L"  actor = %s", Actor ? *Actor->StaticFullName() : L"n/a");
                    MILAN_WARNW(L"  channel->actor = %s", ChannelProxy->Actor ? *ChannelProxy->Actor->StaticFullName() : L"n/a");
                }
            }
            Drawer.DrawString(L"Client visible levels");
            unsigned int VisibleIndex = 0;
            for (auto const VisibleLevelName : ChildConn->ClientVisibleLevelNames)
            {
                Drawer.PushIndent();
                Drawer.DrawProperty(FString::Printf(L"%d", VisibleIndex++), VisibleLevelName.ToString());
                Drawer.PopIndent();
            }
            Drawer.DrawString(L"Pending packages");
            unsigned int PendingIndex = 0;
            for (auto const& PendingPackage : ChildConn->PendingPackageInfos)
            {
                Drawer.PushIndent();
                Drawer.DrawProperty(FString::Printf(L"%d", PendingIndex++),
                    FString::Printf(L"Name=%s, Parent=%s, Guid=%s",
                        *PendingPackage.PackageName.ToString(),
                        PendingPackage.Parent ? *PendingPackage.Parent->StaticName() : L"n/a",
                        *FormatGuid(PendingPackage.Guid)));
                Drawer.PopIndent();
            }
            Drawer.PopIndent();
        }

        Drawer.PopIndent();
    }
    else if (NetDriver && NetDriver->ServerConnection)
    {
        Drawer.PushIndent();

        auto* const ServerConn = reinterpret_cast<UNetConnection_Proxy*>(NetDriver->ServerConnection);
        Drawer.DrawProperty(L"Viewer", ServerConn->Viewer ? ServerConn->Viewer->StaticFullName() : L"n/a");

        Drawer.DrawString(L"ServerConnection:");
        Drawer.PushIndent();

        Drawer.DrawString(L"Actor channels");
        unsigned int ChannelIndex = 0;
        for (auto const& [Actor, Channel] : ServerConn->ActorChannels)
        {
            Drawer.PushIndent();
            MILAN_CHECKW(Channel != nullptr, L"");
            auto* const ChannelProxy = reinterpret_cast<UActorChannel_Proxy*>(Channel);
            DrawActorChannel(Drawer, ChannelProxy, ChannelIndex++);
            Drawer.PopIndent();

            if (Actor != ChannelProxy->Actor)
            {
                MILAN_WARNW(L"client has actor channel with actor != channel->actor");
                MILAN_WARNW(L"  actor = %s", Actor ? *Actor->StaticFullName() : L"n/a");
                MILAN_WARNW(L"  channel->actor = %s", ChannelProxy->Actor ? *ChannelProxy->Actor->StaticFullName() : L"n/a");
            }
        }

        Drawer.PopIndent();
        Drawer.PopIndent();
    }
}

void ProfileWorldInfo(MilanCanvasDrawer& Drawer, UObject* const Target)
{
    MILAN_UNUSED_2(Drawer, Target);
}

void DrawActorChannel(MilanCanvasDrawer& Drawer, UActorChannel_Proxy* const Channel, unsigned int const Index)
{
    static FColor s_flagsActorColor{ .B = 210, .G = 210, .R = 200, .A = 255 };
    static FColor s_flagsBaseColor{ .B = 255, .G = 185, .R = 185, .A = 255 };
    static FColor s_flagsDerivedColor{ .B = 185, .G = 255, .R = 185, .A = 255 };

    //// I'd very much like to know what's supposed to be in here...
    //MILAN_CHECKW(Channel->Unknown_114.Count() == 0, L"");

    Drawer.DrawString(FString::Printf(L"Channel %d: %s", Index, Channel->Actor ? *Channel->Actor->StaticFullName() : L"n/a"));
    Drawer.PushIndent();

    // > Replicated properties.
    // ----------------------------------------

    //unsigned int PropertyIndex = 0;
    //for (auto const& [Offset, Property] : Channel->ReplicatedProperties)
    //{
    //    Drawer.DrawString(FString::Printf(L"%d. %s -> %d", PropertyIndex++, Property ? *Property->StaticName() : L"n/a", Offset));
    //}

    // > Relevancy timer.
    // ----------------------------------------

    //auto* const NetDriver = (UNetDriver_Proxy*)(((UWorld_Proxy*)Channel->World)->NetDriver);
    //Drawer.DrawProperty(L"relevancy timer", FString::Printf(L"Time=%f, RelevantTime=%f, RelevantTimeout=%f",
    //    NetDriver->Time, Channel->RelevantTime, NetDriver->RelevantTimeout));

    // > Actors and ownerships.
    // ----------------------------------------

    //Drawer.DrawProperty(L"Actor->Owner", (Channel->Actor && Channel->Actor->Owner) ? Channel->Actor->Owner->StaticFullName() : L"n/a");
    //Drawer.DrawProperty(L"Connection->Actor", (Channel->Connection && Channel->Connection->Actor) ? Channel->Connection->Actor->StaticFullName() : L"n/a");

    // > Channel actor flags.
    // ----------------------------------------

    //if (AActor* Actor = Channel->Actor)
    //{
    //    Drawer.PushColor(s_flagsActorColor);
    //    Drawer.DrawProperty(L"Channel actor", FString::Printf(L"Role=%s, RemoteRole=%s, bNetInitial=%d, bNetOwner=%d, bGameRelevant=%d, bNetTemporary=%d, bNetDirty=%d, SkipPropRepl=%d",
    //        NetRoleString(Actor->Role), NetRoleString(Actor->RemoteRole), !!Actor->bNetInitial, !!Actor->bNetOwner, !!Actor->bGameRelevant, !!Actor->bNetTemporary, !!Actor->bNetDirty, !!Actor->bSkipActorPropertyReplication));
    //    Drawer.PopColor();
    //}

    // > Channel flags.
    // ----------------------------------------

    //Drawer.PushColor(s_flagsBaseColor);
    //Drawer.DrawProperty(L"Flags (base)", FString::Printf(L"bOpenAcked=%d, bClosing=%d, bOpenTemporary=%d, bBroken=%d, bTornOff=%d",
    //    !!Channel->bOpenAcked, !!Channel->bClosing, !!Channel->bOpenTemporary, !!Channel->bBroken, !!Channel->bTornOff));
    //Drawer.PopColor();
    //Drawer.PushColor(s_flagsDerivedColor);
    //Drawer.DrawProperty(L"Flags (derived)", FString::Printf(L"SpawnAcked=%d, ActorDirty=%d, MustStayDirty=%d, StillInitial=%d, IsReplicating=%d",
    //    !!Channel->SpawnAcked, !!Channel->ActorDirty, !!Channel->bActorMustStayDirty, !!Channel->bActorStillInitial, !!Channel->bIsReplicatingActor));
    //Drawer.PopColor();

    Drawer.PopIndent();
}
