/**
 * @file        Proxies/Core.hpp
 * @brief       This file contains UClass proxy types for @c Engine package.
 * @remarks     Ported from MilanASI repo, originally via Ghidra decompilation.
 */

#pragma once
#pragma pack(push, 4)

#include <LESDK/Headers.hpp>
#include "Proxies/Core.hpp"


enum EConnectionState
{
    USOCK_Invalid                   = 0,
    USOCK_Closed                    = 1,
    USOCK_Pending                   = 2,
    USOCK_Open                      = 3
};


class UChannel_Proxy : public UObject
{
public:

    UNetConnection*                 Connection;                 // 0x60

    int                             bOpenAcked : 1;             // 0x68 (& 0x01)
    int                             bClosing : 1;               // 0x68 (& 0x02)
    int                             bOpenTemporary : 1;         // 0x68 (& 0x04)
    int                             bBroken : 1;                // 0x68 (& 0x08)
    int                             bTornOff : 1;               // 0x68 (& 0x10)

    int                             ChannelIndex;               // 0x6C
    int                             Unknown_70;                 // 0x70
    int                             OpenPacketId;               // 0x74
    int                             ChannelType;                // 0x78
    int                             NumInRec;                   // 0x7C
    int                             NumOutRec;                  // 0x80
    int                             Unknown_84;                 // 0x84
    unsigned char                   UnknownData_88[0x10];       // 0x88
};

static_assert(offsetof(UChannel_Proxy, ChannelIndex) == 0x6C);
static_assert(offsetof(UChannel_Proxy, Unknown_84) == 0x84);
static_assert(sizeof(UChannel_Proxy) == sizeof(UChannel));


class UActorChannel_Proxy : public UChannel_Proxy
{
public:

    UWorld*                         World;                      // 0x98
    AActor*                         Actor;                      // 0xA0
    UClass*                         ActorClass;                 // 0xA8
    BYTE                            Unknown_B0[8];              // 0xB0
    BYTE                            Unknown_B8[8];              // 0xB8

    long                            SpawnAcked : 1;
    long                            ActorDirty : 1;
    long                            bActorMustStayDirty : 1;
    long                            bActorStillInitial : 1;
    long                            bIsReplicatingActor : 1;    // 0xC0 (& 0x10)
    long                            bClearRecentActorRefs : 1;

    TArray<BYTE>                    Recent;                     // 0xC4
    TArray<BYTE>                    RepEval;                    // 0xD4
    TArray<int>                     Dirty;                      // 0xE4
    TArray<void*>                   Retirement;                 // 0xF4
    TArray<void*>                   ReplicatedProperties;       // 0x104
    TArray<void*>                   Unknown_114;                // 0x114 (elements size = 0x78)
};

static_assert(offsetof(UActorChannel_Proxy, World) == 0x98);
static_assert(offsetof(UActorChannel_Proxy, Actor) == 0xA0);
static_assert(offsetof(UActorChannel_Proxy, ActorClass) == 0xA8);
static_assert(offsetof(UActorChannel_Proxy, Unknown_B0) == 0xB0);
static_assert(offsetof(UActorChannel_Proxy, Unknown_B8) == 0xB8);
static_assert(offsetof(UActorChannel_Proxy, RepEval) == 0xD4);
static_assert(offsetof(UActorChannel_Proxy, Dirty) == 0xE4);
static_assert(offsetof(UActorChannel_Proxy, Retirement) == 0xF4);
static_assert(offsetof(UActorChannel_Proxy, ReplicatedProperties) == 0x104);
static_assert(offsetof(UActorChannel_Proxy, Unknown_114) == 0x114);
static_assert(sizeof(UActorChannel_Proxy) == sizeof(UActorChannel));


class UControlChannel_Proxy : public UChannel_Proxy
{
public:

    TArray<TArray<BYTE>>            QueuedMessages;
    BYTE                            Unknown_A8[4];
};

static_assert(offsetof(UControlChannel_Proxy, QueuedMessages) == 0x98);
static_assert(offsetof(UControlChannel_Proxy, Unknown_A8) == 0xA8);
static_assert(sizeof(UControlChannel_Proxy) == sizeof(UControlChannel));


#pragma pack(pop)
