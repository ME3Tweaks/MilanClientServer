#include <intrin.h>

#include "Base.hpp"
#include "NetProf.hpp"
#include "Systems/Output.hpp"


// ! Internal handlers called by asm code.
// ========================================

void MlnUGameEngineTick1_Handler()
{
    //MILAN_DEBUGW(L"NETPROF (UGameEngine::Tick): %d", 1);
}

void MlnUGameEngineLoadMap1_Handler(FURL* const Url)
{
    MILAN_DEBUGW(L"NETPROF (UGameEngine::LoadMap): proto = %s, host = %s, map = %s, portal = %s, port = %d, valid = %d",
        *Url->Protocol, *Url->Host, *Url->Map, *Url->Portal, Url->Port, Url->Valid);
    for (auto const& Op : Url->Op) MILAN_DEBUGW(L"  > Op = %s", *Op);
}
