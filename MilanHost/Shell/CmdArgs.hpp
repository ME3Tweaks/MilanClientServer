/**
 * @file        MilanHost/Shell/CmdArgs.hpp
 * @header      Utilities for reading command line arguments.
 */

#pragma once

namespace CmdArgs
{

/** Retrieves command line arguments as a string. */
wchar_t const* Get() noexcept;
/** Retrieves command line arguments with executable name as a string. */
wchar_t const* GetWithExec() noexcept;

/** Checks if command line arguments contain a string (case-sensitively). */
bool Contain(wchar_t const* Needle) noexcept;

} // namespace CmdArgs
