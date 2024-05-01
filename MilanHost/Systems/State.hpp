/**
 * @file        MilanHost/State.hpp
 * @brief       Subsystem for managing ECSM and associate logic.
 */

#pragma once


struct FFrame;
class UObject;

namespace LESDK { class Initializer; }


namespace Milan::State
{
    // ! Global subsystem functions.
    // ========================================

    void Initialize(::LESDK::Initializer& Context);
    void Terminate();


    // ! Public interface.
    // ========================================

    bool On_UObject_ProcessInternal(UObject* Context, FFrame* Stack, void* Result);
    void On_UWorld_Tick(UWorld* Context, int Unknown, float Delta);
}
