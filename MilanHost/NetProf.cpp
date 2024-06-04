#include <intrin.h>

#include "Base.hpp"
#include "NetProf.hpp"
#include "Proxies/Engine.hpp"
#include "Systems/Output.hpp"


void TraceUrl(wchar_t const* const InLabel, FURL* const InUrl)
{
    MILAN_UNUSED_2(InLabel, InUrl);
    MILAN_TRACEW(L"NETPROF (%s): proto=%s, host=%s, map=%s, portal=%s, port=%d, valid=%d",
        InLabel, *InUrl->Protocol, *InUrl->Host, *InUrl->Map, *InUrl->Portal, InUrl->Port, InUrl->Valid);

    FString OpLine{};
    for (auto const& Op : InUrl->Op)
    {
        OpLine.Append(L'?');
        OpLine.Append(Op);
    }

    MILAN_TRACEW(L"  > ops = %s", *OpLine);
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

    INT const BitsWrittenAfter = Bunch->Num;
    MILAN_TRACEW(L"NETPROF (UChannel::SendBunch): %s, bits = %d",
        Bunch->Channel ? *Bunch->Channel->StaticName() : L"???",
        BitsWrittenAfter - SendBunch1_BitsWrittenBefore);

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

    auto* const ActorChan = reinterpret_cast<UActorChannel_Proxy*>(Channel);
    MILAN_TRACEW(L"NETPROF (InternalReplicateFunction): bits = %d", Bunch ? Bunch->Num : -1);
    MILAN_TRACEW(L"  Actor:     %s", (ActorChan && ActorChan->Actor) ? *ActorChan->Actor->StaticName() : L"???");
    MILAN_TRACEW(L"  Function:  %s", Function ? *Function->StaticName() : L"???");
}

void MlnSeamlessTravelTick1_Handler(FURL* const PendingUrl)
{
    FURL* const WorldUrl = &reinterpret_cast<UWorld_Proxy*>(*GWorld)->URL;
    TraceUrl(L"SeamlessTravelTick / world", WorldUrl);
    TraceUrl(L"SeamlessTravelTick / pending", PendingUrl);
}

void MlnUGameEngineTick1_Handler()
{
    //MILAN_TRACEW(L"NETPROF (UGameEngine::Tick): %d", 1);
}

void MlnUGameEngineLoadMap1_Handler(FURL* const Url)
{
    TraceUrl(L"UGameEngine::LoadMap", Url);
}
