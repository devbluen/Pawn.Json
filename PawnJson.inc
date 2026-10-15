/*
 *  Pawn.Json - JSON natives for SA-MP and open.mp, with accented text support.
 *
 *  The same binary works on both servers:
 *    open.mp : components/PawnJson.dll (or PawnJson.so)
 *    SA-MP   : plugins/PawnJson.dll    + "plugins PawnJson" in server.cfg
 *              (Linux: plugins/PawnJson.so + "plugins PawnJson.so")
 *
 *  Usage: #include <PawnJson>
 */

#if defined _pawn_json_included
	#endinput
#endif
#define _pawn_json_included

#define PAWN_JSON_VERSION_MAJOR 1
#define PAWN_JSON_VERSION_MINOR 0
#define PAWN_JSON_VERSION_PATCH 0

// -----------------------------------------------------------------------------
// Encoding
// -----------------------------------------------------------------------------

enum JSON_ENCODING {
    // Default. Strings returned to Pawn are Windows-1252, which is what the
    // SA-MP client renders ("ação" shows correctly in messages, dialogs and
    // textdraws). JsonStringify replaces every non-ASCII character with \uXXXX,
    // so the output is valid JSON for any HTTP API.
    JSON_ENCODING_CP1252,

    // Strings returned to Pawn are raw UTF-8 (one byte per cell) and
    // JsonStringify writes UTF-8 without escapes. Meant for files, sockets or
    // another plugin that expects UTF-8.
    JSON_ENCODING_UTF8,
}

// Input is always detected automatically: text that is already valid UTF-8 is
// kept as is, anything else is read as Windows-1252. Accented text never
// crashes the server, whatever the encoding of the .pwn file.
//
// The setting is global (shared by the gamemode and all filterscripts).
native JsonSetEncoding(JSON_ENCODING:encoding);
native JSON_ENCODING:JsonGetEncoding();

// -----------------------------------------------------------------------------
// Nodes
//
// Every JSON value is stored inside the plugin and the script holds a `Node:`
// handle to it. A node passed as a value to JsonObject, JsonArray, JsonAppend,
// JsonSetObject, JsonSetArray or JsonArrayAppend is moved into the new value,
// and its old handle stops existing. Remaining nodes are released when their
// `Node:` variable goes out of scope (see operator~ at the end of this file)
// and, at the latest, when the script that created them unloads.
// -----------------------------------------------------------------------------

enum JSON_NODE {
    JSON_NODE_NUMBER,
    JSON_NODE_BOOLEAN,
    JSON_NODE_STRING,
    JSON_NODE_OBJECT,
    JSON_NODE_ARRAY,
    JSON_NODE_NULL,
}

// Parses `string` into a node stored in `output`.
// Returns 0 on success and 1 if the text is not valid JSON.
native JsonParse(const string[], &Node:output);

// Writes the JSON text of `node` into `buf`. Returns 0, or 1 if the node does
// not exist (in which case "null" is written). If the buffer is too small, the
// text is cut and a warning goes to the log.
native JsonStringify(Node:node, buf[], len = sizeof(buf));

// Type of the node. An invalid handle returns JSON_NODE_NULL.
native JSON_NODE:JsonNodeType(Node:node);

// Builds an object from key/value pairs, in the given order:
//
//     JsonObject(
//         "name", JsonString("João"),
//         "location", JsonObject("city", JsonString("São Paulo"))
//     );
//     // {"name":"João","location":{"city":"São Paulo"}}
//
// Returns -1 if the last key has no value and -2 if any value is not a valid
// node (in which case none of the values is consumed).
native Node:JsonObject({_, Node}:...);

// Nodes holding simple values.
native Node:JsonInt(value);
native Node:JsonBool(bool:value);
native Node:JsonFloat(Float:value);
native Node:JsonString(const value[]);

// Builds an array from the given nodes, in order:
//
//     JsonArray(JsonInt(1), JsonString("two"), JsonBool(true));
//     // [1,"two",true]
//
// Returns -2 if any value is not a valid node.
native Node:JsonArray(Node:...);

// Merges two objects (keys of `b` overwrite keys of `a`) or two arrays (the
// items of `b` go to the end of `a`) and returns a new node. `a` and `b` are
// consumed. Returns -1 if both are not of the same kind (object/array); in
// that case nothing is consumed.
//
//     new Node:list = JsonArray(JsonInt(1)) + JsonArray(JsonInt(2)); // [1,2]
native Node:JsonAppend(Node:a, Node:b);
native Node:operator+(Node:a, Node:b) = JsonAppend;

// Sets `key` on an existing object (creates or replaces it).
// Returns 0; 1 if the node does not exist; 2 if it is not an object;
// 3 (JsonSetObject only) if `object` is not a valid node.
native JsonSetObject(Node:node, const key[], Node:object);
native JsonSetInt(Node:node, const key[], output);
native JsonSetFloat(Node:node, const key[], Float:output);
native JsonSetBool(Node:node, const key[], bool:output);
native JsonSetString(Node:node, const key[], const output[], len = sizeof(output));

// Sets an array at `key`. Same as JsonSetObject, but also returns 3 when
// `array` is not an array (the node is released anyway).
//
//     JsonSetArray(rules, "limits", JsonArray(JsonInt(1), JsonInt(2)));
//     // {"limits":[1,2]}
native JsonSetArray(Node:node, const key[], Node:array);

// Appends `value` (of any type) to the end of the array at `key`. If the key
// does not exist yet, the array is created. `value` is consumed.
// Returns 0; 1 if the node does not exist; 2 if it is not an object; 3 if
// `value` is not a valid node; 4 if `key` already exists and is not an array
// (the node is released anyway).
//
//     new Node:payload = JsonObject();
//     for (new i = 0; i < 3; i++) {
//         JsonArrayAppend(payload, "houses", JsonObject("id", JsonInt(i)));
//     }
//     // {"houses":[{"id":0},{"id":1},{"id":2}]}
native JsonArrayAppend(Node:node, const key[], Node:value);

// Copies the value at `key` (of any type) into a new node in `output`. The
// source node stays valid.
// Returns 0; 1 if the node does not exist; 2 if it is not an object or does
// not have the key.
native JsonGetObject(Node:node, const key[], &Node:output);

// Reads `key` directly as a Pawn value.
// Returns 0; 1 if the node does not exist; 2 if the key does not exist (or the
// node is not an object); 3 if the value has another type. JsonGetFloat also
// accepts integers ({"x": 5} reads as 5.0).
native JsonGetInt(Node:node, const key[], &output);
native JsonGetFloat(Node:node, const key[], &Float:output);
native JsonGetBool(Node:node, const key[], &bool:output);
native JsonGetString(Node:node, const key[], output[], len = sizeof(output));

// Same as JsonGetObject, but returns 3 if the value is not an array.
native JsonGetArray(Node:node, const key[], &Node:output);

// Number of items in an array. Returns 1 if the node is not an array.
native JsonArrayLength(Node:node, &length);

// Copies item `index` (starting at 0) of an array into a new node.
// Returns 1 if the node is not an array and 2 if the index is out of range.
native JsonArrayObject(Node:node, index, &Node:output);

// Reads a node holding a simple value (e.g. an item from JsonArrayObject). A
// successful read releases the node (unless its GC is disabled), so reading
// items in a loop does not pile up memory.
// Returns 0, or 1 if the node does not exist or has another type.
//
//     new Node:items, Node:item, count, name[32];
//     JsonGetArray(root, "items", items);
//     JsonArrayLength(items, count);
//     for (new i = 0; i < count; i++) {
//         JsonArrayObject(items, i, item);
//         JsonGetNodeString(item, name);
//     }
native JsonGetNodeInt(Node:node, &output);
native JsonGetNodeFloat(Node:node, &Float:output);
native JsonGetNodeBool(Node:node, &bool:output);
native JsonGetNodeString(Node:node, output[], len = sizeof(output));

// Turns automatic release of a node on or off. With it off, the node survives
// the end of its scope (including as a function parameter) and, when passed as
// a value, is copied instead of moved. Useful to keep a node in a global
// variable or pass it through several functions. Release it later with
// JsonCleanup(node), otherwise it stays in memory until the script unloads.
//
//     new Node:n = JsonObject();
//     JsonToggleGC(n, false);
//     CallLocalFunction("FillJson", "d", _:n);
//     JsonToggleGC(n, true);
native JsonToggleGC(Node:node, bool:toggle);

// Releases a node. Returns 0; 1 if it does not exist; 2 if `auto` is true and
// the node's GC is disabled. When calling it manually, leave `auto` as false.
native JsonCleanup(Node:node, auto = false);

// Called by the compiler when `Node:` variables go out of scope.
stock operator~(const Node:nodes[], len) {
    for (new i; i < len; ++i) {
        JsonCleanup(nodes[i], true);
    }
}
