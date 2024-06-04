/**
 * @file        MilanHost/NetProf.hpp
 * @header      Embedded logic for our Network Profiler.
 */

#pragma once

#include <LESDK/Headers.hpp>
#include "Proxies/Core.hpp"


// ! Declarations for assembly stubs.
// ========================================

extern "C" void MlnUActorChannelReplicateActor1();
extern "C" void MlnUActorChannelReplicateActor2();
extern "C" void MlnUActorChannelReplicateActor3();
extern "C" void MlnSeamlessTravelTick1();
extern "C" void MlnUGameEngineTick1();
extern "C" void MlnUGameEngineLoadMap1();


// ! Internal handlers called by asm code.
// ========================================

extern "C" void MlnUActorChannelReplicateActor1_Handler(AActor* Actor);
extern "C" void MlnUActorChannelReplicateActor2_Handler(FOutBunch* Bunch);
extern "C" void MlnUActorChannelReplicateActor3_Handler(UProperty* Property, FOutBunch* Bunch);
extern "C" void MlnSeamlessTravelTick1_Handler(FURL* PendingUrl);
extern "C" void MlnUGameEngineTick1_Handler();
extern "C" void MlnUGameEngineLoadMap1_Handler(FURL* Url);
