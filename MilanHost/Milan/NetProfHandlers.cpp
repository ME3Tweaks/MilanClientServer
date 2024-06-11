#include <intrin.h>

#include "Milan/Base.hpp"
#include "Milan/NetProfDecls.hpp"
#include "Proxies/Engine.hpp"
#include "Systems/Output.hpp"


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


// ! Internal handlers called by asm code.
// ========================================

// Used by UChannel::SendBunch handlers: stored by 1st, loaded and cleared by 2nd.
static INT SendBunch1_BitsWrittenBefore = 0;

void MlnUChannelSendBunch1_Handler(FOutBunch* const Bunch)
{
    SendBunch1_BitsWrittenBefore = Bunch->Num;
}

void MlnUChannelSendBunch2_Handler(FOutBunch* const Bunch)
{
    MILAN_UNUSED(Bunch);
    // Note that Bunch pointers here and in the 1st handler are not necessarily the same!

    //INT const BitsWrittenAfter = Bunch->Num;
    //MILAN_TRACEW(L"NETPROF (UChannel::SendBunch): %s, bits = %d",
    //    Bunch->Channel ? *Bunch->Channel->StaticName() : L"???",
    //    BitsWrittenAfter - SendBunch1_BitsWrittenBefore);

    SendBunch1_BitsWrittenBefore = 0;
}

// Used by UActorChannel::ReplicateActor handlers: stored by 2nd, loaded and cleared by 3rd.
static INT ReplicateActor_BitsWrittenBeforeProp = 0;

void MlnUActorChannelReplicateActor1_Handler(AActor* const Actor)
{
    MILAN_UNUSED(Actor);
    //MILAN_TRACEW(L"NETPROF (UActorChannel::ReplicateActor / actor): %s", *Actor->StaticName());
}

void MlnUActorChannelReplicateActor2_Handler(FOutBunch* const Bunch)
{
    ReplicateActor_BitsWrittenBeforeProp = Bunch->Num;
}

void MlnUActorChannelReplicateActor3_Handler(UProperty* const Property, FOutBunch* const Bunch)
{
    MILAN_UNUSED_2(Property, Bunch);
    //INT const BitsWrittenAfterProp = Bunch->Num;
    //MILAN_TRACEW(L"NETPROF (UActorChannel::ReplicateActor / property): %s, bits = %d",
    //    *Property->StaticName(), BitsWrittenAfterProp - ReplicateActor_BitsWrittenBeforeProp);
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
    TraceUrl(L"SeamlessTravelTick / world", WorldUrl);
    TraceUrl(L"SeamlessTravelTick / pending", PendingUrl);
}

void MlnUTcpNetDriverTickDispatch_Handler(UTcpipConnection* const Connection)
{
    auto const Address = reinterpret_cast<UTcpipConnection_Proxy*>(Connection)->Address;
    auto const Remote = FString::Printf(L"%S:%hu", inet_ntoa(Address.sin_addr), ntohs(Address.sin_port));
    MILAN_TRACEW(L"NETPROF (call to FNetworkNotify::NotifyAcceptedConnection): %s", *Remote);
    MILAN_UNUSED_2(Address, Remote);
}

void MlnUWorldNotifyControlMessageHello_Handler(BYTE const Platform, INT const RemoteMinVersion, INT const RemoteVersion, FInBunch* const Bunch)
{
    MILAN_WARNW(L"UWorld::NotifyControlMessage (Hello): Platform=%hhu, RemoteMinVersion=%#x, RemoteVersion=%#x", Platform, Byteswap(RemoteMinVersion), Byteswap(RemoteVersion));
    MILAN_UNUSED_4(Platform, RemoteMinVersion, RemoteVersion, Bunch);
}

void MlnUWorldNotifyControlMessageNetspeed_Handler(INT const Speed, INT const ClampedSpeed)
{
    MILAN_WARNW(L"UWorld::NotifyControlMessage (Netspeed): Speed=%d, Clamped=%d", Speed, ClampedSpeed);
    MILAN_UNUSED_2(Speed, ClampedSpeed);
}

void MlnUWorldNotifyControlMessageLogin_Handler(UNetConnection* const Connection, FString* const LoginError)
{
    auto const NetConnection = reinterpret_cast<UNetConnection_Proxy*>(Connection);
    MILAN_WARNW(L"UWorld::NotifyControlMessage (Login): ClientResponse=%s, RequestURL=%s, LoginError=%s",
        *NetConnection->ClientResponse, *NetConnection->RequestURL, LoginError->Chars());
    MILAN_UNUSED_2(NetConnection, LoginError);
}

void MlnUWorldNotifyControlMessageHave_Handler(FGuid* const Guid, INT const Generation)
{
    //MILAN_WARNW(L"UWorld::NotifyControlMessage (Have): Guid=%08X%08X%08X%08X, Generation=%d", Guid->A, Guid->B, Guid->C, Guid->D, Generation);
    MILAN_UNUSED_2(Guid, Generation);
}

void MlnUWorldNotifyControlMessageJoin_Handler(QWORD* const UniqueId, APlayerController* const Actor, FString* const SpawnError)
{
    MILAN_WARNW(L"UWorld::NotifyControlMessage (Join): UniqueId=%llu, Actor=%s, Error=%s",
        *UniqueId, Actor ? *Actor->StaticName() : L"(null)", SpawnError->Chars());
    MILAN_UNUSED_3(UniqueId, Actor, SpawnError);
}
