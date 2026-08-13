/**
 * @file        MilanHost/Shell/Entry.hpp
 * @header      Local declarations for @c Shell/Entry.cpp
 */

#pragma once

#include <LESDK/Init.hpp>
#include <SPI.h>


// ! Initialization and cleanup.
// ========================================

extern ::LESDK::Initializer* gp_sdkContext;

// This dll's HMODULE, used for loading resources.
extern HMODULE g_hModule;

void InitSDKBasics(::LESDK::Initializer& Context);
void ReportConfigError();
void SetupImguiInterfaces();
void RegisterNonshipCommands();
void RegisterVectoredExceptions();
void SpawnLocalClients();
