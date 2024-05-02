/**
 * @file        MilanHost/Intercept.hpp
 * @brief       Utilities for intercepting / overriding UFunction calls.
 */

#pragma once

#include <LESDK/Headers.hpp>
#include "Containers/vector.hpp"


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
