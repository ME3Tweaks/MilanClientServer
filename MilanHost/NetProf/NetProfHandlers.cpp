#include <intrin.h>

#include "Base/Base.hpp"
#include "Engine/Hooks.hpp"
#include "Milan/Misc.hpp"
#include "NetProf/NetProfDecls.hpp"
#include "NetProf/NetProfStream.hpp"


namespace
{
    void TraceUrl(SFXName const Category, wchar_t const* const InLabel, FURL* const InUrl)
    {
        MILAN_UNUSED_2(InLabel, InUrl);
        MILAN_TRACEW(Category, L"%s: proto=%s, host=%s, map=%s, portal=%s, port=%d, valid=%d",
            InLabel, *InUrl->Protocol, *InUrl->Host, *InUrl->Map, *InUrl->Portal, InUrl->Port, InUrl->Valid);

        if (InUrl->Op.Any())
        {
            FString OpLine{};
            for (auto const& Op : InUrl->Op)
            {
                OpLine.Append(L'?');
                OpLine.Append(Op);
            }

            MILAN_TRACEW(Category, L"  > ops = %s", *OpLine);
        }
    }

    FPackageInfo const* LookupPackageInfo(UTcpipConnection* const Connection, FGuid* const Guid)
    {
        //for (FPackageInfo const& Package : reinterpret_cast<UPackageMap_Proxy*>(Connection->PackageMap)->List)
        for (FPackageInfo const& Package : Connection->PackageMap->List)
        {
            auto const PackGuid = &Package.Guid;
            if (PackGuid->A == Guid->A && PackGuid->B == Guid->B && PackGuid->C == Guid->C && PackGuid->D == Guid->D)
                return &Package;
        }

        return nullptr;
    }

    FString LookupPackageName(UTcpipConnection* const Connection, FGuid* const Guid)
    {
        auto const Info = LookupPackageInfo(Connection, Guid);
        return Info ? Info->PackageName.ToString() : L"n/a";
    }
}


// ! Internal handlers called by asm code.
// ========================================

void MlnUNetConnectionReceivedPacket1_Handler(UNetConnection* const Connection, FInBunch* const Bunch)
{
    auto const Channel = Connection->Channels[Bunch->ChannelIndex];

#if !defined(_FINAL_RELEASE) && false
    if (Channel && Channel->IsA<UActorChannel>())
    {
        auto const ActorChannel = reinterpret_cast<UActorChannel*>(Channel);
        MILAN_TRACEW(ELOG_NetHooksTrace, L"UNetConnection::ReceivedPacket: channel = %s, actor = %s",
            *Channel->GetName(), ActorChannel->Actor ? *ActorChannel->Actor->GetName() : L"n/a");
    }
#endif

    if (!Bunch->bReliable && (!Bunch->bOpen || !Bunch->bClose) && (!Channel || Channel->OpenPacketId == -1))
    {
        auto const ChannelDesc = Channel ? Channel->GetName() : L"n/a";
        MILAN_WARNW(ELOG_NetHooks, L"UNetConnection::ReceivedPacket: unreliable bunch before open (channel = %s)", *ChannelDesc);
    }

    if (Bunch->bReliable && Bunch->ChannelSequence <= Connection->InReliable[Bunch->ChannelIndex])
    {
        auto const ChannelDesc = Channel ? Channel->GetName() : L"n/a";
        MILAN_WARNW(ELOG_NetHooks, L"UNetConnection::ReceivedPacket: reliable bunch outdated (channel = %s)", *ChannelDesc);
    }
}

// Used by UChannel::SendBunch handlers: stored by 1st, loaded and cleared by 2nd.
static INT SendBunch1_BitsWrittenBefore = 0;

void MlnUChannelSendBunch1_Handler(FOutBunch* const Bunch)
{
    SendBunch1_BitsWrittenBefore = Bunch->Num;
}

void MlnUChannelSendBunch2_Handler(UChannel* const Channel, FOutBunch* const Bunch)
{
    // Bunches here and in the 1st handler might not be the same!

    auto* const Connection = Channel->Connection->Cast<UTcpipConnection>();
    MILAN_UNUSED_2(Channel, Bunch);

    auto const BitsSent = Bunch->Num - SendBunch1_BitsWrittenBefore;
    MILAN_TRACEW(ELOG_NetHooksTrace, L"UChannel::SendBunch: %s, remote = %s, bits = %d",
        *Channel->StaticName(), *Connection->FormatRemoteAddress(), BitsSent);

    NETPROF(CaptureSendBunch(Channel, BitsSent));
    SendBunch1_BitsWrittenBefore = 0;
}

// Used by UActorChannel::ReplicateActor handlers: stored by 2nd, loaded and cleared by 3rd.
static INT ReplicateActor_BitsWrittenBeforeProp = 0;

void MlnUActorChannelReplicateActor1_Handler(UActorChannel* const Channel, AActor* const Actor)
{
    MILAN_UNUSED_2(Channel, Actor);
    MILAN_TRACEW(ELOG_NetHooksRepl, L"UActorChannel::ReplicateActor: %s, actor = %s", *Channel->GetName(), *Actor->GetName());

    NETPROF(CaptureActorReplication(Channel->Connection, Actor));
}

void MlnUActorChannelReplicateActor2_Handler(FOutBunch* const Bunch)
{
    ReplicateActor_BitsWrittenBeforeProp = Bunch->Num;
}

void MlnUActorChannelReplicateActor3_Handler(UActorChannel* const Channel, UProperty* const Property, FOutBunch* const Bunch)
{
    MILAN_UNUSED_3(Channel, Property, Bunch);

    auto const BitsWritten = Bunch->Num - ReplicateActor_BitsWrittenBeforeProp;
    MILAN_TRACEW(ELOG_NetHooksRepl, L"UActorChannel::ReplicateActor: %s, property = %s, bits = %d", *Channel->GetName(), *Property->GetName(), BitsWritten);

    NETPROF(CapturePropertyReplication(Channel->Connection, Property, BitsWritten));
    ReplicateActor_BitsWrittenBeforeProp = 0;
}

void MlnInternalReplicateFunction1_Handler(UActorChannel* const Channel, FOutBunch* const Bunch, UFunction* const Function)
{
    MILAN_UNUSED_3(Channel, Bunch, Function);

    MILAN_TRACEW(ELOG_NetHooksRepl, L"InternalReplicateFunction: bits = %d, actor = %s, func = %s",
        Bunch ? Bunch->Num : -1,
        (Channel && Channel->Actor) ? *Channel->Actor->StaticName() : L"???",
        Function ? *Function->StaticName() : L"???");

    NETPROF(CaptureRemoteFunction(Channel->Connection, Channel->Actor, Function, Bunch->Num));
}

void MlnSeamlessTravelTick1_Handler(FURL* const PendingUrl)
{
    FURL* const WorldUrl = &(*GWorld)->URL;
    TraceUrl(ELOG_NetHooks, L"SeamlessTravelTick / world", WorldUrl);
    TraceUrl(ELOG_NetHooks, L"SeamlessTravelTick / pending", PendingUrl);
    NETPROF(CaptureLevelStart(PendingUrl));
    MILAN_UNUSED_2(WorldUrl, PendingUrl);
}

void MlnUTcpNetDriverTickDispatch_Handler(UTcpipConnection* const Connection)
{
    MILAN_TRACEW(ELOG_NetHooks, L"FNetworkNotify::NotifyAcceptedConnection: %s", *Connection->FormatRemoteAddress());
    NETPROF(CaptureSessionEvent(Connection, L"ACCEPT", L""));
    MILAN_UNUSED(Connection);
}

void MlnUWorldNotifyControlMessageHello_Handler(UNetConnection* const Connection, BYTE const Platform, INT const StaticVersion, INT const DynamicVersion)
{
    auto const TcpConn = Connection->Cast<UTcpipConnection>();
    MILAN_CHECKW(TcpConn != nullptr, L"");

    MILAN_TRACEW(ELOG_NetHooks, L"UWorld::NotifyControlMessage (Hello): Remote = %s, Platform = %hhu, StaticVersion = %#x, DynamicVersion = %#x", *TcpConn->FormatRemoteAddress(), Platform, Byteswap(StaticVersion), Byteswap(DynamicVersion));
    NETPROF(CaptureControlMessage(Connection, false, NM_Hello, FString::Printf(L"Platform=%hhu, StaticVersion=%#x, DynamicVersion=%#x", Platform, StaticVersion, DynamicVersion)));

    MILAN_UNUSED_5(Connection, TcpConn, Platform, StaticVersion, DynamicVersion);
}

void MlnUWorldNotifyControlMessageNetspeed_Handler(UNetConnection* const Connection, INT const Speed, INT const ClampedSpeed)
{
    auto const TcpConn = Connection->Cast<UTcpipConnection>();
    MILAN_CHECKW(TcpConn != nullptr, L"");

    MILAN_TRACEW(ELOG_NetHooks, L"UWorld::NotifyControlMessage (Netspeed): Remote = %s, Speed = %d, Clamped = %d", *TcpConn->FormatRemoteAddress(), Speed, ClampedSpeed);
    NETPROF(CaptureControlMessage(Connection, false, NM_Netspeed, FString::Printf(L"Speed=%d, Clamped=%d", Speed, ClampedSpeed)));
    MILAN_UNUSED_4(Connection, TcpConn, Speed, ClampedSpeed);
}

void MlnUWorldNotifyControlMessageLogin_Handler(UNetConnection* const Connection, FString* const LoginError)
{
    auto const TcpConn = Connection->Cast<UTcpipConnection>();
    MILAN_CHECKW(TcpConn != nullptr, L"");

    MILAN_TRACEW(ELOG_NetHooks, L"UWorld::NotifyControlMessage (Login): Remote = %s, ClientResponse = %s, RequestURL = %s, LoginError = %s",
        *TcpConn->FormatRemoteAddress(), *TcpConn->ClientResponse, *TcpConn->RequestURL, LoginError->Chars());
    NETPROF(CaptureControlMessage(Connection, false, NM_Login,
        FString::Printf(L"ClientResponse=%s, RequestURL=%s, LoginError=%s",
            *TcpConn->ClientResponse, *TcpConn->RequestURL, LoginError->Chars())));
    MILAN_UNUSED_3(TcpConn, Connection, LoginError);
}

void MlnUWorldNotifyControlMessageFailure_Handler(UNetConnection* const Connection, FString* const Error)
{
    auto const TcpConn = Connection->Cast<UTcpipConnection>();
    MILAN_CHECKW(TcpConn != nullptr, L"");

    MILAN_ERRORW(ELOG_NetHooks, L"UWorld::NotifyControlMessage (Failure): Remote = %s, Error = %s", *TcpConn->FormatRemoteAddress(), Error->Chars());
    NETPROF(CaptureControlMessage(Connection, false, NM_Failure, *Error));

    MILAN_BREAK_ATTACHED();  // This injection hasn't been tested yet.
    MILAN_UNUSED_3(TcpConn, Connection, Error);
}

void MlnUWorldNotifyControlMessageUses_Handler(UNetConnection* const Connection, FPackageInfo* const Package)
{
    auto const TcpConn = Connection->Cast<UTcpipConnection>();
    MILAN_CHECKW(TcpConn != nullptr, L"");

    MILAN_TRACEW(ELOG_NetHooks, L"UWorld::NotifyControlMessage (Uses): Remote = %s, Package = %s, Guid = %s (%s), Gen = %d",
        *TcpConn->FormatRemoteAddress(), *Package->PackageName.ToString(), *FormatGuid(Package->Guid), *LookupPackageName(TcpConn, &Package->Guid), Package->RemoteGeneration);

    auto const Resolved = LookupPackageName(TcpConn, &Package->Guid);
    NETPROF(CaptureControlMessage(Connection, false, NM_Uses, FString::Printf(L"Package=%s, Guid=%s, Gen=%d, Resolved=%s",
            *Package->PackageName.ToString(), *FormatGuid(Package->Guid), Package->RemoteGeneration, *Resolved)));

    MILAN_UNUSED_4(TcpConn, Resolved, Connection, Package);
}

void MlnUWorldNotifyControlMessageHave_Handler(UNetConnection* const Connection, FGuid* const Guid, INT const Generation)
{
    auto const TcpConn = Connection->Cast<UTcpipConnection>();
    MILAN_CHECKW(TcpConn != nullptr, L"");

    MILAN_TRACEW(ELOG_NetHooks, L"UWorld::NotifyControlMessage (Have): Remote = %s, Guid = %s (%s), Gen = %d",
        *TcpConn->FormatRemoteAddress(), *FormatGuid(*Guid), *LookupPackageName(TcpConn, Guid), Generation);

    NETPROF(CaptureControlMessage(Connection, false, NM_Have, FString::Printf(L"Guid=%s, Gen=%d, Resolved=%s",
        *FormatGuid(*Guid), Generation, *LookupPackageName(TcpConn, Guid))));

    MILAN_UNUSED_4(TcpConn, Connection, Guid, Generation);
}

void MlnUWorldNotifyControlMessageJoin_Handler(UNetConnection* const Connection, QWORD* const UniqueId, FString* const SpawnError)
{
    auto const TcpConn = Connection->Cast<UTcpipConnection>();
    MILAN_CHECKW(TcpConn != nullptr, L"");


    NETPROF(CaptureControlMessage(Connection, false, NM_Join, FString::Printf(L"UniqueId=%llu, SpawnError=%s", *UniqueId, **SpawnError)));

    if (SpawnError->Empty())
    {
        FString const ActorName = TcpConn->Actor ? TcpConn->Actor->GetName() : L"";
        NETPROF(CaptureSessionEvent(Connection, L"JOIN", FString::Printf(L"UniqueId=%llu, Actor=%s", *UniqueId, *ActorName)));
        MILAN_TRACEW(ELOG_NetHooks, L"UWorld::NotifyControlMessage (Join): Remote = %s, UniqueId = %llu, Actor = %s", *TcpConn->FormatRemoteAddress(), *UniqueId, *ActorName);
    }
    else
    {
        NETPROF(CaptureSessionEvent(Connection, L"JOINFAIL", *SpawnError));
        MILAN_WARNW(ELOG_NetHooks, L"UWorld::NotifyControlMessage (Join): Remote = %s, UniqueId = %llu, Error = %s", *TcpConn->FormatRemoteAddress(), *UniqueId, SpawnError->Chars());
    }

    MILAN_UNUSED_4(Connection, TcpConn, UniqueId, SpawnError);
}

void MlnUWorldNotifyControlMessageAbort_Handler(UNetConnection* const Connection, FGuid* const Guid)
{
    auto const TcpConn = Connection->Cast<UTcpipConnection>();
    MILAN_CHECKW(TcpConn != nullptr, L"");

    MILAN_WARNW(ELOG_NetHooks, L"UWorld::NotifyControlMessage (Abort): Remote = %s, Guid = %s (%s)",
        *TcpConn->FormatRemoteAddress(), *FormatGuid(*Guid), *LookupPackageName(TcpConn, Guid));

    NETPROF(CaptureControlMessage(Connection, false, NM_Abort, FString::Printf(L"Guid=%s, Resolved=%s",
        *FormatGuid(*Guid), *LookupPackageName(TcpConn, Guid))));

    MILAN_UNUSED_3(TcpConn, Connection, Guid);
}

void MlnUWorldNotifyControlMessageUnload_Handler(UNetConnection* const Connection, FGuid* const Guid)
{
    auto const TcpConn = Connection->Cast<UTcpipConnection>();
    MILAN_CHECKW(TcpConn != nullptr, L"");

    MILAN_TRACEW(ELOG_NetHooks, L"UWorld::NotifyControlMessage (Unload): Remote = %s, Guid = %s (%s)",
        *TcpConn->FormatRemoteAddress(), *FormatGuid(*Guid), *LookupPackageName(TcpConn, Guid));

    NETPROF(CaptureControlMessage(Connection, false, NM_Unload, FString::Printf(L"Guid=%s, Resolved=%s",
        *FormatGuid(*Guid), *LookupPackageName(TcpConn, Guid))));

    MILAN_UNUSED_3(TcpConn, Connection, Guid);
}

void MlnUWorldNotifyControlMessageUnknown19_Handler(UNetConnection* const Connection, INT const UnknownCrc, FInBunch* const Bunch)
{
    auto const TcpConn = Connection->Cast<UTcpipConnection>();
    MILAN_CHECKW(TcpConn != nullptr, L"");

    MILAN_TRACEW(ELOG_NetHooks, L"UWorld::NotifyControlMessage (Unk19_DLM): Remote = %s, DLI CRC32 = 0x%08x", *TcpConn->FormatRemoteAddress(), UnknownCrc);

    NETPROF(CaptureControlMessage(Connection, false, NM_Unk19_DLM, FString::Printf(L"CRC32=0x%08x", UnknownCrc)));
    MILAN_UNUSED_4(Connection, TcpConn, UnknownCrc, Bunch);
}

void MlnUWorldNotifyControlMessageUnknown21_Handler(UNetConnection* const Connection, FGuid* const Guid, INT const Flag, FInBunch* const Bunch)
{
    auto const TcpConn = Connection->Cast<UTcpipConnection>();
    MILAN_CHECKW(TcpConn != nullptr, L"");

    MILAN_TRACEW(ELOG_NetHooks, L"UWorld::NotifyControlMessage (Unk21_VIS): Remote = %s, Guid = %s (%s), Flag = %d",
        *TcpConn->FormatRemoteAddress(), *FormatGuid(*Guid), *LookupPackageName(TcpConn, Guid), Flag);

    NETPROF(CaptureControlMessage(Connection, false, NM_Unk21_VIS, FString::Printf(L"Flag=%d, Guid=%s, Resolved=%s",
        Flag, *FormatGuid(*Guid), *LookupPackageName(TcpConn, Guid))));

    MILAN_UNUSED_5(Connection, TcpConn, Guid, Flag, Bunch);
}

void MlnUNetPendingLevelNotifyControlMessageWelcome_Handler(UNetConnection* const Connection, FString* const Map, FString* const GameName)
{
    auto const TcpConn = Connection->Cast<UTcpipConnection>();
    MILAN_CHECKW(TcpConn != nullptr, L"");

    MILAN_TRACEW(ELOG_NetHooks, L"UNetPendingLevel::NotifyControlMessage (Welcome): Remote = %s, Map = %s, GameName = %s",
        *TcpConn->FormatRemoteAddress(), Map->Chars(), GameName->Chars());
    NETPROF(CaptureControlMessage(Connection, true, NM_Welcome, FString::Printf(L"Map=%s, GameName=%s", **Map, **GameName)));
    MILAN_UNUSED_4(Connection, TcpConn, Map, GameName);
}

void MlnUNetPendingLevelNotifyControlMessageUpgrade_Handler(UNetConnection* const Connection, INT const StaticVersion, INT const DynamicVersion)
{
    auto const TcpConn = Connection->Cast<UTcpipConnection>();
    MILAN_CHECKW(TcpConn != nullptr, L"");

    MILAN_ERRORW(ELOG_NetHooks, L"UNetPendingLevel::NotifyControlMessage (Upgrade): Remote = %s, StaticVersion = 0x%08X, DynamicVersion = 0x%08X",
        *TcpConn->FormatRemoteAddress(), StaticVersion, DynamicVersion);

    NETPROF(CaptureControlMessage(Connection, true, NM_Upgrade, FString::Printf(L"StaticVersion=0x%08X, DynamicVersion=0x%08X",
        StaticVersion, DynamicVersion)));

    MILAN_UNUSED_4(Connection, TcpConn, StaticVersion, DynamicVersion);
}

void MlnUNetPendingLevelNotifyControlMessageChallenge_Handler(UNetConnection* const Connection, INT const Version, FString* const Challenge)
{
    auto const TcpConn = Connection->Cast<UTcpipConnection>();
    MILAN_CHECKW(TcpConn != nullptr, L"");

    MILAN_TRACEW(ELOG_NetHooks, L"UNetPendingLevel::NotifyControlMessage (Challenge): Remote = %s, Version = %d, Challenge = %s",
        *TcpConn->FormatRemoteAddress(), Version, Challenge->Chars());
    NETPROF(CaptureControlMessage(Connection, true, NM_Challenge, FString::Printf(L"Version=%d, Challenge=%s", Version, **Challenge)));
    MILAN_UNUSED_4(Connection, TcpConn, Version, Challenge);
}

void MlnUNetPendingLevelNotifyControlMessageFailure_Handler(UNetConnection* const Connection, FString* const Error)
{
    auto const TcpConn = Connection->Cast<UTcpipConnection>();
    MILAN_CHECKW(TcpConn != nullptr, L"");

    MILAN_TRACEW(ELOG_NetHooks, L"UNetPendingLevel::NotifyControlMessage (Failure): Remote = %s, Error = %s", *TcpConn->FormatRemoteAddress(), Error->Chars());
    NETPROF(CaptureControlMessage(Connection, true, NM_Failure, *Error));
    MILAN_UNUSED_3(Connection, TcpConn, Error);
}

void MlnUNetPendingLevelNotifyControlMessageUses_Handler(UNetConnection* const Connection, FPackageInfo* const Package)
{
    auto const TcpConn = Connection->Cast<UTcpipConnection>();
    MILAN_CHECKW(TcpConn != nullptr, L"");

    auto const  Resolved = LookupPackageName(TcpConn, &Package->Guid);
    FGuid       FoundGuid{};

    // Me: can we get a proper package lookup from guidcaches?
    // Mom: we have package lookup from guidcaches at home!
    // At home:
    bool bFoundPackage = false;
    bool bMatchedPackage = false;
    for (TypedObjectIterator<UGuidCache, true, false> Iter{}; Iter; ++Iter)
    {
        // Let's assume our context netpendinglevel caches all the guidcaches.

#if defined(ENABLE_GUIDCACHE_OVERRIDE) && ENABLE_GUIDCACHE_OVERRIDE
        bFoundPackage = !!UGuidCache_LookupPackageGuid_hook(*Iter, Package->PackageName, &FoundGuid);
        if (bFoundPackage)
        {
            bMatchedPackage = FoundGuid == Package->Guid;
            break;
        }
#else
        auto* const Guid = (*Iter)->PackageGuids.Find(SFXName(Package->PackageName));

        if (Guid != nullptr)
        {
            bFoundPackage = true;
            bMatchedPackage = *Guid == Package->Guid;

            FoundGuid = *Guid;
            break;
        }
#endif
    }

    if (bMatchedPackage)
    {
        MILAN_TRACEW(ELOG_NetHooks, L"UNetPendingLevel::NotifyControlMessage (Uses): Remote = %s, Package = %s, Guid = %s (%s), Gen = %d",
            *TcpConn->FormatRemoteAddress(), *Package->PackageName.ToString(), *FormatGuid(Package->Guid),
            *Resolved, Package->RemoteGeneration);
    }
    else
    {
        wchar_t const* const ErrorReason = bFoundPackage ? L"mismatched guid" : L"missing package";

        MILAN_WARNW(ELOG_NetHooks, L"UNetPendingLevel::NotifyControlMessage (Uses): Remote = %s, Package = %s, Guid = %s (%s), Gen = %d, Failure = %s",
            *TcpConn->FormatRemoteAddress(), *Package->PackageName.ToString(), *FormatGuid(Package->Guid),
            *Resolved, Package->RemoteGeneration, ErrorReason);

        if (bFoundPackage)
        {
            MILAN_WARNW(ELOG_NetHooks, L"  expected guid = %s, found guid = %s", *FormatGuid(Package->Guid), *FormatGuid(FoundGuid));
        }
    }

    NETPROF(CaptureControlMessage(Connection, true, NM_Uses, FString::Printf(L"Package=%s, Guid=%s, Gen=%d, Resolved=%s",
        *Package->PackageName.ToString(), *FormatGuid(Package->Guid), Package->RemoteGeneration, *Resolved)));
    MILAN_UNUSED_4(TcpConn, Resolved, Connection, Package);
}

void MlnUNetPendingLevelNotifyControlMessageDlMngr_Handler(FString* const Class, FString* const Params, INT* const bUseCompression)
{
    MILAN_TRACEW(ELOG_NetHooks, L"UNetPendingLevel::NotifyControlMessage (DlMngr): Remote = n/a, Class = %s, Params = %s, bUseCompression = %d",
        Class->Chars(), Params->Chars(), *bUseCompression);

    // NO CONNECTION POINTER!
    NETPROF(CaptureControlMessage(nullptr, true, NM_DlMngr, FString::Printf(L"Class=%s, Params=%s, Compression=%d",
        **Class, **Params, *bUseCompression)));

    MILAN_UNUSED_3(Class, Params, bUseCompression);
}

void MlnUNetPendingLevelNotifyControlMessageUnload_Handler(UNetConnection* const Connection, FGuid* const Guid)
{
    auto const TcpConn = Connection->Cast<UTcpipConnection>();
    MILAN_CHECKW(TcpConn != nullptr, L"");

    MILAN_TRACEW(ELOG_NetHooks, L"UNetPendingLevel::NotifyControlMessage (Unload): Remote = %s, Guid = %s (%s)",
        *TcpConn->FormatRemoteAddress(), *FormatGuid(*Guid), *LookupPackageName(TcpConn, Guid));

    NETPROF(CaptureControlMessage(Connection, true, NM_Unload, FString::Printf(L"Guid=%s, Resolved=%s",
        *FormatGuid(*Guid), *LookupPackageName(TcpConn, Guid))));

    MILAN_BREAK_ATTACHED();  // This injection hasn't been tested yet.
    MILAN_UNUSED_3(Connection, TcpConn, Guid);
}
