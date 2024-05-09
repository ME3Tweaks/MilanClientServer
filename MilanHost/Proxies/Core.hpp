/**
 * @file        Proxies/Core.hpp
 * @brief       This file contains UClass proxy types for @c Core package.
 * @remarks     Ported from MilanASI repo, originally via Ghidra decompilation.
 */

#pragma once
#pragma pack(push, 4)

#include <LESDK/Headers.hpp>


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
    BYTE                            LoadingPhase;
};

static_assert(offsetof(FPackageInfo, ObjectBase) == 0x20);
static_assert(offsetof(FPackageInfo, ObjectCount) == 0x24);
static_assert(offsetof(FPackageInfo, LocalGeneration) == 0x28);
static_assert(offsetof(FPackageInfo, RemoteGeneration) == 0x2C);


class UPackageMap_Proxy : public UObject
{
public:

    BYTE                            Unknown60[8];
    TArray<FPackageInfo>            List;
    TMap<SFXName, int>              PackageMapList;
    TMap<UClass*, void*>            ClassFieldIndices;
};

static_assert(offsetof(UPackageMap_Proxy, List) == 0x68);
static_assert(sizeof(UPackageMap_Proxy) == sizeof(UPackageMap));


#pragma pack(pop)
