/**
 * @file        MilanHost/Engine/ConsoleAutoComplete.cpp
 * @brief       See ConsoleAutoComplete.hpp.
 *
 * @remarks     Comparing LE3's shipped Engine.Console UnrealScript (decompiled straight from the
 *              retail Engine.pcc) against Epic's original UE3 source shows the entire Tab-complete
 *              UI - key handling, match rendering, the ManualAutoCompleteList defaults - survived
 *              intact. Only Console::BuildRuntimeAutoCompleteList, the native function that fills
 *              AutoCompleteList and the AutoCompleteTree trie, was compiled out (its body is empty
 *              in the retail binary, guarded there by Epic's own FINAL_RELEASE_DEBUGCONSOLE check).
 *              Nothing else calls it either, so this hijacks it with a real implementation; the
 *              first build is triggered from the (already binary-hooked, not UFunction-hijacked)
 *              UGameEngine::LoadMap hook in Hooks.cpp, which also refreshes it on later map loads.
 *
 *              Deliberately does NOT hijack Console.Initialized() to trigger the first build, even
 *              though that reads as the more "natural" hook point. Console.Initialized() is a plain
 *              script function with no FUNC_Native flag, and HijackUtils::InstallFunction
 *              unconditionally sets FUNC_Native on whatever it hijacks. Doing that while the engine
 *              is still linking/binding the Console class (i.e. from CGlobals::PostInitialize, the
 *              earliest point the class exists at all) reliably hung the game inside the original
 *              call the very first time Initialized() ran - see git history for the repro. Hijacking
 *              BuildRuntimeAutoCompleteList is safe by contrast because it is already FUNC_Native in
 *              the retail engine, so setting that flag again is a no-op.
 */

#include <cstdio>
#include <cwctype>
#include <utility>

#include "Base/Logger.hpp"
#include "Engine/ConsoleAutoComplete.hpp"
#include "Engine/ExtObject.hpp"
#include "Engine/Hooks.hpp"
#include "Engine/Intercept.hpp"


#ifndef ENABLE_AUTOCOMPLETE_LOGGING
#define ENABLE_AUTOCOMPLETE_LOGGING 0
#endif


namespace ConsoleAutoComplete
{
    DECLARE_HIJACK(UConsole, BuildRuntimeAutoCompleteList);

    static constexpr DWORD k_funcExec = 0x200;


    static FString ToLowerCopy(FString const& In)
    {
        FString Out{};
        Out.Reserve(In.Length());
        for (UINT Index = 0; Index < In.Length(); ++Index)
            Out.Append(static_cast<wchar_t>(std::towlower(In.Chars()[Index])));
        return Out;
    }

    /** Recursively frees the C++-side trie nodes we allocate ourselves; mirrors ~FAutoCompleteNode from the original engine. */
    static void FreeNodeChildren(FAutoCompleteNode& Node)
    {
        for (void* const Child : Node.ChildNodes)
        {
            auto* const ChildNode = static_cast<FAutoCompleteNode*>(Child);
            FreeNodeChildren(*ChildNode);
            delete ChildNode;
        }
        Node.ChildNodes.Clear();
        Node.ChildNodes.Shrink();
    }

    static void InsertIntoTrie(FAutoCompleteNode& Root, FString const& LowerCommand, int const ListIndex)
    {
        FAutoCompleteNode* Node = &Root;

        for (UINT Depth = 0; Depth < LowerCommand.Length(); ++Depth)
        {
            int const Char = static_cast<int>(LowerCommand.Chars()[Depth]);

            FAutoCompleteNode* Found = nullptr;
            for (void* const Child : Node->ChildNodes)
            {
                auto* const ChildNode = static_cast<FAutoCompleteNode*>(Child);
                if (ChildNode->IndexChar == Char)
                {
                    Found = ChildNode;
                    Found->AutoCompleteListIndices.Add(ListIndex);
                    break;
                }
            }

            if (Found == nullptr)
            {
                Found = new FAutoCompleteNode{};
                Found->IndexChar = Char;
                Found->AutoCompleteListIndices.Add(ListIndex);
                Node->ChildNodes.Add(static_cast<void*>(Found));
            }

            Node = Found;
        }
    }

    static void LogAndFlush(wchar_t const* const Message)
    {
        MILAN_UNUSED(Message);

    #if defined(ENABLE_AUTOCOMPLETE_LOGGING) && ENABLE_AUTOCOMPLETE_LOGGING

        MILAN_WARNW(ELOG_EngineHooks, L"[ConsoleAutoComplete] %s", Message);
        if (gp_mainLogger != nullptr) gp_mainLogger->Flush();

    #endif
    }

    static void RebuildAutoCompleteList(UConsole* const Console, bool const bForce)
    {
        MILAN_CHECKW(Console != nullptr, L"");
        LogAndFlush(L"RebuildAutoCompleteList: enter");

        if (!bForce)
        {
            // Matches the original engine: an unforced call just invalidates the cache for later.
            Console->bIsRuntimeAutoCompleteUpToDate = 0;
            LogAndFlush(L"RebuildAutoCompleteList: unforced, invalidated only");
            return;
        }

        LogAndFlush(L"RebuildAutoCompleteList: freeing existing trie");
        FreeNodeChildren(Console->AutoCompleteTree);
        Console->AutoCompleteTree.AutoCompleteListIndices.Clear();
        Console->AutoCompleteTree.AutoCompleteListIndices.Shrink();

        Console->AutoCompleteList.Clear();
        Console->AutoCompleteList.Shrink();

        // 1. Manual list first (BaseInput.ini's ManualAutoCompleteList - ~60 built-in commands in LE3).
        LogAndFlush(L"RebuildAutoCompleteList: copying manual list");
        for (FAutoCompleteCommand const& Manual : Console->ManualAutoCompleteList)
            Console->AutoCompleteList.Add(Manual);

        // 2. Every top-level exec UFunction (skip overrides, which share their base declaration's name).
        {
            wchar_t Buf[128]{};
            ::swprintf_s(Buf, L"RebuildAutoCompleteList: scanning exec functions, GObjObjects.Count=%u", UObject::GObjObjects->Count());
            LogAndFlush(Buf);
        }

        int ExecCount = 0;
        UINT Visited = 0;
        UINT const ObjectCount = UObject::GObjObjects->Count();
        // Defensive cap: this loop should visit at most ObjectCount objects. If the iterator
        // somehow never terminates (bad GObjObjects resolution, corrupted class chain, etc.) we
        // bail out loudly instead of hanging forever.
        UINT const VisitCap = ObjectCount + 1000u;

        for (TypedObjectIterator<UFunction, false, false> Iter{}; Iter; ++Iter)
        {
            ++Visited;
            if (Visited % 5000u == 0u)
            {
                wchar_t Buf[128]{};
                ::swprintf_s(Buf, L"RebuildAutoCompleteList: still scanning, visited=%u, execFound=%d", Visited, ExecCount);
                LogAndFlush(Buf);
            }
            if (Visited > VisitCap)
            {
                wchar_t Buf[160]{};
                ::swprintf_s(Buf, L"RebuildAutoCompleteList: ABORTING exec scan, visited %u > cap %u (GObjObjects.Count=%u)", Visited, VisitCap, ObjectCount);
                LogAndFlush(Buf);
                break;
            }

            UFunction* const Func = *Iter;
            if ((Func->FunctionFlags & k_funcExec) == 0) continue;
            if (Func->SuperField != nullptr) continue;

            FString FuncName{};
            Func->Name.AppendToString(FuncName, SFXName::k_formatBasic);

            FAutoCompleteCommand Entry{};
            Entry.Command = FuncName;
            Entry.Desc = std::move(FuncName);
            Console->AutoCompleteList.Add(std::move(Entry));
            ++ExecCount;
        }

        {
            wchar_t Buf[128]{};
            ::swprintf_s(Buf, L"RebuildAutoCompleteList: exec scan done, visited=%u, execFound=%d", Visited, ExecCount);
            LogAndFlush(Buf);
        }

        // 3. BioWare's config-driven map list (MapPathsForAutoComplete), replacing Epic's filesystem scan.
        LogAndFlush(L"RebuildAutoCompleteList: adding map paths");
        for (FString const& MapPath : Console->MapPathsForAutoComplete)
        {
            FString OpenCommand{ L"open " };
            OpenCommand.Append(MapPath);

            FAutoCompleteCommand OpenEntry{};
            OpenEntry.Command = OpenCommand;
            OpenEntry.Desc = std::move(OpenCommand);
            Console->AutoCompleteList.Add(std::move(OpenEntry));

            FString StartCommand{ L"start " };
            StartCommand.Append(MapPath);

            FAutoCompleteCommand StartEntry{};
            StartEntry.Command = StartCommand;
            StartEntry.Desc = std::move(StartCommand);
            Console->AutoCompleteList.Add(std::move(StartEntry));
        }

        // 4. Build the per-character trie UpdateCompleteIndices (already implemented natively) walks.
        LogAndFlush(L"RebuildAutoCompleteList: building trie");
        for (UINT Index = 0; Index < Console->AutoCompleteList.Count(); ++Index)
        {
            FString const Lower = ToLowerCopy(Console->AutoCompleteList(Index).Command);
            InsertIntoTrie(Console->AutoCompleteTree, Lower, static_cast<int>(Index));
        }

        Console->bIsRuntimeAutoCompleteUpToDate = 1;

        MILAN_TRACEW(ELOG_EngineHooks, L"[ConsoleAutoComplete] rebuilt: %d manual, %d exec, %d map, %d total",
            Console->ManualAutoCompleteList.Count(), ExecCount, Console->MapPathsForAutoComplete.Count(), Console->AutoCompleteList.Count());
        if (gp_mainLogger != nullptr) gp_mainLogger->Flush();
    }


    // ! Hijacked function bodies.
    // ========================================

    void BuildRuntimeAutoCompleteList_Hook(UConsole* const Context, FFrame* const Stack, void* const Result)
    {
        MILAN_UNUSED(Result);

        NATIVE_GET_INT_OPTIONAL(bForce, 0);
        NATIVE_FINISH();

        RebuildAutoCompleteList(Context, bForce != 0);
    }

    // ! Public interface.
    // ========================================

    bool TryInstall()
    {
        static bool s_installed = false;
        if (s_installed) return true;

        UClass* const ConsoleClass = HijackUtils::FindClass(SFXName(L"Console", 0));
        if (ConsoleClass == nullptr) return false;

        UFunction* const Target = HijackUtils::FindClassMethod(ConsoleClass, SFXName(L"BuildRuntimeAutoCompleteList", 0));
        if (Target == nullptr) return false;

        HijackUtils::InstallFunction(Target, BuildRuntimeAutoCompleteList_Hook,
            reinterpret_cast<void**>(&BuildRuntimeAutoCompleteList_Original));
        s_installed = true;

        MILAN_INFOW(ELOG_EngineHooks, L"installed Engine.Console auto-complete hijack after package load");
        return true;
    }

    void RebuildForCurrentLevel()
    {
        if (*GEngine != nullptr && (*GEngine)->GameViewport != nullptr && (*GEngine)->GameViewport->ViewportConsole != nullptr)
            (*GEngine)->GameViewport->ViewportConsole->BuildRuntimeAutoCompleteList(1);
    }
}
