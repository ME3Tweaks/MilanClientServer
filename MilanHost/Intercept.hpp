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
class MiddleIntercept
{
    /** Brief description of this intercept. */
    wchar_t const* Label{ L"" };

    /** Address of a code cave allocated. */
    void* AllocatedCode{ nullptr };

    /** Address of the first overriden instruction. */
    void* FirstJumpSource{ nullptr };

    /** Address of the first unmodified instruction after @ref FirstJumpSource. */
    void* SecondJumpDest{ nullptr };

public:

    MiddleIntercept() = default;
    explicit MiddleIntercept(wchar_t const* InLabel) noexcept;

    void Init(void* Address);

private:

    void EncodeInternal(const wchar_t* Label,
        ZydisEncoderRequest* Request, unsigned char*& Buffer) const;
    void UpdateProtect(void* Address, size_t Length, bool bEnable) const;

    static constexpr size_t k_maxFirstJump = 128;
    size_t EncodeFirstJump(std::array<unsigned char, k_maxFirstJump>& Buffer) const;
    size_t InstallFirstJump(std::span<unsigned char> JumpBytes,
        std::array<unsigned char, k_maxFirstJump>& OutOrigBuffer) const;

    static size_t GetPageSize() noexcept;
};
