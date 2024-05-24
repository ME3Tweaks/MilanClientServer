/**
 * @file        MilanHost/Intercept.hpp
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


/**
 * @brief       Primitive for setting interception of a native function at an arbitrary place.
 * @remarks     This basically handles most of the dirty work for setting up code caves.
 */
class MiddleIntercept : public NonCopyable
{
    /** Brief description of this intercept. */
    FString Label{ L"" };
    /** Address of an ASM function which should run within the code cave. */
    void* StubFunction{ nullptr };

    /** Address of the first overriden instruction. */
    unsigned char* InterceptAddress{ nullptr };
    /** Address of the first unmodified instruction after @ref InterceptAddress. */
    unsigned char* JumpBackAddress{ nullptr };

    /** Address of the allocated code cave. */
    unsigned char* AllocatedAddress{ nullptr };
    /** Address past the last encoded instruction in the code cave. */
    mutable unsigned char* AllocatedCursor{ nullptr };
    /** Number of bytes allocated at @ref AllocatedAddress. */
    size_t AllocatedSize{ 0 };

public:

    explicit MiddleIntercept(wchar_t const* InLabel) noexcept;
    MiddleIntercept(wchar_t const* InLabel, void* InAddress, void* InStubFunction);
    ~MiddleIntercept() noexcept;

private:

    void InitInternal(void* InAddress, void* InStubFunction);
    void UpdateProtect(void* InAddress, size_t Length, bool bEnable) const;
    void EncodeInternal(ZydisEncoderRequest* Request, unsigned char*& Buffer, const wchar_t* Desc) const;
    void EncodeInternalAt(ZydisEncoderRequest* Request, unsigned char*& Buffer, unsigned char*& Address, const wchar_t* Desc) const;

    /** Encodes a call to stub function in the cave. */
    void WriteCallStub();
    /** Encodes a jump to the code cave in the intercepted address, and re-encodes original instructions in the cave. */
    void WriteInstallInstrs();
    /** Encodes a jump back to the original code flow in the cave. */
    void WriteJumpBack();

    static size_t GetPageSize() noexcept;
};
