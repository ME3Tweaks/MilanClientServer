/**
 * @file        Proxies/Core.hpp
 * @brief       This file contains UClass proxy types for @c Core package and some internals.
 * @remarks     Ported from MilanASI repo, originally via Ghidra decompilation.
 */

#pragma once
#pragma pack(push, 4)

#include <LESDK/Headers.hpp>


enum EChannelType
{
    CHTYPE_None                     = 0,  // Invalid
    CHTYPE_Control                  = 1,  // Control channel for client-server comms
    CHTYPE_Actor                    = 2,  // Channels for a specific actor
    CHTYPE_File                     = 3,  // Unlikely to be used - file transfer
    CHTYPE_Voice                    = 4,  // Voice traffic
    CHTYPE_MAX                      = 8,
};


class FArchive
{
    // Mgamerz:  Built from Ghidra decompilation, FArchive size appears to be 0x8C.
    // d00t:  Some offsets have not verified while porting.

public:

    void*                           _vftable;

    int                             ArVer;                              // 0x08
    int                             ArNetVer;                           // 0x0C
    int                             ArLicenseeVer;                      // 0x10
    BOOL                            ArIsLoading;                        // 0x14
    BOOL                            ArIsSaving;                         // 0x18
    BOOL                            ArIsTransacting;                    // 0x1C

    int                             Unknown_20[6];                      // 0x20

    BOOL                            ArIsError;                          // 0x38
    BOOL                            ArIsCriticalError;                  // 0x3C
    BOOL                            ArContainsCookedData;               // 0x40

    int                             Unknown_44[4];                      // 0x44

    BOOL                            ArIgnoreArchetypeRef;               // 0x54
    BOOL                            ArIgnoreOuterRef;                   // 0x58
    BOOL                            ArIgnoreClassRef;                   // 0x5C
    BOOL                            ArAllowEliminatingReferences;       // 0x60
    BOOL                            ArAllowLazyLoading;                 // 0x64
    BOOL                            ArIsObjectReferenceCollector;       // 0x68
    BOOL                            ArIsCountingMemory;                 // 0x6C

    int                             Unknown_70[4];

    // Note: 01/22/2024 Mgamerz
    // Decomp shows this might be 32 or 64 bit, unsure if a second property follows and it's 2 32-bits.
    // It would make sense to be a 32 bit as max file size is 2GiB in Legendary Edition.

    __int64                         ArMaxSerialSize;
    //int                           ArMaxSerialSize;

    BOOL                            ArBioWareUnknown;
};

static_assert(offsetof(FArchive, Unknown_20) == 0x20);
static_assert(offsetof(FArchive, ArContainsCookedData) == 0x40);
static_assert(offsetof(FArchive, Unknown_44) == 0x44);
static_assert(offsetof(FArchive, ArIgnoreArchetypeRef) == 0x54);
static_assert(offsetof(FArchive, Unknown_70) == 0x70);
static_assert(sizeof(FArchive) == 0x8C);


class FBitReader : public FArchive
{
public:

    TArray<BYTE>                    Buffer;                             // 0x8C
    INT                             NumBytes;                           // 0x9C
    INT                             BunchPos;                           // 0xA0
};

static_assert(offsetof(FBitReader, Buffer) == 0x8C);
static_assert(offsetof(FBitReader, NumBytes) == 0x9C);
static_assert(offsetof(FBitReader, BunchPos) == 0xA0);


class FBitWriter : public FArchive
{
public:

    TArray<BYTE>                    Buffer;                             // 0x8C
    INT                             Num;                                // 0x9C
    INT                             Max;                                // 0xA0
};

static_assert(offsetof(FBitWriter, Buffer) == 0x8C);
static_assert(offsetof(FBitWriter, Num) == 0x9C);
static_assert(offsetof(FBitWriter, Max) == 0xA0);


// Incoming bunch of information
class FInBunch : public FBitReader
{
public:

    int                             PacketId;
    FInBunch*                       Next;
    UNetConnection*                 Connection;
    int                             ChannelIndex;
    EChannelType                    ChannelType;
    int                             ChannelSequence;
    bool                            bOpen;
    bool                            bClose;
    bool                            bReliable;
};

static_assert(offsetof(FInBunch, PacketId) == 0xA4);


// Outgoing bunch of information
class FOutBunch : public FBitWriter
{
public:

    FOutBunch*                      Next;
    UChannel*                       Channel;
    double                          Time;
    BOOL                            ReceivedAck;
    int                             ChannelIndex;
    EChannelType                    ChannelType;
    int                             ChannelSequence;
    int                             PacketId;
    bool                            bOpen;
    bool                            bClose;
    bool                            bReliable;
};

static_assert(offsetof(FOutBunch, Next) == 0xA4);
static_assert(offsetof(FOutBunch, bReliable) == 0xD2);


class FPackageInfo
{
public:

    SFXName                         PackageName;
    UPackage*                       Parent;
    FGuid                           Guid;
    int                             ObjectBase;
    int                             ObjectCount;
    int                             LocalGeneration;
    int                             RemoteGeneration;
    int                             PackageFlags;
    SFXName                         ForcedExportBasePackageName;
    FString                         Extension;
};

static_assert(offsetof(FPackageInfo, ObjectBase) == 0x20);
static_assert(offsetof(FPackageInfo, ObjectCount) == 0x24);
static_assert(offsetof(FPackageInfo, LocalGeneration) == 0x28);
static_assert(offsetof(FPackageInfo, RemoteGeneration) == 0x2C);
static_assert(sizeof(FPackageInfo) == 0x4C);


class UPackageMap_Proxy : public UObject
{
public:

    unsigned char                   Unknown_60[8];                      // 0x60
    TArray<FPackageInfo>            List;                               // 0x68
    TMap<SFXName, int>              PackageMapList;                     // 0x78
    TMap<UClass*, void*>            ClassFieldIndices;                  // 0xC0
};

static_assert(offsetof(UPackageMap_Proxy, Unknown_60) == 0x60);
static_assert(offsetof(UPackageMap_Proxy, List) == 0x68);
static_assert(sizeof(UPackageMap_Proxy) == sizeof(UPackageMap));


#pragma pack(pop)
