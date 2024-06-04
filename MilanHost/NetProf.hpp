/**
 * @file        MilanHost/NetProf.hpp
 * @header      Embedded logic for our Network Profiler.
 */

#pragma once

#include <LESDK/Headers.hpp>


// ! Declarations for assembly stubs.
// ========================================

extern "C" void MlnUGameEngineTick1();
extern "C" void MlnUGameEngineLoadMap1();


// ! Internal handlers called by asm code.
// ========================================

extern "C" void MlnUGameEngineTick1_Handler();
extern "C" void MlnUGameEngineLoadMap1_Handler(FURL* Url);
