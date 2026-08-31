#pragma once

// This file is used for shared version info in C++ and RC files.
// This file is effectively templated and should be located in all ASI mod projects.

// MILAN SPECIFIC
#define MILAN_WINDOW_ICON 101

// Todo: Move to a single version file for all asis
#define CURRENT_YEAR "2026"

#define DEVELOPER_A "ME3Tweaks"
#define DEVELOPER_W L"ME3Tweaks"
#define ASI_NAME_A "Milan Host"
#define ASI_NAME_W L"Milan Host"
#define ASI_NAME_NO_SPACE_A "MilanHost"
#define ASI_NAME_NO_SPACE_W L"MilanHost"


#define ASI_VERSION 3
// String that appears in the file description and part of SPI
#define ASI_DESCRIPTION "Milan Host is the native component for the Project Milan mod."


// === Identification from automated ME3Tweaks tooling ====
#define GAME_PREFIX_RC    "LMP"
#define ASI_GROUP_ID_RC 501
#define ASI_GAME_ID_RC 8 // 7 is launcher, 8 is LMP


// Constants for all ASIs - DO NOT CHANGE BELOW
// Helper macros for converting version numbers to strings
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

// Comma-separated version for use in the .rc file's FILEVERSION/PRODUCTVERSION
// and logging in c++.
// Version.Unused.GameId.GroupId
#define APP_VERSION_RC ASI_VERSION, 0, ASI_GAME_ID_RC, ASI_GROUP_ID_RC
#define VERSION_STRING_A TOSTRING(ASI_VERSION) ".0." TOSTRING(ASI_GAME_ID_RC) "." TOSTRING(ASI_GROUP_ID_RC)
#define VERSION_STRING_W TOSTRING(ASI_VERSION) L".0." TOSTRING(ASI_GAME_ID_RC) L"." TOSTRING(ASI_GROUP_ID_RC)
