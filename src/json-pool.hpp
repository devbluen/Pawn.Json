/*
 *  Pawn.Json - JSON natives for SA-MP and open.mp
 *
 *  Node pool: every builder returns a numeric `Node:` handle, handles passed
 *  as values into another builder are consumed (moved into the parent) unless
 *  garbage collection was disabled with JsonToggleGC, and `operator~` frees
 *  what is left when a `Node:` variable leaves scope.
 *
 *  Every node also remembers the script that created it and is released when
 *  that script unloads, so a forgotten handle cannot leak across gamemode
 *  restarts.
 */

#pragma once

#include "amx.hpp"
#include <nlohmann/json.hpp>
#include <memory>
#include <unordered_map>

using Json = nlohmann::ordered_json;

namespace JsonPool
{
struct Node
{
	std::unique_ptr<Json> value;
	bool gc = true;
	AMX* owner = nullptr;
};

cell alloc(AMX* owner, Json&& value);

// Returns the node or nullptr when the id is unknown.
Node* find(cell id);
Json* pointer(cell id);

// Takes the value out of the pool. When `consume` is true and the node has GC
// enabled the node is erased and its value moved out; otherwise a copy is
// returned and the node stays. Returns false when the id is unknown.
bool take(cell id, bool consume, Json& out);

bool erase(cell id);
void eraseOwnedBy(AMX* owner);
void clear();
std::size_t size();
}
