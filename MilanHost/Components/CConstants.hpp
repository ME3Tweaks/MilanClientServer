/**
 * @file        MilanHost/Components/CConstants.hpp
 * @header      Component for overwriting data constants in the executables.
 */

#pragma once
#include "Base/Base.hpp"
#include "Engine/Hooks.hpp"


class CConstants final : public NonCopyable
{
public:
    static constexpr wchar_t const* k_typeName = L"directories";

    bool Initialize(::LESDK::Initializer& Context);
    void Terminate(::LESDK::Initializer& Context);

    /// Applies the LE3 minimum-resolution compatibility patch before game PreInit runs.
    static void ApplyEarlyMinimumResolutionCompatibility(::LESDK::Initializer& Context);
    /// Stops the bounded startup dialog watchdog during ASI detach.
    static void StopMinimumResolutionDialogWatchdog();

private:

    /// <summary>
    /// Redirects the documents directory to load LMP instead of ME3 so it is self contained game
    /// </summary>
    /// <param name="Context"></param>
    void PatchDocumentsDirectory(::LESDK::Initializer& Context);

    /// <summary>
    /// Patches the mount values to allow it to go to 1 instead of starting at 1000
    /// </summary>
    /// <param name="Context"></param>
    void PatchMountLoading(::LESDK::Initializer& Context);

    /// <summary>
    /// Allows DLC folder names to contain 'MP'
    /// </summary>
    /// <param name="Context"></param>
    void PatchMPDLCFolderNameFilter(::LESDK::Initializer& Context);

    /// <summary>
    /// Makes the boot-time UGameEngine::LoadMap (the one that loads EntryMenu) mount DLC, so DLC
    /// packages can override EntryMenu. Stock only mounts when leaving a menu level.
    /// </summary>
    /// <param name="Context"></param>
    void PatchMountDLCBeforeEntryMenu(::LESDK::Initializer& Context);

    /// <summary>
    /// NOPs out the NetMode/GPendingLevel gates (and the resulting GDisallowNetworkTravel write)
    /// that stock UE3 uses to disable the 'set'/'setnopec' (UGameEngine::Exec) and
    /// 'show'/'showset'/'showclear' (UGameViewportClient::Exec) console commands in multiplayer.
    /// </summary>
    /// <param name="Context"></param>
    void PatchMultiplayerCommandGates(::LESDK::Initializer& Context);
    /// <summary>
    /// Diables EA's BugSentry crash reporter
    /// </summary>
    void DisableBugSentry(::LESDK::Initializer& Context);
};

static_assert(ComponentType<CConstants>);
