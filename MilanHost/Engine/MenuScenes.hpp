#pragma once

#include "Base/Base.hpp"

class CCommands;

/**
 * Live main menu backdrops. Pick a random one to replace the stock EntryMenu map, and load its streaming levels and textures before the load screen comes down
 */
namespace MenuScenes
{
    /** Rewrites a load of EntryMenu to a menu scene, keeping the URL's options */
    void RedirectEntryMenu(FURL* Url);

    /** If the map is EntryMenu or one of the menu scenes that replace it */
    bool IsMainMenuMap(wchar_t const* MapName);

    /**
     * Called at the end of a successful main menu LoadMap: loads the scene's streaming levels and blocks on texture
     * streaming, so the scene is ready before the load screen comes down
     */
    void FinishLoading();

    /** Registers milanmenu.pos, which logs the view for a scene file (non-final builds) */
    void RegisterCommands(CCommands& Commands);
}
