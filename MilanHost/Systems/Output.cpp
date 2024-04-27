#include <LESDK/Init.hpp>
#include <LESDK/Headers.hpp>

#include "Systems/Output.hpp"


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
