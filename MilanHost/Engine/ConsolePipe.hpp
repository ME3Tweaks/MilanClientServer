#pragma once

#include "Base/Base.hpp"

/**
 * Console commands from outside the game, for debugging (non-final builds). A local named pipe,
 * \\.\pipe\MilanHost-<process id>, takes newline-separated console commands as UTF-8 and answers with what each one
 * returned. Commands run on the game thread after the next engine tick, so a client waits while the game is loading.
 */
namespace ConsolePipe
{
    /** Starts the pipe server thread */
    void Start();

    /** Runs the commands clients have sent. Call on the game thread. */
    void Pump();
}
