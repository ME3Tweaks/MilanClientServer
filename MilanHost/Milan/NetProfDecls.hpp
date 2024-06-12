/**
 * @file        MilanHost/Milan/NetProfDecls.hpp
 * @header      Declarations for Network Profiler extractors and handlers.
 */

#pragma once

#include <LESDK/Headers.hpp>

#include "Milan/Base.hpp"
#include "Proxies/Core.hpp"
#include "Proxies/Engine.hpp"


/**
 * @def         MILAN_DECLARE_EXTRACTOR(name, ...)
 * @brief       Declares two extern functions: an extractor defined in assembly, and a handler defined in .cpp code.
 */
#define MILAN_DECLARE_EXTRACTOR(name, ...) \
    extern "C" void name(); \
    extern "C" void MILAN_VARCAT(name, _Handler)(__VA_ARGS__)


MILAN_DECLARE_EXTRACTOR(MlnUChannelSendBunch1, FOutBunch* Bunch);
MILAN_DECLARE_EXTRACTOR(MlnUChannelSendBunch2, FOutBunch* Bunch);
MILAN_DECLARE_EXTRACTOR(MlnUActorChannelReplicateActor1, AActor* Actor);
MILAN_DECLARE_EXTRACTOR(MlnUActorChannelReplicateActor2, FOutBunch* Bunch);
MILAN_DECLARE_EXTRACTOR(MlnUActorChannelReplicateActor3, UProperty* Property, FOutBunch* Bunch);
MILAN_DECLARE_EXTRACTOR(MlnInternalReplicateFunction1, UActorChannel* Channel, FOutBunch* Bunch, UFunction* Function);
MILAN_DECLARE_EXTRACTOR(MlnSeamlessTravelTick1, FURL* PendingUrl);
MILAN_DECLARE_EXTRACTOR(MlnUTcpNetDriverTickDispatch, UTcpipConnection* Connection);

MILAN_DECLARE_EXTRACTOR(MlnUWorldNotifyControlMessageHello, BYTE Platform, INT RemoteMinVersion, INT RemoteVersion, FInBunch* Bunch);
MILAN_DECLARE_EXTRACTOR(MlnUWorldNotifyControlMessageNetspeed, INT Speed, INT ClampedSpeed);
MILAN_DECLARE_EXTRACTOR(MlnUWorldNotifyControlMessageLogin, UNetConnection* Connection, FString* LoginError);
MILAN_DECLARE_EXTRACTOR(MlnUWorldNotifyControlMessageUses, UNetConnection* Connection, FPackageInfo* Package);
MILAN_DECLARE_EXTRACTOR(MlnUWorldNotifyControlMessageHave, FGuid* Guid, INT Generation);
MILAN_DECLARE_EXTRACTOR(MlnUWorldNotifyControlMessageJoin, QWORD* UniqueId, APlayerController* Actor, FString* SpawnError);
MILAN_DECLARE_EXTRACTOR(MlnUWorldNotifyControlMessageUnload, FGuid* Guid);
MILAN_DECLARE_EXTRACTOR(MlnUWorldNotifyControlMessageUnknown19, INT UnknownCrc, FInBunch* Bunch);
MILAN_DECLARE_EXTRACTOR(MlnUWorldNotifyControlMessageUnknown21, FGuid* Guid, INT Flag, FInBunch* Bunch);


#undef MILAN_DECLARE_EXTRACTOR
