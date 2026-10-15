/*
 *  Pawn.Json - JSON natives for SA-MP and open.mp
 */

#pragma once

#include <cstdarg>

namespace Log
{
using SampLogprintf = void (*)(const char* format, ...);
using OmpPrinter = void (*)(void* context, bool error, const char* message);

// SA-MP: the logprintf from the plugin data table.
void setSampLogger(SampLogprintf logger) noexcept;
// open.mp: a printer forwarding to ICore (keeps this module SDK-free).
void setOmpLogger(OmpPrinter printer, void* context) noexcept;
void clear() noexcept;

void info(const char* format, ...);
void error(const char* format, ...);
}
