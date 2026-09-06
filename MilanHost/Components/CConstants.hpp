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

private:

    /// <summary>
    /// Allows the game to load our LMP licensee packages
    /// </summary>
    /// <param name="Context"></param>
    void PatchLicenseeVersion(::LESDK::Initializer& Context);

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
    /// NOPs out the NetMode/GPendingLevel gates (and the resulting GDisallowNetworkTravel write)
    /// that stock UE3 uses to disable the 'set'/'setnopec' (UGameEngine::Exec) and
    /// 'show'/'showset'/'showclear' (UGameViewportClient::Exec) console commands in multiplayer.
    /// </summary>
    /// <param name="Context"></param>
    void PatchMultiplayerCommandGates(::LESDK::Initializer& Context);
};

static_assert(ComponentType<CConstants>);
