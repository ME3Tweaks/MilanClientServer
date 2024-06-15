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
MILAN_DECLARE_EXTRACTOR(MlnUChannelSendBunch2, UChannel* Channel, FOutBunch* Bunch);
MILAN_DECLARE_EXTRACTOR(MlnUActorChannelReplicateActor1, UActorChannel* Channel, AActor* Actor);
MILAN_DECLARE_EXTRACTOR(MlnUActorChannelReplicateActor2, FOutBunch* Bunch);
MILAN_DECLARE_EXTRACTOR(MlnUActorChannelReplicateActor3, UActorChannel* Channel, UProperty* Property, FOutBunch* Bunch);
MILAN_DECLARE_EXTRACTOR(MlnInternalReplicateFunction1, UActorChannel* Channel, FOutBunch* Bunch, UFunction* Function);
MILAN_DECLARE_EXTRACTOR(MlnSeamlessTravelTick1, FURL* PendingUrl);
MILAN_DECLARE_EXTRACTOR(MlnUTcpNetDriverTickDispatch, UTcpipConnection* Connection);

MILAN_DECLARE_EXTRACTOR(MlnUWorldNotifyControlMessageHello, UNetConnection* Connection, BYTE Platform, INT RemoteMinVersion, INT RemoteVersion);
MILAN_DECLARE_EXTRACTOR(MlnUWorldNotifyControlMessageNetspeed, UNetConnection* Connection, INT Speed, INT ClampedSpeed);
MILAN_DECLARE_EXTRACTOR(MlnUWorldNotifyControlMessageLogin, UNetConnection* Connection, FString* LoginError);
MILAN_DECLARE_EXTRACTOR(MlnUWorldNotifyControlMessageFailure, UNetConnection* Connection, FString* Error);
MILAN_DECLARE_EXTRACTOR(MlnUWorldNotifyControlMessageUses, UNetConnection* Connection, FPackageInfo* Package);
MILAN_DECLARE_EXTRACTOR(MlnUWorldNotifyControlMessageHave, UNetConnection* Connection, FGuid* Guid, INT Generation);
MILAN_DECLARE_EXTRACTOR(MlnUWorldNotifyControlMessageJoin, UNetConnection* Connection, QWORD* UniqueId, FString* SpawnError);
MILAN_DECLARE_EXTRACTOR(MlnUWorldNotifyControlMessageAbort, UNetConnection* Connection, FGuid* Guid);
MILAN_DECLARE_EXTRACTOR(MlnUWorldNotifyControlMessageUnload, UNetConnection* Connection, FGuid* Guid);
MILAN_DECLARE_EXTRACTOR(MlnUWorldNotifyControlMessageUnknown19, UNetConnection* Connection, INT UnknownCrc, FInBunch* Bunch);
MILAN_DECLARE_EXTRACTOR(MlnUWorldNotifyControlMessageUnknown21, UNetConnection* Connection, FGuid* Guid, INT Flag, FInBunch* Bunch);

MILAN_DECLARE_EXTRACTOR(MlnUNetPendingLevelNotifyControlMessageWelcome, UNetConnection* Connection, FString* Map, FString* GameName);
MILAN_DECLARE_EXTRACTOR(MlnUNetPendingLevelNotifyControlMessageUpgrade, UNetConnection* Connection, INT StaticVersion, INT DynamicVersion);
MILAN_DECLARE_EXTRACTOR(MlnUNetPendingLevelNotifyControlMessageChallenge, UNetConnection* Connection, INT Version, FString* Challenge);
MILAN_DECLARE_EXTRACTOR(MlnUNetPendingLevelNotifyControlMessageFailure, UNetConnection* Connection, FString* Error);
MILAN_DECLARE_EXTRACTOR(MlnUNetPendingLevelNotifyControlMessageUses, UNetConnection* Connection, FPackageInfo* Package);
MILAN_DECLARE_EXTRACTOR(MlnUNetPendingLevelNotifyControlMessageDlMngr, /* no connection pointer! */ FString* Class, FString* Params, INT* bUseCompression);
MILAN_DECLARE_EXTRACTOR(MlnUNetPendingLevelNotifyControlMessageUnload, UNetConnection* Connection, FGuid* Guid);


#undef MILAN_DECLARE_EXTRACTOR
