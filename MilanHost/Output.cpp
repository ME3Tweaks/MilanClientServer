// TODO(d00t): Remove this.
#define SDK_TARGET_LE3

#include <LESDK/Init.hpp>
#include <LESDK/Headers.hpp>

#include "Output.hpp"


namespace Milan::Output
{
    void Initialize(::LESDK::Initializer& Context)
    {
        (void)Context;
        ::LESDK::InitializeConsole();
    }

    void Terminate()
    {
        ::LESDK::TerminateConsole();
    }
}
