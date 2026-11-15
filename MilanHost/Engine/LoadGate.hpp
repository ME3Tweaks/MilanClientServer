/**
 * @file        MilanHost/Engine/LoadGate.hpp
 * @brief       Holds the game at its first package load until MilanHost has finished attaching.
 *
 * @remarks     MilanHost is a post-DRM ASI: LEBinkProxy calls SpiOnAttach on its own worker
 *              thread once DRM has unwrapped the image, while the game's main thread carries on
 *              into engine startup in parallel. Nothing orders the two, so any hook installed
 *              during attach can go live after the code it targets has already run for the
 *              first time - most importantly, after packages have started loading.
 *
 *              The gate closes that race. It hooks LoadPackage as the very first thing attach
 *              does and parks every non-attach thread that enters it until attach calls Open().
 *              The first package the engine loads is Core.pcc, and nothing upstream of that
 *              depends on MilanHost, so stalling there costs nothing but the attach time.
 *
 *              That splits MilanHost's startup work into three phases:
 *
 *              1. Immediate - before LoadGate::Install(), at the top of SpiOnAttach. Only for
 *                 code the game can reach *before* its first package load (byte patches to the
 *                 startup path such as the minimum-resolution check, Win32 hooks like
 *                 CreateWindowExW for the splash window). Must be cheap, must not log (the
 *                 logger needs GMalloc and does not exist yet), and should use fixed RVAs over
 *                 pattern scans, because this phase is a race against the game thread.
 *
 *              2. Gated - everything between Install() and Open(), i.e. normal component
 *                 initialization. Anything only reachable from package loading or later
 *                 (UObject/linker/script/network hooks) belongs here, and is guaranteed live
 *                 before the first package is loaded - provided the gate itself won the race,
 *                 which Open() reports on.
 *
 *              3. Lazy - installed on demand once the content it needs exists
 *                 (e.g. ConsoleAutoComplete, which needs Engine.Console to be loaded).
 *
 *              The rule of thumb for classifying a new hook: if its target can execute before
 *              Core.pcc loads, it is Immediate; otherwise it is Gated.
 *
 *              The gate fails open: a thread is never held longer than k_maxStallMs, so a bug
 *              in attach degrades into the old race instead of a hung game. Pass -noloadgate
 *              to disable it entirely.
 */

#pragma once

#include <LESDK/Init.hpp>


namespace LoadGate
{
    /**
     * Installs the LoadPackage hook and closes the gate. Call first thing in SpiOnAttach, from
     * the attach thread - that thread is exempt from the gate. Does not log.
     */
    void Install(::LESDK::Initializer& Context);

    /** Releases every held thread and logs a report of what the gate saw. Call last in attach. */
    void Open();

    /** True once Open() has run (or if the gate is disabled). */
    bool IsOpen() noexcept;

    /**
     * Called from LoadPackage_hook before the original runs. Blocks the calling thread until the
     * gate opens, unless it is the attach thread or the gate is already open.
     */
    void Wait(wchar_t const* PackageName);
}
