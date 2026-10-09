#include <algorithm>
#include <atomic>
#include <cwctype>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Base/IOUtils.hpp"
#include "Base/Logger.hpp"
#include "Engine/Hooks.hpp"
#include "Engine/LinkerPatcher.hpp"


namespace LinkerPatcher
{
    // FConfigCacheIni::CreateConfigFile(GConfig, RawFileBytes, DisplayName, Flags, RegisterFastLookup).
    // RawFileBytes is the whole compiled coalesced file exactly as read from disk; the engine decodes it.
    using t_CreateConfigFile = void*(void* Config, void* RawFileBytes, wchar_t const* DisplayName, unsigned long long Flags, int bRegister);

    // FConfigCacheIni::GetArray(GConfig, Section, Key, OutArray, Filename). Each layer looks the three
    // names up by case-insensitive *hash* (Filename -> Section -> Key); any miss silently yields nothing.
    using t_GetArray = void(void* Config, wchar_t const* Section, wchar_t const* Key, TArray<FString>* OutArray, wchar_t const* Filename);

    // UObject::StaticFindObject(Class, Package, Name, bExactClass). Pure in-memory lookup that accepts
    // a null class as a wildcard. (StaticLoadObject cannot: ULinkerLoad::Create dereferences the class.)
    using t_StaticFindObject = UObject*(UClass* Class, UObject* Package, wchar_t const* Name, int bExactClass);

    static void** gp_config = nullptr;
    // GObjBeginLoadCount: the BeginLoad / EndLoad nesting depth. Zero once the outermost EndLoad is done.
    static int const* gp_objBeginLoadCount = nullptr;
    static t_CreateConfigFile* CreateConfigFile = nullptr;
    static t_GetArray* GetArray = nullptr;
    static t_StaticFindObject* StaticFindObject = nullptr;

    // Bookkeeping name of our layer inside GConfig, like a DLC mount's folder name.
    static constexpr wchar_t const* k_configDisplayName = L"LinkerPatches";
    static constexpr wchar_t const* k_configPath = L"../../BioGame/LinkerPatches/CookedPCConsole/Default_LinkerPatches.bin";
    static constexpr wchar_t const* k_patchFolder = L"../../BioGame/LinkerPatches/CookedPCConsole/";

    static constexpr wchar_t const* k_configSection = L"linkerpatchobjects";
    static constexpr wchar_t const* k_configKey = L"redirects";

    // The first trie level of a BINI layer is the CoalesceAsset's `source` attribute, verbatim.
    // Tried in order; the first one with entries wins (and is logged).
    static constexpr wchar_t const* k_configFilenameCandidates[] = {
        L"..\\..\\biogame\\config\\bioengine.ini",
        L"..\\..\\BIOGame\\Config\\DefaultEngine.ini",
        L"bioengine.ini",
    };

    // FObjectExport::ExportFlags bit: the export's top-level package is created as a root object
    // instead of under LinkerRoot (CreateExport @ RVA 0xFBDD0 tests `ExportFlags & 1` to choose).
    // The only other readers (Ghidra, 11.3.2) all run after FinalizeCreation's body starts:
    // FinalizeCreation itself (net object counts, RVA 0xFF520), DissociateImportsAndForcedExports
    // (RVA 0x123A50, plus its copy inlined into EndLoad) and GetExportPathName (RVA 0x100620, naming only).
    static constexpr int k_exportFlagForcedExport = 0x1;            // EF_ForcedExport

    // ULinkerLoad::bHasFinishedInitialization. FinalizeCreation (RVA 0xFF520) opens with
    // `cmp dword [rcx+0x680], 0` and skips its body once it's set; the body sets it last.
    static constexpr size_t k_linkerFinishedInitializationOffset = 0x680;

    // The path segment every level object sits under. Patching level objects is unsupported.
    static constexpr wchar_t const* k_worldSegmentLower = L"theworld";

    // UObject::ObjectFlags bit the GC seeds its reachability walk from (Ghidra: AddToRoot @ RVA
    // 0x121BB0 is `or qword [rcx+0xC], 0x4000`; the GC root scan @ RVA 0x72590 keeps bit 14).
    static constexpr unsigned long long k_objectFlagRootSet = 0x4000ull;   // RF_RootSet

    // UObject::ObjectFlags bits that mean a freshly created export is still mid-load (Ghidra, 11.3.2).
    // CreateExport @ RVA 0xFBDD0 constructs every export with both set; EndLoad (RVA 0x123BF0) clears
    // NeedLoad when it preloads the object and ConditionalPostLoad (RVA 0x122910) clears NeedPostLoad.
    static constexpr unsigned long long k_objectFlagNeedLoad = 0x20000000000ull;        // RF_NeedLoad
    static constexpr unsigned long long k_objectFlagNeedPostLoad = 0x100000000000000ull; // RF_NeedPostLoad

    // Bounds an OuterIndex walk, so a corrupt (cyclic) chain fails instead of spinning.
    static constexpr int k_maxOuterDepth = 128;

    // The registered layer references this buffer, so it lives as long as the process.
    static ::game::vector<byte> g_configBuffer{};

    struct FRedirectTarget
    {
        std::wstring PatchPackage;      // As authored: the patch package's name.
        std::wstring OriginalObject;    // As authored: the patch object is <PatchPackage>.<OriginalObject>.
    };

    // Memory full path (lowercase) -> where its patch object lives.
    static std::unordered_map<std::wstring, FRedirectTarget> g_redirects{};
    // Last path segment of every redirect key (lowercase): CreateExport's cheap pre-filter.
    static std::unordered_set<std::wstring> g_leafNames{};
    // Patch package name (lowercase) -> original casing, for FindPackageFile and loading.
    static std::unordered_map<std::wstring, std::wstring> g_patchPackages{};
    // Patch package name (lowercase) -> its targeted tree roots: the first OriginalObject segment of
    // every redirect into it, lowercase -> original casing. FinalizeCreation un-forces these trees.
    static std::unordered_map<std::wstring, std::unordered_map<std::wstring, std::wstring>> g_patchTreeRoots{};

    // Package loading is multi-threaded. g_redirects / g_leafNames / g_patchPackages /
    // g_patchTreeRoots are only written under g_configMutex, and only read once gb_redirectsReady
    // has been published - after which they are immutable, so the hot read paths stay lock-free.
    static std::mutex g_configMutex{};
    static bool gb_configLoadAttempted = false;
    static std::atomic_bool gb_redirectsReady{ false };

    static std::mutex g_patchLoadMutex{};
    static std::unordered_set<std::wstring> g_attemptedPatchLoads{};

    // Suppresses redirects while this thread loads a patch package, so one can't recurse into itself.
    static thread_local int t_patchLoadDepth = 0;

    // Reused per thread by CreateExport's leaf pre-filter, so the common no-match case doesn't
    // allocate a std::wstring for every export of every package.
    static thread_local std::wstring t_leafScratch{};

    /**
     * An original object CreateExport built and then discarded in favor of its patch object. It still
     * points at its linker's export slot, which now holds the patch object; see CreateExport and
     * DetachDiscardedOriginals.
     */
    struct FDiscardedOriginal
    {
        UObject* Object;            // Raw pointer: nothing keeps it alive, so it is validated before every use.
        int InternalIndex;          // Object->ObjectInternalInteger, i.e. its GObjObjects slot.
        UObject* Linker;            // What Object->Linker was when CreateExport returned.
        long long LinkerIndex;      // What Object->LinkerIndex was.
    };

    // Written by CreateExport, drained by the EndLoad hook. Package loading is multi-threaded (see
    // above), so both sides take the mutex; the atomic count only lets the EndLoad hook, which runs
    // for every load, skip the lock while nothing is pending. Nothing engine-side is called with
    // the mutex held.
    static std::mutex g_discardedMutex{};
    static std::vector<FDiscardedOriginal> g_discardedOriginals{};
    static std::atomic<size_t> g_discardedCount{ 0 };


    static std::wstring ToLower(std::wstring Value)
    {
        std::transform(Value.begin(), Value.end(), Value.begin(),
            [](wchar_t const C) { return static_cast<wchar_t>(std::towlower(C)); });
        return Value;
    }

    static std::wstring ToStd(FString const& Value)
    {
        return Value.Chars() != nullptr ? Value.Chars() : L"";
    }

    /** Parses (OriginalObject="...",PatchObject="..."). False if either field is missing or empty. */
    static bool ParseRedirectEntry(std::wstring const& Raw, std::wstring& OutOriginal, std::wstring& OutPatch)
    {
        auto const ExtractField = [&Raw](wchar_t const* const FieldName, std::wstring& OutValue) -> bool
            {
                std::wstring const Needle = std::wstring(FieldName) + L"=\"";
                size_t const Start = Raw.find(Needle);
                if (Start == std::wstring::npos)
                    return false;
                size_t const ValueStart = Start + Needle.size();
                size_t const ValueEnd = Raw.find(L'"', ValueStart);
                if (ValueEnd == std::wstring::npos)
                    return false;
                OutValue = Raw.substr(ValueStart, ValueEnd - ValueStart);
                return !OutValue.empty();
            };

        return ExtractField(L"OriginalObject", OutOriginal) && ExtractField(L"PatchObject", OutPatch);
    }

    /** Splits an OriginalObject path on '.'. Empty segments are kept, so the caller can reject them. */
    static std::vector<std::wstring> SplitPath(std::wstring const& Path)
    {
        std::vector<std::wstring> Segments{};
        size_t Start = 0;
        while (true)
        {
            size_t const Dot = Path.find(L'.', Start);
            Segments.push_back(Path.substr(Start, Dot == std::wstring::npos ? std::wstring::npos : Dot - Start));
            if (Dot == std::wstring::npos)
                return Segments;
            Start = Dot + 1;
        }
    }

    /**
     * Port of LegendaryExplorerCore's StripUnrealLocalization (MELocalization.cs), which
     * ExportEntry.MemoryFullPath applies to the file name: "BioD_Foo_LOC_INT" -> "BioD_Foo",
     * "Startup_DEU" -> "Startup". Detection is GetUnrealLocalization's, suffix list included.
     */
    static std::wstring StripUnrealLocalization(std::wstring const& Name)
    {
        std::wstring Upper = Name;
        std::transform(Upper.begin(), Upper.end(), Upper.begin(),
            [](wchar_t const C) { return static_cast<wchar_t>(std::towupper(C)); });

        // Same probe order as LEC: a trailing "LOC_xxx", else whatever follows the last '_', else
        // (no usable '_') the whole name.
        std::wstring Suffix = Upper;
        size_t const LocPos = Upper.rfind(L"LOC_");
        size_t const Underscore = Upper.rfind(L'_');
        if (LocPos != std::wstring::npos && LocPos > 0 && LocPos + 6 >= Upper.size())
            Suffix = Upper.substr(LocPos);
        else if (Underscore != std::wstring::npos && Underscore > 0 && Upper.size() > Underscore + 1)
            Suffix = Upper.substr(Underscore + 1);

        static std::unordered_set<std::wstring> const k_localizationSuffixes{
            L"DE", L"GE", L"DEU", L"LOC_DEU", L"LOC_DE",
            L"ES", L"ESN", L"LOC_ES", L"LOC_ESN",
            L"FR", L"FE", L"FRA", L"LOC_FRA", L"LOC_FR",
            L"INT", L"LOC_INT",
            L"IT", L"IE", L"ITA", L"LOC_ITA", L"LOC_IT",
            L"JA", L"JPN", L"LOC_JPN",
            L"PL", L"POL", L"PLPC", L"LOC_POL", L"LOC_PLPC", L"LOC_PL",
            L"RA", L"RU", L"RUS", L"LOC_RUS", L"LOC_RA", L"LOC_RU",
        };
        if (!k_localizationSuffixes.contains(Suffix))
            return Name;

        // LEC cuts at the last '_' and then drops a trailing "_LOC". With no '_' at all (a package
        // literally named "INT") LEC throws; leaving the name alone is the only sane equivalent.
        if (Underscore == std::wstring::npos)
            return Name;
        std::wstring Stripped = Name.substr(0, Underscore);
        if (Stripped.size() >= 4 && Upper.compare(Stripped.size() - 4, 4, L"_LOC") == 0)
            Stripped.resize(Stripped.size() - 4);
        return Stripped;
    }

    /**
     * Builds an export's in-memory path from the linker's own tables (loads nothing), lowercased,
     * to match against g_redirects. This is LegendaryExplorerCore's ExportEntry.MemoryFullPath:
     *
     *   - every segment is the instanced name, "Name_N" for FName number N+1 (SFXName::ToString);
     *   - if the export, or any export above it before the chain reaches an import, has
     *     EF_ForcedExport, the path is just the joined chain (the forced package is created as a
     *     root object - CreateExport @ RVA 0xFBDD0 calls CreatePackage(NULL, Name) for it);
     *   - otherwise it is prefixed with LinkerRoot's name, localization suffix stripped as LEC does.
     *
     * Matching on the path rather than on Context->Filename is deliberate: seekfree cooking
     * duplicates commonly-referenced exports into many unrelated files, and they share one path.
     *
     * A chain that ends at an import is where LEC and the engine disagree. The engine parents the
     * export under the imported object, so its real path has no LinkerRoot prefix; LEC's
     * MemoryFullPath only stops IsForcedExport at an import and so still prefixes the file name.
     * Both spellings are produced for that case (OutAltKey is the LEC one) and either may match.
     * Cooked LE3 packages essentially never parent an export under an import.
     *
     * False if the chain is malformed, or there's no LinkerRoot to prefix with.
     */
    static bool BuildMemoryPathKeys(ULinkerLoad* const Context, FObjectExport const& Export,
        std::wstring& OutKey, std::wstring& OutAltKey)
    {
        // Leaf first; reversed when joining.
        std::vector<std::wstring> Segments{ ToStd(Export.ObjectName.ToString()) };
        bool bForcedExport = (Export.ExportFlags & k_exportFlagForcedExport) != 0;
        bool bImportRooted = false;

        int OuterIndex = Export.OuterIndex;
        for (int Depth = 0; OuterIndex != 0; ++Depth)
        {
            if (Depth >= k_maxOuterDepth)
                return false;

            if (OuterIndex > 0)
            {
                UINT const OuterSlot = static_cast<UINT>(OuterIndex - 1);
                if (OuterSlot >= Context->ExportMap.Count())
                    return false;

                FObjectExport const& Outer = Context->ExportMap(OuterSlot);
                Segments.push_back(ToStd(Outer.ObjectName.ToString()));
                // LEC's IsForcedExport recurses through export parents only.
                if (!bImportRooted && (Outer.ExportFlags & k_exportFlagForcedExport) != 0)
                    bForcedExport = true;
                OuterIndex = Outer.OuterIndex;
            }
            else
            {
                UINT const OuterSlot = static_cast<UINT>(-OuterIndex - 1);
                if (OuterSlot >= Context->ImportMap.Count())
                    return false;

                FObjectImport const& Outer = Context->ImportMap(OuterSlot);
                Segments.push_back(ToStd(Outer.ObjectName.ToString()));
                bImportRooted = true;
                OuterIndex = Outer.OuterIndex;
            }
        }

        std::wstring Path{};
        for (auto Segment = Segments.rbegin(); Segment != Segments.rend(); ++Segment)
        {
            if (!Path.empty())
                Path += L'.';
            Path += *Segment;
        }

        OutAltKey.clear();
        if (bForcedExport)
        {
            OutKey = ToLower(std::move(Path));
            return true;
        }

        if (Context->LinkerRoot == nullptr)
            return false;
        std::wstring const PrefixedPath = StripUnrealLocalization(ToStd(Context->LinkerRoot->GetName())) + L"." + Path;

        if (bImportRooted)
        {
            OutKey = ToLower(std::move(Path));
            OutAltKey = ToLower(PrefixedPath);
        }
        else
        {
            OutKey = ToLower(PrefixedPath);
        }
        return true;
    }

    static bool RegisterConfigLayer(void* const Config)
    {
        if (!ReadFileFull(k_configPath, g_configBuffer))
        {
            MILAN_DEBUGW(ELOG_LinkerPatcher, L"%s not found, no linker patches to load", k_configPath);
            return false;
        }

        if (g_configBuffer.size() < 4)
        {
            MILAN_ERRORW(ELOG_LinkerPatcher, L"%s is too short to be a compiled coalesced file (%llu bytes)",
                k_configPath, (unsigned long long)g_configBuffer.size());
            return false;
        }

        // Real DLC config layers use flags 2 (LiveINI uses 3, a different layer type).
        void* const ConfigFile = (*CreateConfigFile)(Config, g_configBuffer.data(), k_configDisplayName, 2ull, 1);
        if (ConfigFile == nullptr)
        {
            MILAN_ERRORW(ELOG_LinkerPatcher, L"CreateConfigFile returned null for %s", k_configPath);
            return false;
        }

        // FConfigFile keeps its parsed representation at +0x28, which the merge code dereferences.
        void* const ParsedData = *reinterpret_cast<void**>(reinterpret_cast<BYTE*>(ConfigFile) + 0x28);
        if (ParsedData == nullptr)
        {
            MILAN_ERRORW(ELOG_LinkerPatcher, L"CreateConfigFile produced an unusable FConfigFile for %s", k_configPath);
            return false;
        }

        MILAN_INFOW(ELOG_LinkerPatcher, L"registered %s (%llu bytes) into GConfig as '%s'",
            k_configPath, (unsigned long long)g_configBuffer.size(), k_configDisplayName);
        return true;
    }

    static void BuildRedirectTable(void* const Config)
    {
        // GetArray empties its out-array first, so every candidate is a clean attempt.
        TArray<FString> RawEntries{};
        for (wchar_t const* const Candidate : k_configFilenameCandidates)
        {
            (*GetArray)(Config, k_configSection, k_configKey, &RawEntries, Candidate);
            if (RawEntries.Count() > 0)
            {
                MILAN_DEBUGW(ELOG_LinkerPatcher, L"[%s] %s resolved under config filename '%s'", k_configSection, k_configKey, Candidate);
                break;
            }
        }

        if (RawEntries.Count() == 0)
        {
            MILAN_WARNW(ELOG_LinkerPatcher, L"no [%s] %s entries under any known config filename; check the CoalesceAsset "
                L"`source` attribute (`confighash names` prints the layer's File: path)", k_configSection, k_configKey);
            return;
        }

        for (UINT i = 0; i < RawEntries.Count(); ++i)
        {
            std::wstring const Raw = ToStd(RawEntries(i));

            std::wstring Original{}, Patch{};
            if (!ParseRedirectEntry(Raw, Original, Patch))
            {
                MILAN_WARNW(ELOG_LinkerPatcher, L"skipping malformed redirect entry: %s", Raw.c_str());
                continue;
            }

            // Every memory path of a patchable object has at least "Package.Leaf". Anything shorter,
            // or with an empty segment, can never equal a key BuildMemoryPathKeys produces.
            std::vector<std::wstring> const Segments = SplitPath(Original);
            bool const bHasEmptySegment = std::any_of(Segments.begin(), Segments.end(),
                [](std::wstring const& Segment) { return Segment.empty(); });
            if (Segments.size() < 2 || bHasEmptySegment)
            {
                MILAN_WARNW(ELOG_LinkerPatcher, L"skipping redirect %s -> %s: OriginalObject must be a full memory path "
                    L"(Package.Object, as LEX shows MemoryFullPath), it can never match", Original.c_str(), Patch.c_str());
                continue;
            }

            // Level objects are unsupported. A level's objects are destroyed with the world, but a
            // rooted patch object would keep whatever level objects it references alive past that,
            // and be handed back, state and all, the next time the level loads. Mod Manager rejects
            // these at merge time; this is defense in depth.
            bool const bIsLevelObject = std::any_of(Segments.begin(), Segments.end(),
                [](std::wstring const& Segment) { return ToLower(Segment) == k_worldSegmentLower; });
            if (bIsLevelObject)
            {
                MILAN_WARNW(ELOG_LinkerPatcher, L"skipping redirect %s -> %s: level objects (anything under TheWorld) "
                    L"can't be linker patched", Original.c_str(), Patch.c_str());
                continue;
            }

            g_redirects.insert_or_assign(ToLower(Original), FRedirectTarget{ Patch, Original });
            g_leafNames.insert(ToLower(Segments.back()));
            g_patchPackages.insert_or_assign(ToLower(Patch), Patch);
            MILAN_INFOW(ELOG_LinkerPatcher, L"registered redirect %s -> %s.%s", Original.c_str(), Patch.c_str(), Original.c_str());
        }

        // From the final table, so an OriginalObject listed twice only un-forces the tree of the
        // package that won.
        for (auto const& [Key, Target] : g_redirects)
        {
            std::wstring const TreeRoot = Target.OriginalObject.substr(0, Target.OriginalObject.find(L'.'));
            g_patchTreeRoots[ToLower(Target.PatchPackage)].try_emplace(ToLower(TreeRoot), TreeRoot);
        }

        MILAN_INFOW(ELOG_LinkerPatcher, L"%llu redirect(s) active", (unsigned long long)g_redirects.size());
    }

    /**
     * Clears EF_ForcedExport on the targeted trees of a patch package's linker: every export whose
     * top-level ancestor (OuterIndex 0) is named after the first segment of a redirect into this
     * package, that ancestor included. CreateExport then builds the top-level export as a group under
     * LinkerRoot instead of a root package, so the tree loads at <PatchPkg>.<OriginalObject> - unique,
     * since every patch package name is - instead of deduplicating against the real objects.
     *
     * Every other forced export is left alone: those are shared dependencies, meant to resolve to the
     * real game objects as usual.
     *
     * Runs from FinalizeCreation, which is the first point both conditions hold: the export map is
     * fully serialized, and nothing has acted on EF_ForcedExport yet (FinalizeCreation's own net
     * object count reads it, so clearing it at CreateExport time would already be too late for that).
     */
    static void UnforceTargetedTrees(ULinkerLoad* const Context)
    {
        if (!gb_redirectsReady.load(std::memory_order_acquire) || g_patchTreeRoots.empty())
            return;

        // FinalizeCreation is re-entered on every later Tick of the linker; its body (and this) runs once.
        if (*reinterpret_cast<int const*>(reinterpret_cast<BYTE const*>(Context) + k_linkerFinishedInitializationOffset) != 0)
            return;

        // Once per linker, never per export: the CreateExport hot path doesn't pay for this.
        if (Context->LinkerRoot == nullptr)
            return;
        std::wstring const PackageName = ToStd(Context->LinkerRoot->GetName());
        auto const Roots = g_patchTreeRoots.find(ToLower(PackageName));
        if (Roots == g_patchTreeRoots.end())
            return;

        // Per export: the slot of its targeted top-level ancestor, or one of these.
        static constexpr int k_unresolved = -2;
        static constexpr int k_notTargeted = -1;

        UINT const ExportCount = Context->ExportMap.Count();
        std::vector<int> TreeOf(ExportCount, k_unresolved);
        std::vector<UINT> Chain{};

        for (UINT Slot = 0; Slot < ExportCount; ++Slot)
        {
            if (TreeOf[Slot] != k_unresolved)
                continue;

            // Walk up until a resolved export or the top; a chain that reaches an import, leaves the
            // table or runs too deep (cyclic) is not part of any targeted tree.
            Chain.clear();
            int Result = k_notTargeted;
            UINT Current = Slot;
            for (int Depth = 0; Depth < k_maxOuterDepth; ++Depth)
            {
                if (TreeOf[Current] != k_unresolved)
                {
                    Result = TreeOf[Current];
                    break;
                }
                Chain.push_back(Current);

                int const OuterIndex = Context->ExportMap(Current).OuterIndex;
                if (OuterIndex == 0)
                {
                    std::wstring const TopName = ToLower(ToStd(Context->ExportMap(Current).ObjectName.ToString()));
                    Result = Roots->second.contains(TopName) ? static_cast<int>(Current) : k_notTargeted;
                    break;
                }
                if (OuterIndex < 0 || static_cast<UINT>(OuterIndex - 1) >= ExportCount)
                    break;
                Current = static_cast<UINT>(OuterIndex - 1);
            }

            for (UINT const Visited : Chain)
                TreeOf[Visited] = Result;
        }

        struct FTreeStats { int Exports = 0; int Unforced = 0; };
        std::unordered_map<int, FTreeStats> Stats{};
        for (UINT Slot = 0; Slot < ExportCount; ++Slot)
        {
            if (TreeOf[Slot] < 0)
                continue;

            FTreeStats& Tree = Stats[TreeOf[Slot]];
            ++Tree.Exports;
            FObjectExport& Export = Context->ExportMap(Slot);
            if ((Export.ExportFlags & k_exportFlagForcedExport) != 0)
            {
                Export.ExportFlags &= ~k_exportFlagForcedExport;
                ++Tree.Unforced;
            }
        }

        std::unordered_set<std::wstring> FoundRoots{};
        for (auto const& [TopSlot, Tree] : Stats)
        {
            std::wstring const TopName = ToStd(Context->ExportMap(static_cast<UINT>(TopSlot)).ObjectName.ToString());
            FoundRoots.insert(ToLower(TopName));
            MILAN_INFOW(ELOG_LinkerPatcher, L"patch package %s: un-forced tree %s (%d export(s), EF_ForcedExport cleared on %d)",
                PackageName.c_str(), TopName.c_str(), Tree.Exports, Tree.Unforced);
        }

        for (auto const& [RootLower, Root] : Roots->second)
        {
            if (!FoundRoots.contains(RootLower))
            {
                MILAN_WARNW(ELOG_LinkerPatcher, L"patch package %s has no top-level export named %s; every redirect "
                    L"into it under %s will keep its original", PackageName.c_str(), Root.c_str(), Root.c_str());
            }
        }
    }

    /** Loads a patch package at most once per process. */
    static void EnsurePatchPackageLoaded(std::wstring const& PackageName)
    {
        {
            std::lock_guard<std::mutex> const Guard{ g_patchLoadMutex };
            if (!g_attemptedPatchLoads.insert(ToLower(PackageName)).second)
                return;
        }

        // The original, not the hook: the config is already loaded and the gate long open.
        ++t_patchLoadDepth;
        UPackage* const Package = (*LoadPackage_orig)(nullptr, PackageName.c_str(), LOAD_None);
        --t_patchLoadDepth;

        if (Package == nullptr)
        {
            MILAN_WARNW(ELOG_LinkerPatcher, L"patch package %s failed to load; its redirects fall back to the originals",
                PackageName.c_str());
            return;
        }

        MILAN_INFOW(ELOG_LinkerPatcher, L"loaded patch package %s", PackageName.c_str());
    }


    void Initialize(::LESDK::Initializer& Context)
    {
        gp_config = Context.ResolveTyped<void*>(GLOBAL_GCONFIG_RVA);
        gp_objBeginLoadCount = Context.ResolveTyped<int>(GLOBAL_GOBJBEGINLOADCOUNT_RVA);
        CreateConfigFile = Context.ResolveTyped<t_CreateConfigFile>(GLOBAL_FCONFIGCACHEINI_CREATECONFIGFILE_RVA);
        GetArray = Context.ResolveTyped<t_GetArray>(GLOBAL_FCONFIGCACHEINI_GETARRAY_RVA);
        StaticFindObject = Context.ResolveTyped<t_StaticFindObject>(GLOBAL_UOBJECT_STATICFINDOBJECT_RVA);

        LOG_SDK_GLOBAL(gp_config, *gp_config);
        LOG_SDK_GLOBAL(gp_objBeginLoadCount, gp_objBeginLoadCount);
        LOG_SDK_GLOBAL(CreateConfigFile, CreateConfigFile);
        LOG_SDK_GLOBAL(GetArray, GetArray);
        LOG_SDK_GLOBAL(StaticFindObject, StaticFindObject);
    }

    void EnsureConfigLoaded()
    {
        if (gb_redirectsReady.load(std::memory_order_acquire))
            return;

        std::lock_guard<std::mutex> const Guard{ g_configMutex };
        if (gb_configLoadAttempted)
            return;

        void* const Config = gp_config != nullptr ? *gp_config : nullptr;
        if (Config == nullptr || CreateConfigFile == nullptr || GetArray == nullptr)
        {
            // Not latched: GConfig always exists by the first package load, so try again next time.
            MILAN_WARNW(ELOG_LinkerPatcher, L"GConfig is not initialized yet, deferring linker patch config");
            return;
        }
        gb_configLoadAttempted = true;

        if (RegisterConfigLayer(Config))
            BuildRedirectTable(Config);

        // Published unconditionally: on failure both tables are empty, which readers treat as a no-op.
        gb_redirectsReady.store(true, std::memory_order_release);
    }

    bool ResolvePatchPackageFile(wchar_t const* const InName, FString* const OutFileName)
    {
        if (InName == nullptr || OutFileName == nullptr || !gb_redirectsReady.load(std::memory_order_acquire))
            return false;

        auto const Found = g_patchPackages.find(ToLower(InName));
        if (Found == g_patchPackages.end())
            return false;

        std::wstring const Path = std::wstring(k_patchFolder) + Found->second + L".pcc";
        *OutFileName = Path.c_str();

        MILAN_DEBUGW(ELOG_LinkerPatcher, L"resolved patch package %s -> %s (not in PCConsoleTOC)", InName, Path.c_str());
        return true;
    }

    UObject* CreateExport(ULinkerLoad* const Context, int const Index)
    {
        // No EnsureConfigLoaded() here: an export can't exist before a package load, so
        // LoadPackage_hook has always run it first. Keeps this path lock-free and non-reentrant.
        if (!gb_redirectsReady.load(std::memory_order_acquire) || g_redirects.empty()
            || StaticFindObject == nullptr || t_patchLoadDepth > 0)
        {
            return (*ULinkerLoad_CreateExport_orig)(Context, Index);
        }

        FObjectExport& Export = Context->ExportMap(static_cast<UINT>(Index));

        // Already created: CreateExport returns the cached object, nothing to redirect.
        if (Export.Object != nullptr)
            return (*ULinkerLoad_CreateExport_orig)(Context, Index);

        // Pre-filter on the leaf alone: almost every export fails here, before any path walk.
        FString const LeafString = Export.ObjectName.ToString();
        wchar_t const* const LeafChars = LeafString.Chars() != nullptr ? LeafString.Chars() : L"";
        t_leafScratch.assign(LeafChars);
        std::transform(t_leafScratch.begin(), t_leafScratch.end(), t_leafScratch.begin(),
            [](wchar_t const C) { return static_cast<wchar_t>(std::towlower(C)); });
        if (!g_leafNames.contains(t_leafScratch))
            return (*ULinkerLoad_CreateExport_orig)(Context, Index);

        std::wstring Key{}, AltKey{};
        if (!BuildMemoryPathKeys(Context, Export, Key, AltKey))
        {
            MILAN_WARNW(ELOG_LinkerPatcher, L"could not build the memory path of export %d (%s) in %s, not redirecting",
                Index, LeafChars, *Context->Filename);
            return (*ULinkerLoad_CreateExport_orig)(Context, Index);
        }

        auto Found = g_redirects.find(Key);
        if (Found == g_redirects.end() && !AltKey.empty())
        {
            Found = g_redirects.find(AltKey);
            if (Found != g_redirects.end())
                Key = AltKey;
        }
        if (Found == g_redirects.end())
            return (*ULinkerLoad_CreateExport_orig)(Context, Index);

        FRedirectTarget const& Target = Found->second;

        // Create the original FIRST, then load the patch package. A patch object is an instance of
        // the same class as what it replaces, and that class often lives in the package being
        // patched. Loading the patch package first would drag that package in mid-load, its
        // exports would stream past this hook before the patch object exists, and the redirect
        // would silently miss. CreateExport resolves and preloads the export's class before
        // constructing anything (Ghidra @ RVA 0xFBDD0), so once it returns every class the patch
        // package imports is resident. The original is left for the GC.
        UObject* const Original = (*ULinkerLoad_CreateExport_orig)(Context, Index);
        if (Original == nullptr)
        {
            // Failed or deliberately filtered (the linker's load flags exclude it). Either way
            // there's no class to check against, and redirecting would bypass the filter.
            MILAN_WARNW(ELOG_LinkerPatcher, L"original %s was not created in %s, not redirecting", Key.c_str(), *Context->Filename);
            return Original;
        }

        EnsurePatchPackageLoaded(Target.PatchPackage);

        // The patch object sits at the original's full memory path under the patch package: its
        // tree was un-forced when the patch linker was created (UnforceTargetedTrees), so it loaded
        // as a group of the patch package rather than over the real objects. Found by name with a
        // null (wildcard) class, then checked: a patch object of another class would be handed to
        // code that reads it as the original's class.
        std::wstring const FindPath = Target.PatchPackage + L"." + Target.OriginalObject;
        UObject* const Patched = (*StaticFindObject)(nullptr, nullptr, FindPath.c_str(), 0);
        if (Patched == nullptr)
        {
            MILAN_WARNW(ELOG_LinkerPatcher, L"redirect target %s for %s not found after loading its package, keeping the original",
                FindPath.c_str(), Key.c_str());
            return Original;
        }

        if (Patched->Class != Original->Class)
        {
            std::wstring const OriginalClass = Original->Class != nullptr ? ToStd(Original->Class->GetName()) : L"(null)";
            std::wstring const PatchedClass = Patched->Class != nullptr ? ToStd(Patched->Class->GetName()) : L"(null)";
            MILAN_WARNW(ELOG_LinkerPatcher, L"redirect target %s is a %s but %s is a %s, keeping the original",
                FindPath.c_str(), PatchedClass.c_str(), Key.c_str(), OriginalClass.c_str());
            return Original;
        }

        // Root the patch object. The export slot below is a raw pointer the GC doesn't trace, so if
        // nothing else happened to reference the patch object at a collection, it'd be destroyed
        // while the slot still pointed at it, and the next lookup of this export would hand out a
        // dangling pointer. Its own destroy path can't clear the slot either: SetLinker only clears
        // the *patch* package's slot, via the object's own Linker/LinkerIndex. Rooting also keeps the
        // patch package alive, which EnsurePatchPackageLoaded's load-once latch already assumes.
        // The cost: it is never collected, nor is anything it references (see LinkerPatcher.hpp).
        Patched->ObjectFlags |= k_objectFlagRootSet;

        // The discarded original is NOT detached from its linker here. It is queued instead, and the
        // EndLoad hook detaches it once loading has finished with it (DetachDiscardedOriginals).
        //
        // Why it must be detached at all: CreateExport has just called SetLinker(Linker, Index) on the
        // original, so it points at this export slot, which is about to hold the patch object. Two
        // things go wrong if it stays that way. (1) The GC destroys the original and
        // UObject::BeginDestroy (RVA 0x121BC0) calls the virtual SetLinker(NULL, INDEX_NONE) (RVA
        // 0x130280), which clears `Linker->ExportMap[LinkerIndex].Object` without checking it is still
        // this object: the redirect below is wiped. (2) Worse, nothing detaches it when the linker
        // goes away. ULinkerLoad::DetachExport (RVA 0xFE880) finds the patch object in the slot, sees
        // its outermost package is cooked and that its Linker is another linker, and only nulls the
        // slot; it never calls SetLinker on the original. The seekfree startup loaders are reset at
        // engine init, freeing their ExportMap, and the first CollectGarbage then runs BeginDestroy
        // on the original, whose SetLinker writes `Linker->ExportMap.Data[idx * 0x64 + 0x30] = 0`
        // through the freed (NULL) Data pointer: an access violation at a small address.
        //
        // Why not here: CreateExport has also just added the original to GObjLoaded with RF_NeedLoad,
        // and UObject::EndLoad (RVA 0x123BF0) preloads every such object through `Obj->Linker` with
        // no null check, so a detached original crashes the load. It can only be detached once
        // EndLoad has preloaded and post-loaded it. (SetLinker itself is never used for this: it
        // would null the export slot, which now holds the patch object.)
        {
            std::lock_guard<std::mutex> const Guard{ g_discardedMutex };
            g_discardedOriginals.push_back(FDiscardedOriginal{ Original, Original->ObjectInternalInteger,
                Original->Linker, Original->LinkerIndex });
            g_discardedCount.store(g_discardedOriginals.size(), std::memory_order_release);
        }

        // Overwrite the export's cache slot so later references resolve straight to the patch.
        Export.Object = Patched;
        MILAN_INFOW(ELOG_LinkerPatcher, L"redirected %s -> %s (in %s)", Key.c_str(), FindPath.c_str(), *Context->Filename);
        return Patched;
    }

    INT FinalizeCreation(ULinkerLoad* const Context)
    {
        UnforceTargetedTrees(Context);
        return (*ULinkerLoad_FinalizeCreation_orig)(Context);
    }

    /**
     * Cuts every discarded original that loading has finished with off from its linker, so the
     * linker's later reset or the original's own destruction can't reach through it. Writes the
     * fields directly instead of calling UObject::SetLinker: SetLinker(NULL, -1) clears the
     * linker's export slot, and that slot now holds the patch object.
     *
     * Only runs once the outermost EndLoad is done (GObjBeginLoadCount == 0), which is also when
     * its tail has finished with the objects it queued. An original still carrying RF_NeedLoad or
     * RF_NeedPostLoad is not through yet - EndLoad reads its Linker to preload it - and stays
     * queued for a later EndLoad.
     *
     * An original can die before it is processed (a collection between its creation and the end of
     * the load). Its pointer is therefore never dereferenced until the engine's own validity test
     * (UObject::IsValid, RVA 0x1287C0: GObjObjects[ObjectInternalInteger] == Object) passes, and
     * the linker/index it had when queued are re-checked, so a recycled address can't be confused
     * with it. Anything that fails either check is dropped: it is gone or already detached.
     */
    static void DetachDiscardedOriginals()
    {
        if (g_discardedCount.load(std::memory_order_acquire) == 0)
            return;
        if (gp_objBeginLoadCount == nullptr || *gp_objBeginLoadCount != 0 || UObject::GObjObjects == nullptr)
            return;

        size_t Detached = 0, Dropped = 0, Remaining = 0;
        {
            std::lock_guard<std::mutex> const Guard{ g_discardedMutex };

            UObject** const Objects = UObject::GObjObjects->GetData();
            INT const ObjectCount = static_cast<INT>(UObject::GObjObjects->Count());

            auto const Finished = [&](FDiscardedOriginal const& Entry) -> bool
                {
                    if (Entry.InternalIndex < 0 || Entry.InternalIndex >= ObjectCount
                        || Objects[Entry.InternalIndex] != Entry.Object)
                    {
                        ++Dropped;
                        return true;
                    }

                    UObject* const Original = Entry.Object;
                    if ((Original->ObjectFlags & (k_objectFlagNeedLoad | k_objectFlagNeedPostLoad)) != 0)
                        return false;

                    if (Original->Linker == Entry.Linker && Original->LinkerIndex == Entry.LinkerIndex)
                    {
                        Original->Linker = nullptr;
                        Original->LinkerIndex = -1;     // INDEX_NONE, as the engine's own qword store writes it.
                        ++Detached;
                    }
                    else
                    {
                        ++Dropped;
                    }
                    return true;
                };

            g_discardedOriginals.erase(
                std::remove_if(g_discardedOriginals.begin(), g_discardedOriginals.end(), Finished),
                g_discardedOriginals.end());
            Remaining = g_discardedOriginals.size();
            g_discardedCount.store(Remaining, std::memory_order_release);
        }

        if (Detached != 0 || Dropped != 0)
        {
            MILAN_DEBUGW(ELOG_LinkerPatcher, L"detached %llu discarded original(s) from their linkers (%llu already gone or detached, %llu still loading)",
                (unsigned long long)Detached, (unsigned long long)Dropped, (unsigned long long)Remaining);
        }
    }

    void EndLoad()
    {
        (*UObject_EndLoad_orig)();
        DetachDiscardedOriginals();
    }
}
