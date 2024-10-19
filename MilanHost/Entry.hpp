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

void InitSDKBasics(::LESDK::Initializer& Context);
void ReportConfigError();
void SetupImguiInterfaces();
void RegisterNonshipCommands();
void RegisterVectoredExceptions();
void SpawnLocalClients();
