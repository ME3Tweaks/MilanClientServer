/**
 * @file        MilanHost/Engine/LinkerPatcher.hpp
 * @brief       Redirects package exports to replacement objects shipped in standalone patch
 *              packages, in the style of ME3's DLC_TestPatch linker hijacking.
 *
 * @remarks     Port of the LExASIs LinkerPatcher ASI (see its HANDOFF.md for the full research
 *              trail). Redirects are read from a compiled coalesced file:
 *
 *                  BioGame/LinkerPatches/CookedPCConsole/Default_LinkerPatches.bin
 *
 *              authored as a BioEngine asset whose `source` attribute is
 *              "..\..\biogame\config\bioengine.ini", containing:
 *
 *                  [LinkerPatchObjects]
 *                  Redirects=(OriginalObject="<MemoryFullPath>",PatchObject="Patch_PackageName")
 *
 *              ME3Tweaks Mod Manager generates this file (its LinkerPatches target merge), so the
 *              format is a contract shared with ME3TweaksCore's LinkerPatchMerge.
 *
 *              OriginalObject is the object's full in-memory path, exactly LegendaryExplorerCore's
 *              ExportEntry.MemoryFullPath (what LEX shows). Matching is case-insensitive.
 *
 *                - Every segment is an instanced name: FName number N+1 prints as Name_N.
 *                - Normal export: <file name>.<instanced path>, with any localization suffix
 *                  stripped from the file name as LEC's StripUnrealLocalization does
 *                  ("BioD_Foo_LOC_INT" -> "BioD_Foo"). The runtime strips LinkerRoot's name the
 *                  same way, so the key matches LEC whether or not the engine kept the suffix.
 *                      SFXGame.SFXWeapon_AssaultRifle_Avenger
 *                      BioD_Foo.Main_Sequence.Sub_Sequence.SeqAct_Interp_2   (deep path)
 *                - Forced export (EF_ForcedExport on the export or any export above it): no file
 *                  name, the forced package is a root object.
 *                      SFXGameContent.SFXPowerCustomAction_Foo   (in, say, Startup_INT.pcc)
 *                - Export parented under an import (essentially never seen in cooked packages):
 *                  both the engine's real path (no file name) and LEC's spelling (file name
 *                  prefixed) are accepted.
 *
 *              A path needs at least two segments; shorter entries are rejected with a warning
 *              when the table is built.
 *
 *              Level objects are unsupported: an OriginalObject with a `TheWorld` segment
 *              (BioP_MPCommon.TheWorld.PersistentLevel.SeqAct_Log_3) is rejected with a warning
 *              (Mod Manager rejects it at merge time too). World unload and GC make them
 *              unmanageable: the rooted patch object (see Lifetime) would keep every level object
 *              it references alive past the level's unload, and be handed back, state and all,
 *              when the level loads again.
 *
 *              Patch_PackageName.pcc lives in the same folder. The patch object is looked up at
 *              <PatchObject>.<OriginalObject>, i.e. the original's full memory path under the patch
 *              package, and must be of the same class as the original (a mismatch is logged and the
 *              original kept). So the package mirrors the cooked structure, e.g. for OriginalObject
 *              GUI_SF_Splash.Splash_I2 it holds GUI_SF_Splash (Package) > Splash_I2 (Texture2D),
 *              found at Patch_PackageName.GUI_SF_Splash.Splash_I2.
 *
 *              Un-forcing: cooked structure usually means the tree root (GUI_SF_Splash) is a forced
 *              export, which would make it the global root package GUI_SF_Splash and let seekfree
 *              dedup hand back the real, already-loaded objects instead of the patch data. So when a
 *              patch package's linker is created (FinalizeCreation), EF_ForcedExport is cleared on
 *              each targeted tree: the top-level export named after the first OriginalObject segment
 *              of any redirect into that package, and all its descendants. That tree then loads as a
 *              group of the patch package, at a path unique to it. Other forced exports in the
 *              package are left alone, as shared dependencies that resolve to the real objects.
 *
 *              Hazard: an object in a NON-targeted forced tree is never patched. If the real object is
 *              already loaded, the patch package's copy dedupes to it; if not, the copy is created at
 *              the real path and becomes the global object for everyone, until GC. Which happens
 *              depends on load order, so those objects must be unmodified copies of the originals.
 *
 *              That folder is not a DLC and is not in PCConsoleTOC.bin, so the config is registered
 *              into GConfig directly and the packages are resolved by FindPackageFile.
 *
 *              By-name lookups: redirecting export slots only covers objects reached through a
 *              linker's tables. Plenty of code instead resolves a path string at runtime, and finds
 *              the original, which still exists at the real path (the patch object lives under the
 *              patch package). The case that prompted this: SFXGUIInteraction.OpenMovie takes a
 *              Tag, looks its MovieLibrary entry up in [SFXGame.SFXGUIInteraction] (BioUI), and
 *              loads GFxResource="GUI_SF_MainMenu.MainMenu" by name (OpenMovie @ RVA 0xBC73B0 ->
 *              UGFxMovie slot 118 -> StaticLoadObject(GFxMovieInfo, NULL, path)); GFx resolves
 *              img:// texture URLs inside a movie the same way (RVA 0x925EA0). So the menu was
 *              patched while EntryMenu's own references opened it, and the original came back once
 *              OpenMovie(MainMenu) did (returning from the multiplayer menu).
 *
 *              StaticLoadObject and StaticFindObject are therefore hooked too. A lookup whose full
 *              path is a redirect key returns the patch object instead: StaticLoadObject loads the
 *              patch package on demand (after the original, for the same reason CreateExport does),
 *              StaticFindObject only answers with it once that package is loaded, as it must never
 *              load. The same class check applies. Paths from an explicit outer are built from the
 *              outer chain; a localization suffix on the first segment is stripped as for keys.
 *
 *              Lifetime: a patch object is added to the root set when it's first redirected to,
 *              so it and everything it references (its outers, the patch package and linker
 *              included) live for the rest of the process.
 *
 *              Discarded originals: CreateExport builds the real object first (so its class and the
 *              patch package's imports are resident), then swaps the patch object into the export
 *              slot. The original keeps pointing at that slot through Linker/LinkerIndex, and the
 *              engine never cleans that up: ULinkerLoad::DetachExport only nulls a slot whose object
 *              belongs to another linker, and UObject::BeginDestroy's SetLinker(NULL) writes through
 *              the linker's ExportMap without checking the slot still holds the object. Once the
 *              linker has been reset and its ExportMap freed, the GC destroying the original
 *              crashes there. So each original is queued by CreateExport and, after the outermost
 *              UObject::EndLoad has preloaded and post-loaded it, detached by writing
 *              Linker = NULL / LinkerIndex = INDEX_NONE directly (not via SetLinker, which would
 *              clear the slot that now holds the patch object). It can't be done sooner: EndLoad
 *              preloads every new export through Obj->Linker without a null check.
 *
 *              All seven entry points are driven from hooks in Engine/Hooks.cpp. They rely on
 *              LoadGate to have every hook live before the first package load.
 */

#pragma once

#include <LESDK/Headers.hpp>
#include <LESDK/Init.hpp>


namespace LinkerPatcher
{
    /** Resolves the native config / object functions this module calls. Hooks are installed by CGlobals. */
    void Initialize(::LESDK::Initializer& Context);

    /**
     * Registers Default_LinkerPatches.bin into GConfig and builds the redirect table, once.
     * Called from LoadPackage_hook: GConfig does not exist yet at attach time, but always does
     * by the first package load.
     */
    void EnsureConfigLoaded();

    /**
     * Called after the original FindPackageFile failed. Supplies the on-disk path if InName is
     * one of our patch packages, which the TOC-backed lookup can never find.
     */
    bool ResolvePatchPackageFile(wchar_t const* InName, FString* OutFileName);

    /** Body of ULinkerLoad::CreateExport's hook; calls the original itself. */
    UObject* CreateExport(ULinkerLoad* Context, int Index);

    /**
     * Body of ULinkerLoad::FinalizeCreation's hook; calls the original itself. Un-forces a patch
     * package's targeted trees first: the export map is complete by then, and nothing has read
     * EF_ForcedExport yet.
     */
    INT FinalizeCreation(ULinkerLoad* Context);

    /**
     * Body of UObject::EndLoad's hook; calls the original itself, then detaches the discarded
     * originals that loading has finished with (see "Discarded originals" above).
     */
    void EndLoad();

    /**
     * StaticFindObject lets us redirect lookups to ours
     */
    UObject* StaticFindObject(UClass* ObjectClass, UObject* InObjectPackage, wchar_t const* OrigInName, UBOOL bExactClass);

    /**
     * StaticLoadObject lets us redirect loads to ours
     */
    UObject* StaticLoadObject(UClass* ObjectClass, UObject* InOuter, wchar_t const* InName, wchar_t const* Filename,
        INT LoadFlags, UPackageMap* Sandbox, UBOOL bAllowObjectReconciliation);
}
