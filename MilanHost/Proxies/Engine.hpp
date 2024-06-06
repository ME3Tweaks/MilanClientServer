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

enum ENetMode
{
    NM_Standalone                   = 0,
    NM_DedicatedServer              = 1,
    NM_ListenServer                 = 2,
    NM_Client                       = 3,
    NM_Max                          = 4
};

enum ENetRole
{
    ROLE_None                       = 0,
    ROLE_SimulatedProxy             = 1,
    ROLE_AutonomousProxy            = 2,
    ROLE_Authority                  = 3,
    ROLE_Max                        = 4
};

enum ESocketType
{
    SOCKTYPE_Unknown                = 0,
    SOCKTYPE_Datagram               = 1,
    SOCKTYPE_Streaming              = 2,
};


class FSocket
{
    void*                           Vtable;                     // 0x00

public:

    ESocketType                     Type;                       // 0x08
    FString                         Description;                // 0x0C

    // ...                          ...                         // ...
};


class UChannel_Proxy : public UObject
{
public:

    UNetConnection*                 Connection;                 // 0x60
    long                            bOpenAcked : 1;             // 0x68 (& 0x01)
    long                            bClosing : 1;               // 0x68 (& 0x02)
    long                            bOpenTemporary : 1;         // 0x68 (& 0x04)
    long                            bBroken : 1;                // 0x68 (& 0x08)
    long                            bTornOff : 1;               // 0x68 (& 0x10)
    int                             ChannelIndex;               // 0x6C
    int                             OpenedLocally;              // 0x70
    int                             OpenPacketId;               // 0x74
    int                             ChannelType;                // 0x78
    int                             NumInRec;                   // 0x7C
    int                             NumOutRec;                  // 0x80
    int                             NegotiatedVer;              // 0x84

    unsigned char                   UnknownData_88[0x10];       // 0x88
};

static_assert(offsetof(UChannel_Proxy, ChannelIndex) == 0x6C);
//static_assert(offsetof(UChannel_Proxy, Unknown_84) == 0x84);
static_assert(sizeof(UChannel_Proxy) == sizeof(UChannel));


struct FReplicatedProperty
{
    int                             Offset;                     // 0x00
    UProperty*                      Property;                   // 0x04
};

static_assert(offsetof(FReplicatedProperty, Property) == 4);
static_assert(sizeof(FReplicatedProperty) == 12);


class UActorChannel_Proxy : public UChannel_Proxy
{
public:

    UWorld*                         World;                      // 0x98
    AActor*                         Actor;                      // 0xA0
    UClass*                         ActorClass;                 // 0xA8
    double                          RelevantTime;               // 0xB0
    double                          LastUpdateTime;             // 0xB8
    long                            SpawnAcked : 1;             // 0xC0 (& 0x01)
    long                            ActorDirty : 1;             // 0xC0 (& 0x02)
    long                            bActorMustStayDirty : 1;    // 0xC0 (& 0x04)
    long                            bActorStillInitial : 1;     // 0xC0 (& 0x08)
    long                            bIsReplicatingActor : 1;    // 0xC0 (& 0x10)
    long                            bClearRecentActorRefs : 1;  // 0xC0 (& 0x20)
    TArray<BYTE>                    Recent;                     // 0xC4
    TArray<BYTE>                    RepEval;                    // 0xD4
    TArray<int>                     Dirty;                      // 0xE4
    TArray<void*>                   Retirement;                 // 0xF4
    TArray<FReplicatedProperty>     ReplicatedProperties;       // 0x104

    TArray<void*>                   Unknown_114;                // 0x114 (elements size = 0x78)
};

static_assert(offsetof(UActorChannel_Proxy, World) == 0x98);
static_assert(offsetof(UActorChannel_Proxy, Actor) == 0xA0);
static_assert(offsetof(UActorChannel_Proxy, ActorClass) == 0xA8);
static_assert(offsetof(UActorChannel_Proxy, RelevantTime) == 0xB0);
static_assert(offsetof(UActorChannel_Proxy, LastUpdateTime) == 0xB8);
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


class ULevelBase_Proxy : public UObject
{
public:

    TArray<AActor*>                 Actors;
    UObject*                        MaybeOwner;
    FURL                            URL;
};

static_assert(offsetof(ULevelBase_Proxy, Actors) == 0x60);
static_assert(sizeof(ULevelBase_Proxy) == sizeof(ULevelBase));


class ULevel_Proxy : public ULevelBase_Proxy
{
public:

    unsigned char                   UnknownData_D0[0xB8];       // 0xD0
    int                             FirstNetRelevantActor;      // 0x188
    int                             FirstDynamicActor;          // 0x18C
    float                           LightmapTotalSize;          // 0x190
    float                           ShadowmapTotalSize;         // 0x194
    unsigned char                   UnknownData_198[0x20C];     // 0x198
};

static_assert(offsetof(ULevel_Proxy, UnknownData_D0) == 0xD0);
static_assert(offsetof(ULevel_Proxy, FirstDynamicActor) == 0x18C);
static_assert(offsetof(ULevel_Proxy, UnknownData_198) == 0x198);
static_assert(sizeof(ULevel_Proxy) == sizeof(ULevel));


class UWorld_Proxy : public UObject
{

    // Note: 01/09/2024 Mgamerz
    // Offsets for objects here were calculated using EmitObjectReference in static constructor hooks.
    // Some objects were not loaded so they could not have their type determined.

public:

    unsigned char                   Unknown_60[0x18];           // 0x60
    TArray<ULevel*>                 Levels;                     // 0x78
    ULevel*                         PersistentLevel;            // 0x88
    UObject*                        Unknown_90;                 // 0x90
    ULevel*                         CurrentLevel;               // 0x98
    UObject*                        Unknown_A0;                 // 0xA0
    unsigned char                   Unknown_A8[0x70];           // 0xA8
    UObject*                        Unknown_118;                // 0x118
    UNetDriver*                     NetDriver;                  // 0x120
    FURL                            URL;                        // 0x128
    UObject*                        Unknown_180;                // 0x180
    unsigned char                   Unknown_188[0x28];          // 0x188
    TArray<UObject*>                Unknown_1B0;                // 0x1B0
    unsigned char                   Unknown_1C0[0x18];          // 0x1C0
    ULineBatchComponent*            LineBatcher;                // 0x1D8
    ULineBatchComponent*            PersistentLineBatcher;      // 0x1E0
    unsigned char                   Unknown_1E8[0x1C];          // 0x1E8
    TArray<UObject*>                Unknown_204;                // 0x204
    TArray<UObject*>                Unknown_214;                // 0x214
    unsigned char                   Unknown_224[0x13C];         // 0x224
    TArray<UObject*>                Unknown_360;                // 0x360
    unsigned char                   Unknown_370[0x28];          // 0x370

    // Other offsets noted in Ghidra:
    //  - 0x2D8 => DominantDirectionalLight (UDominantDirectionalLightComponent*)

};

static_assert(offsetof(UWorld_Proxy, Levels) == 0x78);
static_assert(offsetof(UWorld_Proxy, PersistentLevel) == 0x88);
static_assert(offsetof(UWorld_Proxy, CurrentLevel) == 0x98);
static_assert(offsetof(UWorld_Proxy, Unknown_A0) == 0xA0);
static_assert(offsetof(UWorld_Proxy, Unknown_118) == 0x118);
static_assert(offsetof(UWorld_Proxy, NetDriver) == 0x120);
static_assert(offsetof(UWorld_Proxy, Unknown_180) == 0x180);
static_assert(offsetof(UWorld_Proxy, Unknown_1B0) == 0x1B0);
static_assert(offsetof(UWorld_Proxy, LineBatcher) == 0x1D8);
static_assert(offsetof(UWorld_Proxy, PersistentLineBatcher) == 0x1E0);
static_assert(offsetof(UWorld_Proxy, Unknown_204) == 0x204);
static_assert(offsetof(UWorld_Proxy, Unknown_214) == 0x214);
static_assert(offsetof(UWorld_Proxy, Unknown_360) == 0x360);
static_assert(sizeof(UWorld_Proxy) == sizeof(UWorld));


class UNetDriver_Proxy;
class UChildConnection_Proxy;

class UNetConnection_Proxy : public UPlayer
{
public:

    UNetDriver*                     Driver;                     // 0x8C
    EConnectionState                ConnectionState;            // 0x94
    FURL                            URL;                        // 0x98
    UPackageMap*                    PackageMap;                 // 0xF0
    int                             Unknown_F8[3];              // 0xF8
    int                             MaxPacket;                  // 0x104
    int                             PacketOverhead;             // 0x108
    int                             Unknown_10C;                // 0x10C
    FString                         Challenge;                  // 0x110
    FString                         ClientResponse;             // 0x120
    int                             ResponseId;                 // 0x130
    int                             NegotiatedVer;              // 0x134
    unsigned char                   Unknown_138[0x2c4 - 0x138]; // 0x138
    int                             InBytes;                    // 0x2C4
    int                             OutBytes;                   // 0x2C8
    int                             InPacketsLost;              // 0x2CC
    int                             OutPacketsLost;             // 0x2D0
    FBitWriter                      Out;                        // 0x2D4
    double                          OutLagTime[256];            //   checks out: 0x378
    int                             OutLagPacketId[256];        //   checks out: 0xB78
    int                             InPacketId;                 // 0xF78
    int                             OutPacketId;                // 0xF7C
    int                             OutAckPacketId;             // 0xF80
    UChannel*                       Channels[1023];             // 0xF84
    int                             OutReliable[1023];          //   checks out: 0x2F7C
    int                             InReliable[1023];           //   checks out: 0x3F78
    int                             PendingOutRec[1023];        //   checks out: 0x4F74
    TArray<int>                     QueuedAcknowledgements;     //   checks out: 0x5F70
    TArray<int>                     ResendAcknowledgements;     //   checks out: 0x5F80
    TArray<UChannel*>               OpenChannels;               // 0x5F90
    TArray<AActor*>                 SendTemporaries;            //   checks out: 0x5FA0
    TMap<AActor*, UActorChannel*>   ActorChannels;              //   checks out: 0x5FB0
    UDownload*                      Download;                   // 0x5FF8
    TArray<void*>                   DownloadInfo;               // 0x6000
    TArray<FPackageInfo*>           PendingPackageInfos;        //   checks out: 0x6010
    SFXName                         ClientWorldPackageName;     //   checks out: 0x6020
    TArray<SFXName>                 ClientVisibleLevelNames;    //   checks out: 0x6028
    TArray<FGuid>                   PendingRemovePackageGuids;  //   checks out: 0x6038
    TArray<UChildConnection*>       Children;                   // 0x6048
    AActor*                         Viewer;                     // 0x6058
    unsigned char                   Unknown_6060[0x0C];         // 0x6060
};

static_assert(offsetof(UNetConnection_Proxy, PackageMap) == 0xF0);
static_assert(offsetof(UNetConnection_Proxy, MaxPacket) == 0x104);
static_assert(offsetof(UNetConnection_Proxy, PacketOverhead) == 0x108);
static_assert(offsetof(UNetConnection_Proxy, NegotiatedVer) == 0x134);
static_assert(offsetof(UNetConnection_Proxy, InBytes) == 0x2C4);
static_assert(offsetof(UNetConnection_Proxy, InPacketsLost) == 0x2CC);
static_assert(offsetof(UNetConnection_Proxy, Out) == 0x2D4);
static_assert(offsetof(UNetConnection_Proxy, InPacketId) == 0xF78);
static_assert(offsetof(UNetConnection_Proxy, Channels) == 0xF84);
static_assert(offsetof(UNetConnection_Proxy, OpenChannels) == 0x5f90);
static_assert(offsetof(UNetConnection_Proxy, Download) == 0x5ff8);
static_assert(offsetof(UNetConnection_Proxy, DownloadInfo) == 0x6000);
static_assert(offsetof(UNetConnection_Proxy, Children) == 0x6048);
static_assert(offsetof(UNetConnection_Proxy, Unknown_6060) == 0x6060);
static_assert(sizeof(UNetConnection_Proxy) == sizeof(UNetConnection));


class UTcpipConnection_Proxy : public UNetConnection_Proxy
{
public:

    SOCKADDR_IN                     Address;                    // 0x606C
    unsigned char                   Unknown_607C[24];           // 0x607C
};

static_assert(offsetof(UTcpipConnection_Proxy, Address) == 0x606C);
static_assert(offsetof(UTcpipConnection_Proxy, Unknown_607C) == 0x607C);
static_assert(sizeof(UTcpipConnection_Proxy) == sizeof(UTcpipConnection));


class UChildConnection_Proxy : public UNetConnection_Proxy
{
public:

    UNetConnection*                 Parent;                     // 0x606C
};

static_assert(offsetof(UChildConnection_Proxy, Parent) == 0x606C);
static_assert(sizeof(UChildConnection_Proxy) == sizeof(UChildConnection));


class UNetDriver_Proxy : public USubsystem
{
public:

    // Ghidra Decomp 01/16/2024.

    void*                           FObjectNotify_VTable;       // 0x68
    TArray<UNetConnection*>         ChildConnections;           // 0x70
    UNetConnection*                 ServerConnection;           // 0x80
    void*                           Notify;                     // 0x88   // we have no C++ class for this
    UPackageMap*                    MasterMap;                  // 0x90
    float                           Time;                       // 0x98

    // Generated members from SDK.

    float                           ConnectionTimeout;          // 0x9C
    float                           InitialConnectTimeout;      // 0xA0
    float                           KeepAliveTime;              // 0xA4
    float                           RelevantTimeout;            // 0xA8
    float                           SpawnPrioritySeconds;       // 0xAC
    float                           ServerTravelPause;          // 0xB0
    int                             MaxClientRate;              // 0xB4
    int                             MaxInternetClientRate;      // 0xB8
    int                             NetServerMaxTickRate;       // 0xBC
    unsigned long                   bClampListenServerTickRate; // 0xC0
    unsigned long                   AllowDownloads;             // 0xC4
    unsigned char                   Unknown_C8[0x1C];           // 0xC8
    int                             MaxDownloadSize;            // 0xE4
    TArray<FString>                 DownloadManagers;           // 0xE8
    unsigned char                   Unknown_F8[0xA4];           // 0xF8
    FString                         NetConnectionClassName;     // 0x19C
    unsigned char                   Unknown_1AC[0x30];          // 0x1AC
};

static_assert(offsetof(UNetDriver_Proxy, ChildConnections) == 0x70);
static_assert(offsetof(UNetDriver_Proxy, ConnectionTimeout) == 0x9C);
static_assert(offsetof(UNetDriver_Proxy, Unknown_1AC) == 0x1AC);
// This checks must add '4' to original size until sdk fixes not-bitfield bools.
static_assert(sizeof(UNetDriver_Proxy) == sizeof(UNetDriver) + 4);

#pragma pack(pop)
