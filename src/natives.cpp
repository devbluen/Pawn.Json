/*
 *  Pawn.Json - JSON natives for SA-MP and open.mp
 *
 *  Natives, parameters and return codes are documented in PawnJson.inc.
 *  Every native is wrapped so that no C++ exception can ever reach the AMX
 *  (an escaping exception is what crashes the server).
 */

#include "natives.hpp"
#include "amx-api.hpp"
#include "encoding.hpp"
#include "json-pool.hpp"
#include "log.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <vector>

namespace
{
// Matches the JSON_NODE enum in PawnJson.inc.
enum JsonNodeType : cell
{
	JSON_NODE_NUMBER,
	JSON_NODE_BOOLEAN,
	JSON_NODE_STRING,
	JSON_NODE_OBJECT,
	JSON_NODE_ARRAY,
	JSON_NODE_NULL,
};

constexpr cell INVALID_NODE = -1;

cell paramCount(const cell* params)
{
	return params[0] / static_cast<cell>(sizeof(cell));
}

std::string readString(AMX* amx, cell address)
{
	std::string raw;
	AmxApi::readRawString(amx, address, raw);
	return Encoding::fromPawn(raw);
}

bool writeString(AMX* amx, cell address, const std::string& utf8, cell size, std::size_t* needed = nullptr)
{
	std::string out = Encoding::toPawn(utf8);
	if (needed)
	{
		*needed = out.size() + 1;
	}
	if (size > 0 && out.size() > static_cast<std::size_t>(size - 1) && Encoding::mode() == Encoding::Mode::Utf8)
	{
		// Never leave half a multi-byte character at the end.
		out.resize(Encoding::utf8SafeCut(out, static_cast<std::size_t>(size - 1)));
	}
	return AmxApi::writeRawString(amx, address, out, size) >= 0;
}

bool readCellRef(AMX* amx, cell address, cell& out)
{
	std::size_t available = 0;
	const cell* physical = AmxApi::getAddr(amx, address, &available);
	if (!physical || available == 0)
	{
		return false;
	}
	out = *physical;
	return true;
}

// Pawn floats are 32-bit; widening them directly gives 1.340000033378601.
// Use the shortest decimal that round-trips to the same float instead.
double widenFloat(float value)
{
	if (!std::isfinite(value))
	{
		return static_cast<double>(value);
	}
	char buffer[32];
	for (int precision = 6; precision <= 9; ++precision)
	{
		std::snprintf(buffer, sizeof(buffer), "%.*g", precision, static_cast<double>(value));
		if (static_cast<float>(std::strtod(buffer, nullptr)) == value)
		{
			break;
		}
	}
	return std::strtod(buffer, nullptr);
}

bool isInteger(const Json& value)
{
	return value.is_number_integer();
}

cell asCell(const Json& value)
{
	if (value.is_number_unsigned())
	{
		return static_cast<cell>(value.get<std::uint64_t>());
	}
	return static_cast<cell>(value.get<std::int64_t>());
}

cell asFloatCell(const Json& value)
{
	float f = value.get<float>();
	return amx_ftoc(f);
}

// Looks up `key` in an object node. Returns 1 for an unknown node and 2 when
// the node is not an object or has no such key.
int lookup(AMX* amx, cell nodeId, cell keyAddress, const Json*& target)
{
	const Json* node = JsonPool::pointer(nodeId);
	if (!node)
	{
		return 1;
	}
	if (!node->is_object())
	{
		return 2;
	}
	const auto it = node->find(readString(amx, keyAddress));
	if (it == node->end())
	{
		return 2;
	}
	target = &*it;
	return 0;
}

// Collects the node ids passed by reference in a variadic argument list,
// starting at `first` and taking every `stride`-th argument.
bool collectNodeIds(AMX* amx, const cell* params, cell first, cell stride, std::vector<cell>& ids)
{
	const cell count = paramCount(params);
	for (cell i = first; i <= count; i += stride)
	{
		cell id;
		if (!readCellRef(amx, params[i], id))
		{
			return false;
		}
		ids.push_back(id);
	}
	return true;
}

// -----------------------------------------------------------------------------
// Natives
// -----------------------------------------------------------------------------

// native JsonParse(const string[], &Node:output);
cell JsonParse(AMX* amx, cell* params)
{
	const std::string input = readString(amx, params[1]);
	Json parsed;
	try
	{
		parsed = Json::parse(input);
	}
	catch (const std::exception& e)
	{
		Log::error("JsonParse failed with: %s", e.what());
		return 1;
	}

	const cell id = JsonPool::alloc(amx, std::move(parsed));
	if (!AmxApi::writeCell(amx, params[2], id))
	{
		JsonPool::erase(id);
		return 1;
	}
	return 0;
}

// native JsonStringify(Node:node, buf[], len = sizeof(buf));
cell JsonStringify(AMX* amx, cell* params)
{
	const Json* node = JsonPool::pointer(params[1]);
	const bool asciiOnly = Encoding::mode() == Encoding::Mode::Cp1252;
	const std::string text = node
		? node->dump(-1, ' ', asciiOnly, Json::error_handler_t::replace)
		: std::string("null");

	std::size_t needed = 0;
	writeString(amx, params[2], text, params[3], &needed);
	if (params[3] > 0 && needed > static_cast<std::size_t>(params[3]))
	{
		Log::error("JsonStringify: output truncated, buffer has %d cells but %u are needed",
			params[3], static_cast<unsigned>(needed));
	}
	return node ? 0 : 1;
}

// native JSON_NODE:JsonNodeType(Node:node);
cell JsonNodeType(AMX*, cell* params)
{
	const Json* node = JsonPool::pointer(params[1]);
	if (!node)
	{
		return JSON_NODE_NULL;
	}
	switch (node->type())
	{
	case Json::value_t::number_integer:
	case Json::value_t::number_unsigned:
	case Json::value_t::number_float:
		return JSON_NODE_NUMBER;
	case Json::value_t::boolean:
		return JSON_NODE_BOOLEAN;
	case Json::value_t::string:
		return JSON_NODE_STRING;
	case Json::value_t::object:
		return JSON_NODE_OBJECT;
	case Json::value_t::array:
		return JSON_NODE_ARRAY;
	default:
		return JSON_NODE_NULL;
	}
}

// native Node:JsonObject({_, Node}:...);
cell JsonObject(AMX* amx, cell* params)
{
	const cell count = paramCount(params);
	if (count % 2 != 0)
	{
		Log::error("JsonObject: key '%s' has no value", readString(amx, params[count]).c_str());
		return -1;
	}

	// Validate every value first so a bad argument consumes nothing.
	std::vector<cell> ids;
	if (!collectNodeIds(amx, params, 2, 2, ids))
	{
		return -1;
	}
	for (const cell id : ids)
	{
		if (!JsonPool::find(id))
		{
			Log::error("JsonObject: value node %d was invalid", id);
			return -2;
		}
	}

	Json object = Json::object();
	for (cell i = 1; i <= count; i += 2)
	{
		Json value;
		const cell id = ids[static_cast<std::size_t>((i - 1) / 2)];
		if (!JsonPool::take(id, true, value))
		{
			Log::error("JsonObject: value node %d was invalid (passed twice?)", id);
			return -2;
		}
		object[readString(amx, params[i])] = std::move(value);
	}
	return JsonPool::alloc(amx, std::move(object));
}

// native Node:JsonInt(value);
cell JsonInt(AMX* amx, cell* params)
{
	return JsonPool::alloc(amx, Json(static_cast<std::int64_t>(params[1])));
}

// native Node:JsonBool(bool:value);
cell JsonBool(AMX* amx, cell* params)
{
	return JsonPool::alloc(amx, Json(params[1] != 0));
}

// native Node:JsonFloat(Float:value);
cell JsonFloat(AMX* amx, cell* params)
{
	return JsonPool::alloc(amx, Json(widenFloat(amx_ctof(params[1]))));
}

// native Node:JsonString(const value[]);
cell JsonString(AMX* amx, cell* params)
{
	return JsonPool::alloc(amx, Json(readString(amx, params[1])));
}

// native Node:JsonArray(Node:...);
cell JsonArray(AMX* amx, cell* params)
{
	std::vector<cell> ids;
	if (!collectNodeIds(amx, params, 1, 1, ids))
	{
		return -1;
	}
	for (const cell id : ids)
	{
		if (!JsonPool::find(id))
		{
			Log::error("JsonArray: value node %d was invalid", id);
			return -2;
		}
	}

	Json array = Json::array();
	for (const cell id : ids)
	{
		Json value;
		if (!JsonPool::take(id, true, value))
		{
			Log::error("JsonArray: value node %d was invalid (passed twice?)", id);
			return -2;
		}
		array.push_back(std::move(value));
	}
	return JsonPool::alloc(amx, std::move(array));
}

// native Node:JsonAppend(Node:a, Node:b);
cell JsonAppend(AMX* amx, cell* params)
{
	const Json* a = JsonPool::pointer(params[1]);
	const Json* b = JsonPool::pointer(params[2]);
	if (!a || !b)
	{
		return -1;
	}
	const bool objects = a->is_object() && b->is_object();
	const bool arrays = a->is_array() && b->is_array();
	if (!objects && !arrays)
	{
		return -1;
	}

	Json left;
	Json right;
	JsonPool::take(params[1], true, left);
	if (!JsonPool::take(params[2], true, right))
	{
		// JsonAppend(a, a): the node was already consumed above.
		right = left;
	}

	if (objects)
	{
		for (auto& entry : right.items())
		{
			left[entry.key()] = std::move(entry.value());
		}
	}
	else
	{
		for (auto& element : right)
		{
			left.push_back(std::move(element));
		}
	}
	return JsonPool::alloc(amx, std::move(left));
}

// Shared by the JsonSet* natives: returns the object node or an error code.
int setTarget(cell nodeId, Json*& object)
{
	object = JsonPool::pointer(nodeId);
	if (!object)
	{
		return 1;
	}
	if (!object->is_object())
	{
		return 2;
	}
	return 0;
}

// native JsonSetObject(Node:node, const key[], Node:object);
cell JsonSetObject(AMX* amx, cell* params)
{
	Json* object = nullptr;
	if (const int error = setTarget(params[1], object))
	{
		return error;
	}
	if (params[3] == params[1])
	{
		return 3;
	}
	Json value;
	if (!JsonPool::take(params[3], true, value))
	{
		return 3;
	}
	(*object)[readString(amx, params[2])] = std::move(value);
	return 0;
}

// native JsonSetArray(Node:node, const key[], Node:array);
cell JsonSetArray(AMX* amx, cell* params)
{
	Json* object = nullptr;
	if (const int error = setTarget(params[1], object))
	{
		return error;
	}
	if (params[3] == params[1])
	{
		return 3;
	}
	Json value;
	if (!JsonPool::take(params[3], true, value))
	{
		return 3;
	}
	// A non-array value is still consumed: it is usually an inline temporary
	// with no variable left to release it.
	if (!value.is_array())
	{
		return 3;
	}
	(*object)[readString(amx, params[2])] = std::move(value);
	return 0;
}

// native JsonArrayAppend(Node:node, const key[], Node:value);
cell JsonArrayAppend(AMX* amx, cell* params)
{
	Json* object = nullptr;
	if (const int error = setTarget(params[1], object))
	{
		return error;
	}
	if (params[3] == params[1])
	{
		return 3;
	}
	Json value;
	if (!JsonPool::take(params[3], true, value))
	{
		return 3;
	}

	const std::string key = readString(amx, params[2]);
	const auto it = object->find(key);
	if (it == object->end())
	{
		// First append creates the array.
		Json array = Json::array();
		array.push_back(std::move(value));
		(*object)[key] = std::move(array);
		return 0;
	}
	if (!it->is_array())
	{
		return 4;
	}
	it->push_back(std::move(value));
	return 0;
}

// native JsonSetInt(Node:node, const key[], output);
cell JsonSetInt(AMX* amx, cell* params)
{
	Json* object = nullptr;
	if (const int error = setTarget(params[1], object))
	{
		return error;
	}
	(*object)[readString(amx, params[2])] = static_cast<std::int64_t>(params[3]);
	return 0;
}

// native JsonSetFloat(Node:node, const key[], Float:output);
cell JsonSetFloat(AMX* amx, cell* params)
{
	Json* object = nullptr;
	if (const int error = setTarget(params[1], object))
	{
		return error;
	}
	(*object)[readString(amx, params[2])] = widenFloat(amx_ctof(params[3]));
	return 0;
}

// native JsonSetBool(Node:node, const key[], bool:output);
cell JsonSetBool(AMX* amx, cell* params)
{
	Json* object = nullptr;
	if (const int error = setTarget(params[1], object))
	{
		return error;
	}
	(*object)[readString(amx, params[2])] = params[3] != 0;
	return 0;
}

// native JsonSetString(Node:node, const key[], const output[], len = sizeof(output));
cell JsonSetString(AMX* amx, cell* params)
{
	Json* object = nullptr;
	if (const int error = setTarget(params[1], object))
	{
		return error;
	}
	(*object)[readString(amx, params[2])] = readString(amx, params[3]);
	return 0;
}

// native JsonGetObject(Node:node, const key[], &Node:output);
cell JsonGetObject(AMX* amx, cell* params)
{
	const Json* target = nullptr;
	if (const int error = lookup(amx, params[1], params[2], target))
	{
		return error;
	}
	const cell id = JsonPool::alloc(amx, Json(*target));
	if (!AmxApi::writeCell(amx, params[3], id))
	{
		JsonPool::erase(id);
		return 1;
	}
	return 0;
}

// native JsonGetInt(Node:node, const key[], &output);
cell JsonGetInt(AMX* amx, cell* params)
{
	const Json* target = nullptr;
	if (const int error = lookup(amx, params[1], params[2], target))
	{
		return error;
	}
	if (!isInteger(*target))
	{
		return 3;
	}
	return AmxApi::writeCell(amx, params[3], asCell(*target)) ? 0 : 1;
}

// native JsonGetFloat(Node:node, const key[], &Float:output);
cell JsonGetFloat(AMX* amx, cell* params)
{
	const Json* target = nullptr;
	if (const int error = lookup(amx, params[1], params[2], target))
	{
		return error;
	}
	// Integers are accepted too: {"x": 5} reads as 5.0.
	if (!target->is_number())
	{
		return 3;
	}
	return AmxApi::writeCell(amx, params[3], asFloatCell(*target)) ? 0 : 1;
}

// native JsonGetBool(Node:node, const key[], &bool:output);
cell JsonGetBool(AMX* amx, cell* params)
{
	const Json* target = nullptr;
	if (const int error = lookup(amx, params[1], params[2], target))
	{
		return error;
	}
	if (!target->is_boolean())
	{
		return 3;
	}
	return AmxApi::writeCell(amx, params[3], target->get<bool>() ? 1 : 0) ? 0 : 1;
}

// native JsonGetString(Node:node, const key[], output[], len = sizeof(output));
cell JsonGetString(AMX* amx, cell* params)
{
	const Json* target = nullptr;
	if (const int error = lookup(amx, params[1], params[2], target))
	{
		return error;
	}
	if (!target->is_string())
	{
		return 3;
	}
	return writeString(amx, params[3], target->get_ref<const std::string&>(), params[4]) ? 0 : 1;
}

// native JsonGetArray(Node:node, const key[], &Node:output);
cell JsonGetArray(AMX* amx, cell* params)
{
	const Json* target = nullptr;
	if (const int error = lookup(amx, params[1], params[2], target))
	{
		return error;
	}
	if (!target->is_array())
	{
		return 3;
	}
	const cell id = JsonPool::alloc(amx, Json(*target));
	if (!AmxApi::writeCell(amx, params[3], id))
	{
		JsonPool::erase(id);
		return 1;
	}
	return 0;
}

// native JsonArrayLength(Node:node, &length);
cell JsonArrayLength(AMX* amx, cell* params)
{
	const Json* node = JsonPool::pointer(params[1]);
	if (!node || !node->is_array())
	{
		return 1;
	}
	return AmxApi::writeCell(amx, params[2], static_cast<cell>(node->size())) ? 0 : 1;
}

// native JsonArrayObject(Node:node, index, &Node:output);
cell JsonArrayObject(AMX* amx, cell* params)
{
	const Json* node = JsonPool::pointer(params[1]);
	if (!node || !node->is_array())
	{
		return 1;
	}
	if (params[2] < 0 || static_cast<std::size_t>(params[2]) >= node->size())
	{
		return 2;
	}
	const cell id = JsonPool::alloc(amx, Json((*node)[static_cast<std::size_t>(params[2])]));
	if (!AmxApi::writeCell(amx, params[3], id))
	{
		JsonPool::erase(id);
		return 2;
	}
	return 0;
}

// JsonGetNode*: a successful read consumes the node
// (unless GC was disabled), so reading array elements in a loop does not leak.
template <typename Check, typename Write>
cell readNode(cell nodeId, Check check, Write write)
{
	JsonPool::Node* node = JsonPool::find(nodeId);
	if (!node || !check(*node->value))
	{
		return 1;
	}
	const bool ok = write(*node->value);
	if (node->gc)
	{
		JsonPool::erase(nodeId);
	}
	return ok ? 0 : 1;
}

// native JsonGetNodeInt(Node:node, &output);
cell JsonGetNodeInt(AMX* amx, cell* params)
{
	return readNode(params[1], isInteger,
		[&](const Json& value) { return AmxApi::writeCell(amx, params[2], asCell(value)); });
}

// native JsonGetNodeFloat(Node:node, &Float:output);
cell JsonGetNodeFloat(AMX* amx, cell* params)
{
	return readNode(params[1], [](const Json& value) { return value.is_number(); },
		[&](const Json& value) { return AmxApi::writeCell(amx, params[2], asFloatCell(value)); });
}

// native JsonGetNodeBool(Node:node, &bool:output);
cell JsonGetNodeBool(AMX* amx, cell* params)
{
	return readNode(params[1], [](const Json& value) { return value.is_boolean(); },
		[&](const Json& value) { return AmxApi::writeCell(amx, params[2], value.get<bool>() ? 1 : 0); });
}

// native JsonGetNodeString(Node:node, output[], len = sizeof(output));
cell JsonGetNodeString(AMX* amx, cell* params)
{
	return readNode(params[1], [](const Json& value) { return value.is_string(); },
		[&](const Json& value) { return writeString(amx, params[2], value.get_ref<const std::string&>(), params[3]); });
}

// native JsonToggleGC(Node:node, bool:toggle);
cell JsonToggleGC(AMX*, cell* params)
{
	JsonPool::Node* node = JsonPool::find(params[1]);
	if (!node)
	{
		return 1;
	}
	node->gc = params[2] != 0;
	return 0;
}

// native JsonCleanup(Node:node, auto = false);
cell JsonCleanup(AMX*, cell* params)
{
	const bool automatic = params[2] != 0;
	JsonPool::Node* node = JsonPool::find(params[1]);
	if (!node)
	{
		if (!automatic)
		{
			Log::error("attempt to cleanup node from invalid ID %d", params[1]);
		}
		return 1;
	}
	if (!node->gc && automatic)
	{
		return 2;
	}
	JsonPool::erase(params[1]);
	return 0;
}

// native JsonSetEncoding(JSON_ENCODING:encoding);
cell JsonSetEncoding(AMX*, cell* params)
{
	switch (params[1])
	{
	case static_cast<cell>(Encoding::Mode::Cp1252):
		Encoding::setMode(Encoding::Mode::Cp1252);
		return 0;
	case static_cast<cell>(Encoding::Mode::Utf8):
		Encoding::setMode(Encoding::Mode::Utf8);
		return 0;
	default:
		Log::error("JsonSetEncoding: unknown encoding %d", params[1]);
		return 1;
	}
}

// native JSON_ENCODING:JsonGetEncoding();
cell JsonGetEncoding(AMX*, cell*)
{
	return static_cast<cell>(Encoding::mode());
}

// -----------------------------------------------------------------------------
// Exception barrier
// -----------------------------------------------------------------------------

template <cell (*Native)(AMX*, cell*), cell MinParams, cell FailReturn>
cell AMX_NATIVE_CALL guarded(AMX* amx, cell* params)
{
	if (paramCount(params) < MinParams)
	{
		Log::error("native called with %d parameters, expected %d (outdated include?)", paramCount(params), MinParams);
		return FailReturn;
	}
	try
	{
		return Native(amx, params);
	}
	catch (const std::exception& e)
	{
		Log::error("unexpected error: %s", e.what());
	}
	catch (...)
	{
		Log::error("unexpected unknown error");
	}
	return FailReturn;
}

#define NATIVE(name, params, fail) { #name, guarded<name, params, fail> }

const AMX_NATIVE_INFO kNatives[] = {
	NATIVE(JsonParse, 2, 1),
	NATIVE(JsonStringify, 3, 1),
	NATIVE(JsonNodeType, 1, JSON_NODE_NULL),
	NATIVE(JsonObject, 0, INVALID_NODE),
	NATIVE(JsonInt, 1, INVALID_NODE),
	NATIVE(JsonBool, 1, INVALID_NODE),
	NATIVE(JsonFloat, 1, INVALID_NODE),
	NATIVE(JsonString, 1, INVALID_NODE),
	NATIVE(JsonArray, 0, INVALID_NODE),
	NATIVE(JsonAppend, 2, INVALID_NODE),
	NATIVE(JsonSetObject, 3, 1),
	NATIVE(JsonSetArray, 3, 1),
	NATIVE(JsonArrayAppend, 3, 1),
	NATIVE(JsonSetInt, 3, 1),
	NATIVE(JsonSetFloat, 3, 1),
	NATIVE(JsonSetBool, 3, 1),
	NATIVE(JsonSetString, 3, 1),
	NATIVE(JsonGetObject, 3, 1),
	NATIVE(JsonGetInt, 3, 1),
	NATIVE(JsonGetFloat, 3, 1),
	NATIVE(JsonGetBool, 3, 1),
	NATIVE(JsonGetString, 4, 1),
	NATIVE(JsonGetArray, 3, 1),
	NATIVE(JsonArrayLength, 2, 1),
	NATIVE(JsonArrayObject, 3, 1),
	NATIVE(JsonGetNodeInt, 2, 1),
	NATIVE(JsonGetNodeFloat, 2, 1),
	NATIVE(JsonGetNodeBool, 2, 1),
	NATIVE(JsonGetNodeString, 3, 1),
	NATIVE(JsonToggleGC, 2, 1),
	NATIVE(JsonCleanup, 2, 1),
	NATIVE(JsonSetEncoding, 1, 1),
	NATIVE(JsonGetEncoding, 0, 0),
	{ nullptr, nullptr },
};

#undef NATIVE
}

namespace Natives
{
const AMX_NATIVE_INFO* list(int& count) noexcept
{
	count = static_cast<int>(sizeof(kNatives) / sizeof(kNatives[0])) - 1;
	return kNatives;
}
}
