/**
 * @file        MilanHost/Engine/ConsoleAutoComplete.hpp
 * @brief       Re-implements Engine.Console's Tab-completion, which ships in LE3 with a fully
 *              working UnrealScript/UI side but an empty native BuildRuntimeAutoCompleteList,
 *              so the auto-complete tree it walks is never populated.
 */

#pragma once


namespace ConsoleAutoComplete
{
    /** Attempts to install the Engine.Console native hijack; false means the class is not loaded yet. */
    bool TryInstall();

    /**
     * Rebuilds the live console's auto-complete list, if a viewport console currently exists.
     * Call this after each successful UGameEngine::LoadMap - the first call is what actually
     * populates the list for the first time, since nothing else in LE3 ever triggers a build.
     */
    void RebuildForCurrentLevel();
}
