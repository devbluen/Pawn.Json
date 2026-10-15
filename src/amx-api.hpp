/*
 *  Pawn.Json - JSON natives for SA-MP and open.mp
 *
 *  Thin access layer over the AMX runtime. Both SA-MP (Load data table) and
 *  open.mp (IPawnComponent::getAmxFunctions) expose the same AMX export table,
 *  so the natives only ever talk to this module and never need to know which
 *  server loaded them.
 */

#pragma once

#include "amx.hpp"
#include <cstddef>
#include <string>

namespace AmxApi
{
// `table` is the AMX export table (indices follow PLUGIN_AMX_EXPORT_*).
void setFunctionTable(void* const* table) noexcept;
bool available() noexcept;

int registerNatives(AMX* amx, const AMX_NATIVE_INFO* natives, int count) noexcept;

// Resolves an AMX address to a physical pointer and returns how many cells
// can be safely read/written from it (until the stack top). Returns nullptr on
// an invalid address.
cell* getAddr(AMX* amx, cell address, std::size_t* availableCells = nullptr) noexcept;

// Reads a Pawn string (packed or unpacked) as raw bytes. Cells holding values
// above 0xFF (Unicode code points) are encoded as UTF-8. The result is not
// re-encoded; see Encoding::fromPawn for that.
bool readRawString(AMX* amx, cell address, std::string& out) noexcept;

// Writes raw bytes into an unpacked Pawn string of `size` cells (including
// the terminator). Each byte becomes one cell (0..255). Returns the number of
// characters written, or -1 on an invalid address / size.
int writeRawString(AMX* amx, cell address, const std::string& bytes, cell size) noexcept;

bool writeCell(AMX* amx, cell address, cell value) noexcept;
}
