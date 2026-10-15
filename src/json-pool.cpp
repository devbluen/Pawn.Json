/*
 *  Pawn.Json - JSON natives for SA-MP and open.mp
 */

#include "json-pool.hpp"
#include <limits>

namespace
{
std::unordered_map<cell, JsonPool::Node> nodes;
cell nextId = 0;

cell nextFreeId()
{
	// Ids are never reused while alive; negative ids are reserved for errors.
	for (;;)
	{
		const cell id = nextId;
		nextId = (nextId == std::numeric_limits<cell>::max()) ? 0 : nextId + 1;
		if (nodes.find(id) == nodes.end())
		{
			return id;
		}
	}
}
}

namespace JsonPool
{
cell alloc(AMX* owner, Json&& value)
{
	const cell id = nextFreeId();
	Node node;
	node.value = std::make_unique<Json>(std::move(value));
	node.owner = owner;
	nodes.emplace(id, std::move(node));
	return id;
}

Node* find(cell id)
{
	const auto it = nodes.find(id);
	return it == nodes.end() ? nullptr : &it->second;
}

Json* pointer(cell id)
{
	Node* node = find(id);
	return node ? node->value.get() : nullptr;
}

bool take(cell id, bool consume, Json& out)
{
	const auto it = nodes.find(id);
	if (it == nodes.end())
	{
		return false;
	}
	if (consume && it->second.gc)
	{
		out = std::move(*it->second.value);
		nodes.erase(it);
	}
	else
	{
		out = *it->second.value;
	}
	return true;
}

bool erase(cell id)
{
	return nodes.erase(id) != 0;
}

void eraseOwnedBy(AMX* owner)
{
	for (auto it = nodes.begin(); it != nodes.end();)
	{
		if (it->second.owner == owner)
		{
			it = nodes.erase(it);
		}
		else
		{
			++it;
		}
	}
}

void clear()
{
	nodes.clear();
	nextId = 0;
}

std::size_t size()
{
	return nodes.size();
}
}
