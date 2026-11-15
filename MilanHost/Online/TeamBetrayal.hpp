#pragma once

#include <LESDK/Headers.hpp>

/**
 * @brief   Punishes the changeteam command in public lobbies
 *
 * A player who uses changeteam in a public lobby is killed outright, without the revivable Downed
 * state, and every machine shows "<name> attempted to betray the team" in the event ticker. The
 * kill ticker the death would otherwise add ("<name> bled out") is dropped. Private and
 * friends-only lobbies are not affected
 */
namespace TeamBetrayal
{
    /** Handles a script function about to run; returns false when the original must be skipped */
    bool OnScriptCall(UObject* Context, FFrame* Stack);

    /** Handles a script event being dispatched; returns true when the event was consumed */
    bool OnProcessEvent(UObject* Context, UFunction* Function, void* Parms);
}
