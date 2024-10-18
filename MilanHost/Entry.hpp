/**
 * @file        MilanHost/Entry.hpp
 * @header      Local declarations for @c Entry.cpp
 */

#pragma once

#include <LESDK/Init.hpp>
#include <SPI.h>


// ! Initialization and cleanup.
// ========================================

::LESDK::Initializer* gp_sdkContext = nullptr;

void InitAllocators(::LESDK::Initializer& Context);
void LoadInitialConfig();
void RegisterNonshipCommands();
void RegisterVectoredExceptions();
void SpawnLocalClients();
