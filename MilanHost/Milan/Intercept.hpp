/**
 * @file        MilanHost/Milan/Intercept.hpp
 * @brief       Utilities for intercepting and overriding functions.
 */

#pragma once

#include <array>
#include <span>

#include <Zydis/Zydis.h>
#include <LESDK/Headers.hpp>

#include "Containers/vector.hpp"


/**
 * @brief       Slightly overengineered way to declaratively hook an @ref UFunction.
 * @warning     This @em will slow down execution to a crawl if the function cannot be found.
 *              Prefer good old string comparison for speculatively hooking @ref UFunction calls.
 */
class FunctionInterceptLegacy
{
    UStruct* TargetNode{ nullptr };
    wchar_t const* TargetFullName{ nullptr };

    // Can't use ::game::vector here because it would pre-allocate
    // in dynamic initializer, i.e. before we have the GMalloc pointer.
    static TArray<FunctionInterceptLegacy*> RegisteredIntercepts;

public:

    FunctionInterceptLegacy();
    bool Applies(UStruct* Node, wchar_t const* FullName);

    static void InvalidateAllRegistered();
    static void CleanupRegistered();

};


// ! Mid-function intercept logic.
// ========================================

/**
 * @brief       Self-initializing registry for potential code cave locations.
 * @warning     This might conflict with @c minhook built into @c LEBinkProxy.
 */
class CaveIndexer final : public NonCopyable
{
public:

    struct CCaveInfo
    {
        std::intptr_t Address = 0;
        std::ptrdiff_t Size = 0;
    };

    static constexpr std::ptrdiff_t k_minCaveSize = 8;

    /** Initializes this type and scans main module for @c INT3 sequences. */
    explicit CaveIndexer(std::ptrdiff_t MinSize);

    /**
     * @brief   Retrieves an available cave entry and marks it as occupied.
     * @param   MinSize - Smallest number of bytes the cave must have.
     * @return  Pointer to a cave info, or @c nullptr if there are no caves bigger than @c MinSize.
     */
    CCaveInfo const* GetForSize(std::ptrdiff_t MinSize);

private:

    static std::span<unsigned char> ViewMainModule();

    struct CCaveOccupancy
    {
        bool Occupied = false;
        CCaveOccupancy(bool const Value) : Occupied{ Value } { }
        operator bool() const noexcept { return Occupied; }
    };

    game::vector<CCaveInfo>         Caves{};
    game::vector<CCaveOccupancy>    Occupancy{};
    std::ptrdiff_t                  IndexMinSize{ 0 };
    std::ptrdiff_t                  IndexMaxSize{ 0 };
};


#ifndef _FINAL_RELEASE

/**
 * @brief       Primitive for setting interception of a native function at an arbitrary place.
 * @remarks     This basically handles most of the dirty work for setting up code caves.
 */
class MiddleIntercept : public NonCopyable
{
    /** Brief description of this intercept. */
    FString Label{ L"" };

    /** Address of an ASM function which should be injected. */
    void* Function{ nullptr };

    /** Cave indexer to use throughout initialization. */
    CaveIndexer* Indexer{ nullptr };

    /** Pointer for the injection's location. */
    unsigned char* Address{ nullptr };

public:

    MiddleIntercept(wchar_t const* InLabel, void* InAddress, void* InStubFunction, CaveIndexer* InCaveIndexer);
    ~MiddleIntercept() noexcept;

    static void Install(wchar_t const* InLabel, int InRVA, void* InStubFunction, CaveIndexer& InCaveIndexer);

private:

    void Init(void* InAddress, void* InStubFunction, CaveIndexer* InCaveIndexer);

    void EncodeCallCave(ZydisEncoderRequest* Scratch, void* RelayAddress, unsigned char*& CallCursor, std::ptrdiff_t& CallAvailLength) const;

    void UpdateProtect(void* InAddress, size_t Length, bool bEnable) const;
    void UpdateProtect(CaveIndexer::CCaveInfo const& InCave, bool bEnable) const;
    void DebugDisassemble(unsigned char* Pointer, size_t Length);

    static size_t GetPageSize() noexcept;
    static std::intptr_t GetMainBase() noexcept;
};

#endif // !_FINAL_RELEASE


// ! UScript VM macros to write custom native functions.
// ========================================

#define NATIVE_GET_NAME(NAME) \
    SFXName NAME{}; \
    Stack->Step(Stack->Object, &NAME);

#define NATIVE_GET_NAME_OPTIONAL(NAME, DEFAULT) \
    SFXName NAME = DEFAULT; \
    (*pGRuntimeUCFlags) &= ~2; \
    Stack->Step(Stack->Object, &NAME);

#define NATIVE_GET_STRING(NAME) \
    FString NAME{}; \
    Stack->Step(Stack->Object, &NAME);

#define NATIVE_GET_STRING_OPTIONAL(NAME, DEFAULT) \
    FString NAME{ DEFAULT }; \
    (*pGRuntimeUCFlags) &= ~2; \
    Stack->Step(Stack->Object, &NAME);

#define NATIVE_FINISH() \
    Stack->Code++; \
    if (*Stack->Code == 0x41) \
        Stack->Step(Stack->Object, NULL);
