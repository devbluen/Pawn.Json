/*
 *  Pawn.Json - JSON natives for SA-MP and open.mp
 */

#pragma once

#include "amx.hpp"

namespace Natives
{
// Null-terminated list of every native, ready for amx_Register/IPawnScript::Register.
const AMX_NATIVE_INFO* list(int& count) noexcept;
}
