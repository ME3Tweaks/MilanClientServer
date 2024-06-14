/**
 * @file        MilanHost/State.hpp
 * @brief       Subsystem for managing ECSM and associate logic.
 */

#pragma once
#include <LESDK/Init.hpp>

class UObject;
struct FFrame;


namespace Milan::State
{
    // ! Global subsystem functions.
    // ========================================

    void Initialize(::LESDK::Initializer& Context);
    void Terminate();


    // ! Public interface.
    // ========================================

    bool On_UObject_ProcessInternal_Pre(UObject* Context, FFrame* Stack, void* Result);
    void On_UObject_ProcessInternal_Post(UObject* Context, FFrame* Stack, void* Result);
    void On_UWorld_Tick(UWorld* Context, int Unknown, float Delta);
}
