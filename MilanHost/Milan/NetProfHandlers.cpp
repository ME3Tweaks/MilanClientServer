#include <intrin.h>

#include "Milan/Base.hpp"
#include "Milan/Misc.hpp"
#include "Milan/NetProfDecls.hpp"
#include "Milan/NetProfStream.hpp"
#include "Proxies/Engine.hpp"
#include "Systems/Output.hpp"


namespace
{
    void TraceUrl(wchar_t const* const InLabel, FURL* const InUrl)
    {
        MILAN_UNUSED_2(InLabel, InUrl);
        MILAN_TRACEW(L"NETPROF (%s): proto=%s, host=%s, map=%s, portal=%s, port=%d, valid=%d",
            InLabel, *InUrl->Protocol, *InUrl->Host, *InUrl->Map, *InUrl->Portal, InUrl->Port, InUrl->Valid);

        if (InUrl->Op.Any())
        {
            FString OpLine{};
            for (auto const& Op : InUrl->Op)
            {
                OpLine.Append(L'?');
                OpLine.Append(Op);
            }

            MILAN_TRACEW(L"  > ops = %s", *OpLine);
        }
    }

    FPackageInfo const* LookupPackageInfo(UTcpipConnection_Proxy* const Connection, FGuid* const Guid)
    {
        for (FPackageInfo const& Package : reinterpret_cast<UPackageMap_Proxy*>(Connection->PackageMap)->List)
        {
            auto const PackGuid = &Package.Guid;
            if (PackGuid->A == Guid->A && PackGuid->B == Guid->B && PackGuid->C == Guid->C && PackGuid->D == Guid->D)
                return &Package;
        }

        return nullptr;
    }

    FString LookupPackageName(UTcpipConnection_Proxy* const Connection, FGuid* const Guid)
    {
        auto const Info = LookupPackageInfo(Connection, Guid);
        return Info ? Info->PackageName.ToString() : L"n/a";
    }
}


// ! Internal handlers called by asm code.
// ========================================

// Used by UChannel::SendBunch handlers: stored by 1st, loaded and cleared by 2nd.
static INT SendBunch1_BitsWrittenBefore = 0;

void MlnUChannelSendBunch1_Handler(FOutBunch* const Bunch)
{
    SendBunch1_BitsWrittenBefore = Bunch->Num;
}

void MlnUChannelSendBunch2_Handler(UChannel* const Channel, FOutBunch* const Bunch)
{
    MILAN_UNUSED_2(Channel, Bunch); // Bunches here and in the 1st handler might not be the same!

    //auto const BitsSent = Bunch->Num - SendBunch1_BitsWrittenBefore;
    //MILAN_TRACEW(L"UChannel::SendBunch: %s, bits = %d", *Channel->StaticName(), BitsSent);

    SendBunch1_BitsWrittenBefore = 0;
}

// Used by UActorChannel::ReplicateActor handlers: stored by 2nd, loaded and cleared by 3rd.
static INT ReplicateActor_BitsWrittenBeforeProp = 0;

void MlnUActorChannelReplicateActor1_Handler(UActorChannel* const Channel, AActor* const Actor)
{
    MILAN_UNUSED_2(Channel, Actor);
    //MILAN_TRACEW(L"UActorChannel::ReplicateActor: %s, actor = %s", *Channel->StaticName(), *Actor->StaticName());

    NETPROF(CaptureActorReplication(reinterpret_cast<UActorChannel_Proxy*>(Channel)->Connection, Actor));
}

void MlnUActorChannelReplicateActor2_Handler(FOutBunch* const Bunch)
{
    ReplicateActor_BitsWrittenBeforeProp = Bunch->Num;
}

void MlnUActorChannelReplicateActor3_Handler(UActorChannel* const Channel, UProperty* const Property, FOutBunch* const Bunch)
{
    MILAN_UNUSED_3(Channel, Property, Bunch);

    auto const BitsWritten = Bunch->Num - ReplicateActor_BitsWrittenBeforeProp;
    //MILAN_TRACEW(L"UActorChannel::ReplicateActor: %s, property = %s, bits = %d", *Channel->GetName(), *Property->StaticName(), BitsWritten);

    NETPROF(CapturePropertyReplication(reinterpret_cast<UActorChannel_Proxy*>(Channel)->Connection, Property, BitsWritten));
    ReplicateActor_BitsWrittenBeforeProp = 0;
}

void MlnInternalReplicateFunction1_Handler(UActorChannel* const Channel, FOutBunch* const Bunch, UFunction* const Function)
{
    MILAN_UNUSED_3(Channel, Bunch, Function);

    //auto* const ActorChan = reinterpret_cast<UActorChannel_Proxy*>(Channel);
    //MILAN_TRACEW(L"NETPROF (InternalReplicateFunction): bits = %d", Bunch ? Bunch->Num : -1);
    //MILAN_TRACEW(L"  Actor:     %s", (ActorChan && ActorChan->Actor) ? *ActorChan->Actor->StaticName() : L"???");
    //MILAN_TRACEW(L"  Function:  %s", Function ? *Function->StaticName() : L"???");
}

void MlnSeamlessTravelTick1_Handler(FURL* const PendingUrl)
{
    FURL* const WorldUrl = &reinterpret_cast<UWorld_Proxy*>(*GWorld)->URL;
    //TraceUrl(L"SeamlessTravelTick / world", WorldUrl);
    //TraceUrl(L"SeamlessTravelTick / pending", PendingUrl);
    MILAN_UNUSED_2(WorldUrl, PendingUrl);
}

void MlnUTcpNetDriverTickDispatch_Handler(UTcpipConnection* const Connection)
{
    auto const TcpConn = reinterpret_cast<UTcpipConnection_Proxy*>(Connection);
    MILAN_TRACEW(L"NETPROF (call to FNetworkNotify::NotifyAcceptedConnection): %s", *TcpConn->FormatRemoteAddress());
    MILAN_UNUSED(Connection);
}

void MlnUWorldNotifyControlMessageHello_Handler(UNetConnection* const Connection, BYTE const Platform, INT const RemoteMinVersion, INT const RemoteVersion)
{
    //auto const TcpConn = reinterpret_cast<UTcpipConnection_Proxy*>(Connection);
    //MILAN_WARNW(L"UWorld::NotifyControlMessage (Hello): Remote = %s, Platform = %hhu, RemoteMinVersion = %#x, RemoteVersion = %#x", *TcpConn->FormatRemoteAddress(), Platform, Byteswap(RemoteMinVersion), Byteswap(RemoteVersion));
    MILAN_UNUSED_4(Connection, Platform, RemoteMinVersion, RemoteVersion);
}

void MlnUWorldNotifyControlMessageNetspeed_Handler(UNetConnection* const Connection, INT const Speed, INT const ClampedSpeed)
{
    //auto const TcpConn = reinterpret_cast<UTcpipConnection_Proxy*>(Connection);
    //MILAN_WARNW(L"UWorld::NotifyControlMessage (Netspeed): Remote = %s, Speed = %d, Clamped = %d", *TcpConn->FormatRemoteAddress(), Speed, ClampedSpeed);
    MILAN_UNUSED_3(Connection, Speed, ClampedSpeed);
}

void MlnUWorldNotifyControlMessageLogin_Handler(UNetConnection* const Connection, FString* const LoginError)
{
    //auto const TcpConn = reinterpret_cast<UTcpipConnection_Proxy*>(Connection);
    //MILAN_WARNW(L"UWorld::NotifyControlMessage (Login): Remote = %s, ClientResponse = %s, RequestURL = %s, LoginError = %s",
    //    *TcpConn->FormatRemoteAddress(), *TcpConn->ClientResponse, *TcpConn->RequestURL, LoginError->Chars());
    MILAN_UNUSED_2(Connection, LoginError);
}

void MlnUWorldNotifyControlMessageFailure_Handler(UNetConnection* const Connection, FString* const Error)
{
    auto const TcpConn = reinterpret_cast<UTcpipConnection_Proxy*>(Connection);
    MILAN_ERRORW(L"UWorld::NotifyControlMessage (Failure): Remote = %s, Error = %s", *TcpConn->FormatRemoteAddress(), Error->Chars());
    MILAN_UNUSED_2(Connection, Error);

    // This injection hasn't been tested yet.
    MILAN_BREAK_ATTACHED();
}

void MlnUWorldNotifyControlMessageUses_Handler(UNetConnection* const Connection, FPackageInfo* const Package)
{
    //auto const TcpConn = reinterpret_cast<UTcpipConnection_Proxy*>(Connection);
    //MILAN_WARNW(L"UWorld::NotifyControlMessage (Uses): Remote = %s, Package = %s, Guid = %s (%s), Gen = %d",
    //    *TcpConn->FormatRemoteAddress(), *Package->PackageName.ToString(), *FormatGuid(Package->Guid), *LookupPackageName(TcpConn, &Package->Guid), Package->RemoteGeneration);
    MILAN_UNUSED_2(Connection, Package);
}

void MlnUWorldNotifyControlMessageHave_Handler(UNetConnection* const Connection, FGuid* const Guid, INT const Generation)
{
    //auto const TcpConn = reinterpret_cast<UTcpipConnection_Proxy*>(Connection);
    //MILAN_WARNW(L"UWorld::NotifyControlMessage (Have): Remote = %s, Guid = %s (%s), Gen = %d",
    //    *TcpConn->FormatRemoteAddress(), *FormatGuid(*Guid), *LookupPackageName(TcpConn, Guid), Generation);
    MILAN_UNUSED_3(Connection, Guid, Generation);
}

void MlnUWorldNotifyControlMessageJoin_Handler(UNetConnection* const Connection, QWORD* const UniqueId, FString* const SpawnError)
{
    //auto const TcpConn = reinterpret_cast<UTcpipConnection_Proxy*>(Connection);
    //MILAN_WARNW(L"UWorld::NotifyControlMessage (Join): Remote = %s, UniqueId = %llu, Error = %s", *TcpConn->FormatRemoteAddress(), *UniqueId, SpawnError->Chars());
    MILAN_UNUSED_3(Connection, UniqueId, SpawnError);
}

void MlnUWorldNotifyControlMessageAbort_Handler(UNetConnection* const Connection, FGuid* const Guid)
{
    //auto const TcpConn = reinterpret_cast<UTcpipConnection_Proxy*>(Connection);
    //MILAN_WARNW(L"UWorld::NotifyControlMessage (Abort): Remote = %s, Guid = %s (%s)",
    //    *TcpConn->FormatRemoteAddress(), *FormatGuid(*Guid), *LookupPackageName(TcpConn, Guid));
    MILAN_UNUSED_2(Connection, Guid);
}

void MlnUWorldNotifyControlMessageUnload_Handler(UNetConnection* const Connection, FGuid* const Guid)
{
    //auto const TcpConn = reinterpret_cast<UTcpipConnection_Proxy*>(Connection);
    //MILAN_WARNW(L"UWorld::NotifyControlMessage (Unload): Remote = %s, Guid = %s (%s)",
    //    *TcpConn->FormatRemoteAddress(), *FormatGuid(*Guid), *LookupPackageName(TcpConn, Guid));
    MILAN_UNUSED_2(Connection, Guid);
}

void MlnUWorldNotifyControlMessageUnknown19_Handler(UNetConnection* const Connection, INT const UnknownCrc, FInBunch* const Bunch)
{
    //auto const TcpConn = reinterpret_cast<UTcpipConnection_Proxy*>(Connection);
    //MILAN_WARNW(L"UWorld::NotifyControlMessage (Unk19_DLM): Remote = %s, DLI CRC32 = 0x%08x", *TcpConn->FormatRemoteAddress(), UnknownCrc);
    MILAN_UNUSED_3(Connection, UnknownCrc, Bunch);
}

void MlnUWorldNotifyControlMessageUnknown21_Handler(UNetConnection* const Connection, FGuid* const Guid, INT const Flag, FInBunch* const Bunch)
{
    //auto const TcpConn = reinterpret_cast<UTcpipConnection_Proxy*>(Connection);
    //MILAN_WARNW(L"UWorld::NotifyControlMessage (Unk21_VIS): Remote = %s, Guid = %s (%s), Flag = %d",
    //    *TcpConn->FormatRemoteAddress(), *FormatGuid(*Guid), *LookupPackageName(TcpConn, Guid), Flag);
    MILAN_UNUSED_4(Connection, Guid, Flag, Bunch);
}

void MlnUNetPendingLevelNotifyControlMessageWelcome_Handler(UNetConnection* const Connection, FString* const Map, FString* const GameName)
{
    //auto const TcpConn = reinterpret_cast<UTcpipConnection_Proxy*>(Connection);
    //MILAN_WARNW(L"UNetPendingLevel::NotifyControlMessage (Welcome): Remote = %s, Map = %s, GameName = %s",
    //    *TcpConn->FormatRemoteAddress(), Map->Chars(), GameName->Chars());
    MILAN_UNUSED_3(Connection, Map, GameName);
}

void MlnUNetPendingLevelNotifyControlMessageUpgrade_Handler(UNetConnection* const Connection, INT const StaticVersion, INT const DynamicVersion)
{
    //auto const TcpConn = reinterpret_cast<UTcpipConnection_Proxy*>(Connection);
    //MILAN_WARNW(L"UNetPendingLevel::NotifyControlMessage (Upgrade): Remote = %s, StaticVersion = 0x%08X, DynamicVersion = 0x%08X",
    //    *TcpConn->FormatRemoteAddress(), StaticVersion, DynamicVersion);
    MILAN_UNUSED_3(Connection, StaticVersion, DynamicVersion);
}

void MlnUNetPendingLevelNotifyControlMessageChallenge_Handler(UNetConnection* const Connection, INT const Version, FString* const Challenge)
{
    //auto const TcpConn = reinterpret_cast<UTcpipConnection_Proxy*>(Connection);
    //MILAN_WARNW(L"UNetPendingLevel::NotifyControlMessage (Challenge): Remote = %s, Version = %d, Challenge = %s",
    //    *TcpConn->FormatRemoteAddress(), Version, Challenge->Chars());
    MILAN_UNUSED_3(Connection, Version, Challenge);
}

void MlnUNetPendingLevelNotifyControlMessageFailure_Handler(UNetConnection* const Connection, FString* const Error)
{
    auto const TcpConn = reinterpret_cast<UTcpipConnection_Proxy*>(Connection);
    MILAN_ERRORW(L"UNetPendingLevel::NotifyControlMessage (Failure): Remote = %s, Error = %s", *TcpConn->FormatRemoteAddress(), Error->Chars());
    MILAN_UNUSED_2(Connection, Error);
}

void MlnUNetPendingLevelNotifyControlMessageUses_Handler(UNetConnection* const Connection, FPackageInfo* const Package)
{
    //auto const TcpConn = reinterpret_cast<UTcpipConnection_Proxy*>(Connection);
    //MILAN_WARNW(L"UNetPendingLevel::NotifyControlMessage (Uses): Remote = %s, Package = %s, Guid = %s (%s), Gen = %d",
    //    *TcpConn->FormatRemoteAddress(), *Package->PackageName.ToString(), *FormatGuid(Package->Guid), *LookupPackageName(TcpConn, &Package->Guid), Package->RemoteGeneration);
    MILAN_UNUSED_2(Connection, Package);
}

void MlnUNetPendingLevelNotifyControlMessageDlMngr_Handler(FString* const Class, FString* const Params, INT* const bUseCompression)
{
    //MILAN_WARNW(L"UNetPendingLevel::NotifyControlMessage (DlMngr): Remote = n/a, Class = %s, Params = %s, bUseCompression = %d",
    //    Class->Chars(), Params->Chars(), *bUseCompression);
    MILAN_UNUSED_3(Class, Params, bUseCompression);
}

void MlnUNetPendingLevelNotifyControlMessageUnload_Handler(UNetConnection* const Connection, FGuid* const Guid)
{
    auto const TcpConn = reinterpret_cast<UTcpipConnection_Proxy*>(Connection);
    MILAN_WARNW(L"UNetPendingLevel::NotifyControlMessage (Unload): Remote = %s, Guid = %s (%s)",
        *TcpConn->FormatRemoteAddress(), *FormatGuid(*Guid), *LookupPackageName(TcpConn, Guid));
    MILAN_UNUSED_2(Connection, Guid);

    // This injection hasn't been tested yet.
    MILAN_BREAK_ATTACHED();
}
