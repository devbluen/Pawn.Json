/*
 *  Pawn.Json - unit tests
 *
 *  Runs every native against a fake AMX (a flat memory buffer) through the
 *  same AMX export table the servers provide.
 */

#include "amx-api.hpp"
#include "encoding.hpp"
#include "json-pool.hpp"
#include "natives.hpp"
#include <plugincommon.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

namespace
{
int failures = 0;
int checks = 0;

#define CHECK(cond)                                                              \
	do                                                                           \
	{                                                                            \
		++checks;                                                                \
		if (!(cond))                                                             \
		{                                                                        \
			++failures;                                                          \
			std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
		}                                                                        \
	} while (0)

#define CHECK_EQ_STR(actual, expected)                                           \
	do                                                                           \
	{                                                                            \
		++checks;                                                                \
		const std::string a_ = (actual);                                         \
		const std::string e_ = (expected);                                       \
		if (a_ != e_)                                                            \
		{                                                                        \
			++failures;                                                          \
			std::printf("  FAIL %s:%d: got '%s' expected '%s'\n", __FILE__,      \
				__LINE__, a_.c_str(), e_.c_str());                               \
		}                                                                        \
	} while (0)

// --- fake AMX ---------------------------------------------------------------

constexpr std::size_t kMemoryCells = 1 << 16;
std::vector<cell> memory(kMemoryCells);
std::size_t heapTop = 1; // address 0 is never handed out
AMX fakeAmx {};
void* table[64] {};

int AMXAPI fakeGetAddr(AMX* amx, cell address, cell** physical)
{
	if (address < 0 || address >= amx->stp || (address % sizeof(cell)) != 0)
	{
		return AMX_ERR_MEMACCESS;
	}
	*physical = reinterpret_cast<cell*>(amx->data + address);
	return AMX_ERR_NONE;
}

int AMXAPI fakeRegister(AMX*, const AMX_NATIVE_INFO*, int)
{
	return AMX_ERR_NONE;
}

void resetAmx()
{
	std::fill(memory.begin(), memory.end(), 0);
	heapTop = 1;
	std::memset(&fakeAmx, 0, sizeof(fakeAmx));
	fakeAmx.data = reinterpret_cast<unsigned char*>(memory.data());
	fakeAmx.stp = static_cast<cell>(kMemoryCells * sizeof(cell));
}

cell allot(std::size_t cells)
{
	const cell address = static_cast<cell>(heapTop * sizeof(cell));
	heapTop += cells;
	return address;
}

cell* phys(cell address)
{
	return &memory[static_cast<std::size_t>(address) / sizeof(cell)];
}

// Unpacked string, one byte per cell (what the Pawn compiler emits).
cell str(const std::string& bytes)
{
	const cell address = allot(bytes.size() + 1);
	for (std::size_t i = 0; i < bytes.size(); ++i)
	{
		phys(address)[i] = static_cast<unsigned char>(bytes[i]);
	}
	return address;
}

// Unpacked string with sign-extended characters ('é' == -23).
cell signedStr(const std::string& bytes)
{
	const cell address = allot(bytes.size() + 1);
	for (std::size_t i = 0; i < bytes.size(); ++i)
	{
		phys(address)[i] = static_cast<signed char>(bytes[i]);
	}
	return address;
}

cell packedStr(const std::string& bytes)
{
	const std::size_t cells = bytes.size() / 4 + 1;
	const cell address = allot(cells);
	for (std::size_t i = 0; i < bytes.size(); ++i)
	{
		const int shift = 24 - static_cast<int>(i % 4) * 8;
		phys(address)[i / 4] |= static_cast<cell>(static_cast<ucell>(static_cast<unsigned char>(bytes[i])) << shift);
	}
	return address;
}

cell ref(cell value = 0)
{
	const cell address = allot(1);
	*phys(address) = value;
	return address;
}

cell buffer(std::size_t cells)
{
	return allot(cells);
}

std::string readBack(cell address)
{
	std::string out;
	for (const cell* p = phys(address); *p; ++p)
	{
		out.push_back(static_cast<char>(*p & 0xFF));
	}
	return out;
}

AMX_NATIVE nativeByName(const char* name)
{
	int count = 0;
	const AMX_NATIVE_INFO* natives = Natives::list(count);
	for (int i = 0; i < count; ++i)
	{
		if (std::strcmp(natives[i].name, name) == 0)
		{
			return natives[i].func;
		}
	}
	std::printf("  missing native %s\n", name);
	std::abort();
}

cell call(const char* name, std::initializer_list<cell> args)
{
	std::vector<cell> params;
	params.push_back(static_cast<cell>(args.size() * sizeof(cell)));
	params.insert(params.end(), args.begin(), args.end());
	return nativeByName(name)(&fakeAmx, params.data());
}

cell floatCell(float value) { return amx_ftoc(value); }

// Convenience wrappers mirroring the Pawn API.
cell JsonString(const std::string& s) { return call("JsonString", { str(s) }); }
cell JsonInt(cell v) { return call("JsonInt", { v }); }
cell JsonFloat(float v) { return call("JsonFloat", { amx_ftoc(v) }); }
cell JsonBool(bool v) { return call("JsonBool", { v ? 1 : 0 }); }

std::string stringify(cell node, cell size = 1024)
{
	const cell out = buffer(static_cast<std::size_t>(size));
	call("JsonStringify", { node, out, size });
	return readBack(out);
}

cell parse(const std::string& text, cell* result = nullptr)
{
	const cell out = ref(-1);
	const cell ret = call("JsonParse", { str(text), out });
	if (result)
	{
		*result = ret;
	}
	return *phys(out);
}

const std::string kAcaoCp1252 = "a\xE7\xE3o";      // "ação" in Windows-1252
const std::string kAcaoUtf8 = "a\xC3\xA7\xC3\xA3o"; // "ação" in UTF-8

// --- tests ------------------------------------------------------------------

void testEncodingHelpers()
{
	CHECK(Encoding::isValidUtf8(kAcaoUtf8));
	CHECK(!Encoding::isValidUtf8(kAcaoCp1252));
	CHECK(!Encoding::isValidUtf8("\xC0\xAF"));         // overlong
	CHECK(!Encoding::isValidUtf8("\xED\xA0\x80"));     // surrogate
	CHECK(!Encoding::isValidUtf8("\xE2\x82"));         // truncated
	CHECK_EQ_STR(Encoding::cp1252ToUtf8(kAcaoCp1252), kAcaoUtf8);
	CHECK_EQ_STR(Encoding::utf8ToCp1252(kAcaoUtf8), kAcaoCp1252);
	CHECK_EQ_STR(Encoding::cp1252ToUtf8("\x80"), "\xE2\x82\xAC");  // €
	CHECK_EQ_STR(Encoding::utf8ToCp1252("\xE2\x82\xAC"), "\x80");
	CHECK_EQ_STR(Encoding::utf8ToCp1252("\xE4\xB8\xAD"), "?");     // 中
	CHECK_EQ_STR(Encoding::fromPawn(kAcaoCp1252), kAcaoUtf8);
	CHECK_EQ_STR(Encoding::fromPawn(kAcaoUtf8), kAcaoUtf8);
	CHECK(Encoding::utf8SafeCut(kAcaoUtf8, 2) == 1);
	CHECK(Encoding::utf8SafeCut(kAcaoUtf8, 3) == 3);
}

void testAccentsCp1252()
{
	Encoding::setMode(Encoding::Mode::Cp1252);

	// The classic crash case: accented text from a Windows-1252 .pwn.
	const cell node = call("JsonObject", { str("mensagem"), ref(JsonString("Ol\xE1, a\xE7\xE3o n\xE3o \xE9 f\xE1" "cil!")) });
	CHECK(node >= 0);
	CHECK_EQ_STR(stringify(node), "{\"mensagem\":\"Ol\\u00e1, a\\u00e7\\u00e3o n\\u00e3o \\u00e9 f\\u00e1cil!\"}");

	const cell out = buffer(64);
	CHECK(call("JsonGetString", { node, str("mensagem"), out, 64 }) == 0);
	CHECK_EQ_STR(readBack(out), "Ol\xE1, a\xE7\xE3o n\xE3o \xE9 f\xE1" "cil!");

	// UTF-8 source file gives the same JSON and the same string back.
	const cell utf8Node = JsonString(kAcaoUtf8);
	CHECK_EQ_STR(stringify(utf8Node), "\"a\\u00e7\\u00e3o\"");
	CHECK(call("JsonGetNodeString", { utf8Node, out, 64 }) == 0);
	CHECK_EQ_STR(readBack(out), kAcaoCp1252);

	// Accented keys.
	const cell keyed = call("JsonObject", { str("descri\xE7\xE3o"), ref(JsonInt(1)) });
	const cell value = ref();
	CHECK(call("JsonGetInt", { keyed, str("descri\xE7\xE3o"), value }) == 0);
	CHECK(*phys(value) == 1);
	CHECK(call("JsonGetInt", { keyed, str("descri\xC3\xA7\xC3\xA3o"), value }) == 0);

	// Sign-extended and packed strings.
	const cell signedNode = call("JsonString", { signedStr(kAcaoCp1252) });
	CHECK_EQ_STR(stringify(signedNode), "\"a\\u00e7\\u00e3o\"");
	const cell packedNode = call("JsonString", { packedStr("p\xE3o de a\xE7\xFA" "car") });
	CHECK_EQ_STR(stringify(packedNode), "\"p\\u00e3o de a\\u00e7\\u00facar\"");

	// Euro sign and characters outside Windows-1252.
	const cell euro = JsonString("\x80 10");
	CHECK_EQ_STR(stringify(euro), "\"\\u20ac 10\"");
	const cell parsed = parse("{\"t\":\"\xE4\xB8\xAD \\u00e9 \xE2\x82\xAC\"}");
	CHECK(call("JsonGetString", { parsed, str("t"), out, 64 }) == 0);
	CHECK_EQ_STR(readBack(out), "? \xE9 \x80");
}

void testAccentsUtf8()
{
	Encoding::setMode(Encoding::Mode::Utf8);
	CHECK(call("JsonGetEncoding", {}) == 1);

	const cell node = call("JsonObject", { str("k"), ref(JsonString(kAcaoCp1252)) });
	CHECK_EQ_STR(stringify(node), "{\"k\":\"" + kAcaoUtf8 + "\"}");

	const cell out = buffer(64);
	CHECK(call("JsonGetString", { node, str("k"), out, 64 }) == 0);
	CHECK_EQ_STR(readBack(out), kAcaoUtf8);

	// Truncation never splits a multi-byte character: "aç" needs 3 bytes, a
	// 3-cell buffer holds 2 bytes, so only "a" fits.
	CHECK(call("JsonGetString", { node, str("k"), out, 3 }) == 0);
	CHECK_EQ_STR(readBack(out), "a");

	CHECK(call("JsonSetEncoding", { 0 }) == 0);
	CHECK(call("JsonSetEncoding", { 7 }) == 1);
	CHECK(Encoding::mode() == Encoding::Mode::Cp1252);
}

void testParse()
{
	cell ret = -1;
	const cell node = parse("{\"name\":\"Jo\xC3\xA3o\",\"level\":10,\"money\":1.5,\"vip\":true,\"items\":[1,\"dois\",3.25],\"nested\":{\"x\":-5}}", &ret);
	CHECK(ret == 0);
	CHECK(node >= 0);

	const cell value = ref();
	CHECK(call("JsonGetInt", { node, str("level"), value }) == 0);
	CHECK(*phys(value) == 10);
	CHECK(call("JsonGetFloat", { node, str("money"), value }) == 0);
	CHECK(amx_ctof(*phys(value)) == 1.5f);
	CHECK(call("JsonGetFloat", { node, str("level"), value }) == 0);
	CHECK(amx_ctof(*phys(value)) == 10.0f);
	CHECK(call("JsonGetBool", { node, str("vip"), value }) == 0);
	CHECK(*phys(value) == 1);

	const cell out = buffer(64);
	CHECK(call("JsonGetString", { node, str("name"), out, 64 }) == 0);
	CHECK_EQ_STR(readBack(out), "Jo\xE3o");

	// Documented error codes.
	CHECK(call("JsonGetInt", { 999999, str("level"), value }) == 1);
	CHECK(call("JsonGetInt", { node, str("missing"), value }) == 2);
	CHECK(call("JsonGetInt", { node, str("name"), value }) == 3);
	CHECK(call("JsonGetInt", { node, str("money"), value }) == 3);
	CHECK(call("JsonGetBool", { node, str("level"), value }) == 3);
	CHECK(call("JsonGetString", { node, str("level"), out, 64 }) == 3);

	// Arrays.
	const cell arr = ref(-1);
	CHECK(call("JsonGetArray", { node, str("items"), arr }) == 0);
	CHECK(call("JsonGetArray", { node, str("level"), ref() }) == 3);
	CHECK(call("JsonNodeType", { *phys(arr) }) == 4);
	CHECK(call("JsonArrayLength", { *phys(arr), value }) == 0);
	CHECK(*phys(value) == 3);
	CHECK(call("JsonArrayLength", { node, value }) == 1);

	const cell element = ref(-1);
	CHECK(call("JsonArrayObject", { *phys(arr), 1, element }) == 0);
	CHECK(call("JsonGetNodeString", { *phys(element), out, 64 }) == 0);
	CHECK_EQ_STR(readBack(out), "dois");
	// The read consumed the element node.
	CHECK(JsonPool::find(*phys(element)) == nullptr);

	CHECK(call("JsonArrayObject", { *phys(arr), 2, element }) == 0);
	CHECK(call("JsonGetNodeFloat", { *phys(element), value }) == 0);
	CHECK(amx_ctof(*phys(value)) == 3.25f);
	CHECK(call("JsonArrayObject", { *phys(arr), 0, element }) == 0);
	CHECK(call("JsonGetNodeBool", { *phys(element), value }) == 1); // wrong type
	CHECK(call("JsonGetNodeInt", { *phys(element), value }) == 0);
	CHECK(*phys(value) == 1);
	CHECK(call("JsonArrayObject", { *phys(arr), 3, element }) == 2);
	CHECK(call("JsonArrayObject", { *phys(arr), -1, element }) == 2);

	// Nested objects; the parent stays usable after JsonGetObject.
	const cell nested = ref(-1);
	CHECK(call("JsonGetObject", { node, str("nested"), nested }) == 0);
	CHECK(call("JsonGetInt", { *phys(nested), str("x"), value }) == 0);
	CHECK(*phys(value) == -5);
	CHECK(call("JsonGetObject", { node, str("nope"), nested }) == 2);
	CHECK(call("JsonGetInt", { node, str("level"), value }) == 0);

	// Invalid JSON and Windows-1252 bytes inside JSON text never crash.
	parse("{invalid", &ret);
	CHECK(ret == 1);
	parse("", &ret);
	CHECK(ret == 1);
	const cell cp = parse("{\"cidade\":\"S\xE3o Paulo\"}", &ret);
	CHECK(ret == 0);
	CHECK(call("JsonGetString", { cp, str("cidade"), out, 64 }) == 0);
	CHECK_EQ_STR(readBack(out), "S\xE3o Paulo");
}

void testBuilders()
{
	CHECK_EQ_STR(stringify(JsonInt(-42)), "-42");
	CHECK_EQ_STR(stringify(JsonFloat(1.34f)), "1.34");
	CHECK_EQ_STR(stringify(JsonFloat(0.1f)), "0.1");
	CHECK_EQ_STR(stringify(JsonBool(true)), "true");
	CHECK_EQ_STR(stringify(call("JsonObject", {})), "{}");
	CHECK_EQ_STR(stringify(call("JsonArray", {})), "[]");

	// Insertion order is preserved; children are consumed.
	const cell child = JsonString("v");
	const cell obj = call("JsonObject", {
		str("b"), ref(JsonInt(1)),
		str("a"), ref(call("JsonArray", { ref(JsonInt(1)), ref(JsonString("x")), ref(call("JsonObject", { str("k"), ref(JsonBool(false)) })) })),
		str("c"), ref(child),
	});
	CHECK(obj >= 0);
	CHECK_EQ_STR(stringify(obj), "{\"b\":1,\"a\":[1,\"x\",{\"k\":false}],\"c\":\"v\"}");
	CHECK(JsonPool::find(child) == nullptr);

	// Invalid value: error code and nothing consumed.
	const cell keep = JsonInt(5);
	CHECK(call("JsonObject", { str("a"), ref(keep), str("b"), ref(123456) }) == -2);
	CHECK(JsonPool::find(keep) != nullptr);
	CHECK(call("JsonObject", { str("dangling") }) == -1);
	CHECK(call("JsonArray", { ref(keep), ref(987654) }) == -2);

	// Setters.
	const cell target = call("JsonObject", {});
	CHECK(call("JsonSetInt", { target, str("i"), 7 }) == 0);
	CHECK(call("JsonSetFloat", { target, str("f"), floatCell(2.5f) }) == 0);
	CHECK(call("JsonSetBool", { target, str("b"), 1 }) == 0);
	CHECK(call("JsonSetString", { target, str("s"), str("Jos\xE9"), 5 }) == 0);
	CHECK(call("JsonSetObject", { target, str("o"), call("JsonObject", { str("z"), ref(JsonInt(0)) }) }) == 0);
	CHECK(call("JsonSetInt", { target, str("i"), 8 }) == 0); // overwrite keeps position
	CHECK_EQ_STR(stringify(target), "{\"i\":8,\"f\":2.5,\"b\":true,\"s\":\"Jos\\u00e9\",\"o\":{\"z\":0}}");
	CHECK(call("JsonSetInt", { 555555, str("i"), 1 }) == 1);
	CHECK(call("JsonSetInt", { JsonInt(1), str("i"), 1 }) == 2);
	CHECK(call("JsonSetObject", { target, str("x"), 444444 }) == 3);

	// JsonSetArray.
	const cell rules = call("JsonObject", {});
	const cell list = call("JsonArray", { ref(JsonInt(1)), ref(JsonInt(2)), ref(JsonInt(3)), ref(JsonInt(4)) });
	CHECK(call("JsonSetArray", { rules, str("regras"), list }) == 0);
	CHECK(JsonPool::find(list) == nullptr);
	CHECK(call("JsonSetArray", { rules, str("regras"), call("JsonArray", { ref(JsonInt(9)) }) }) == 0); // replaces
	CHECK_EQ_STR(stringify(rules), "{\"regras\":[9]}");
	const cell notArray = JsonInt(5);
	CHECK(call("JsonSetArray", { rules, str("x"), notArray }) == 3);
	CHECK(JsonPool::find(notArray) == nullptr); // released anyway
	CHECK(call("JsonSetArray", { rules, str("x"), 333333 }) == 3);
	CHECK(call("JsonSetArray", { rules, str("x"), rules }) == 3);
	CHECK(call("JsonSetArray", { JsonInt(1), str("x"), call("JsonArray", {}) }) == 2);
	CHECK(call("JsonSetArray", { 222222, str("x"), call("JsonArray", {}) }) == 1);

	// JsonArrayAppend: creates the array on first use, then appends.
	const cell payload = call("JsonObject", { str("nome"), ref(JsonString("Jo\xE3o")) });
	for (cell i = 0; i < 3; ++i)
	{
		CHECK(call("JsonArrayAppend", { payload, str("propriedades"), call("JsonObject", {
			str("id"), ref(JsonInt(i)),
			str("cidade"), ref(JsonString("S\xE3o Paulo")),
		}) }) == 0);
	}
	CHECK(call("JsonArrayAppend", { payload, str("tags"), call("JsonArray", { ref(JsonInt(1)) }) }) == 0);
	CHECK_EQ_STR(stringify(payload),
		"{\"nome\":\"Jo\\u00e3o\",\"propriedades\":["
		"{\"id\":0,\"cidade\":\"S\\u00e3o Paulo\"},"
		"{\"id\":1,\"cidade\":\"S\\u00e3o Paulo\"},"
		"{\"id\":2,\"cidade\":\"S\\u00e3o Paulo\"}],"
		"\"tags\":[[1]]}");
	const cell orphan = JsonInt(7);
	CHECK(call("JsonArrayAppend", { payload, str("nome"), orphan }) == 4);
	CHECK(JsonPool::find(orphan) == nullptr);
	CHECK(call("JsonArrayAppend", { payload, str("tags"), 111111 }) == 3);
	CHECK(call("JsonArrayAppend", { payload, str("tags"), payload }) == 3);
	CHECK(call("JsonArrayAppend", { JsonInt(1), str("x"), JsonInt(1) }) == 2);
	CHECK(call("JsonArrayAppend", { 101010, str("x"), JsonInt(1) }) == 1);

	// Append.
	const cell a = call("JsonObject", { str("key1"), ref(JsonString("value")) });
	const cell b = call("JsonObject", { str("key2"), ref(JsonString("value")) });
	const cell c = call("JsonAppend", { a, b });
	CHECK_EQ_STR(stringify(c), "{\"key1\":\"value\",\"key2\":\"value\"}");
	CHECK(JsonPool::find(a) == nullptr && JsonPool::find(b) == nullptr);
	const cell arr = call("JsonAppend", {
		call("JsonArray", { ref(JsonInt(1)), ref(JsonInt(2)) }),
		call("JsonArray", { ref(JsonInt(3)) }) });
	CHECK_EQ_STR(stringify(arr), "[1,2,3]");
	const cell mismatchA = call("JsonArray", {});
	CHECK(call("JsonAppend", { mismatchA, call("JsonObject", {}) }) == -1);
	CHECK(JsonPool::find(mismatchA) != nullptr);

	// Node types.
	CHECK(call("JsonNodeType", { JsonInt(1) }) == 0);
	CHECK(call("JsonNodeType", { JsonFloat(1.0f) }) == 0);
	CHECK(call("JsonNodeType", { JsonBool(true) }) == 1);
	CHECK(call("JsonNodeType", { JsonString("") }) == 2);
	CHECK(call("JsonNodeType", { call("JsonObject", {}) }) == 3);
	CHECK(call("JsonNodeType", { call("JsonArray", {}) }) == 4);
	CHECK(call("JsonNodeType", { parse("null") }) == 5);
	CHECK(call("JsonNodeType", { 777777 }) == 5);

	// Escapes.
	CHECK_EQ_STR(stringify(JsonString("aspas \" barra \\ nova\nlinha")), "\"aspas \\\" barra \\\\ nova\\nlinha\"");
}

void testGc()
{
	// operator~ calls JsonCleanup(node, true).
	const cell node = call("JsonObject", { str("key"), ref(JsonString("value")) });
	CHECK(call("JsonToggleGC", { node, 0 }) == 0);
	CHECK(call("JsonCleanup", { node, 1 }) == 2);          // scope exit ignored
	const cell out = buffer(16);
	CHECK(call("JsonGetString", { node, str("key"), out, 16 }) == 0);
	CHECK(call("JsonCleanup", { node, 0 }) == 0);
	CHECK(call("JsonCleanup", { node, 0 }) == 1);
	CHECK(call("JsonCleanup", { node, 1 }) == 1);
	CHECK(call("JsonToggleGC", { node, 1 }) == 1);

	// GC disabled: passing the node into a builder copies it instead.
	const cell shared = JsonString("x");
	call("JsonToggleGC", { shared, 0 });
	call("JsonArray", { ref(shared), ref(shared) });
	CHECK(JsonPool::find(shared) != nullptr);
	call("JsonCleanup", { shared, 0 });

	// Truncated Stringify still null-terminates.
	CHECK_EQ_STR(stringify(call("JsonObject", { str("abc"), ref(JsonInt(12345)) }), 6), "{\"abc");

	// Invalid nodes never crash.
	CHECK(call("JsonStringify", { 888888, buffer(8), 8 }) == 1);
}

void testScriptUnload()
{
	JsonPool::clear();
	AMX other {};
	JsonPool::alloc(&other, Json(1));
	JsonInt(1);
	JsonInt(2);
	CHECK(JsonPool::size() == 3);
	JsonPool::eraseOwnedBy(&fakeAmx);
	CHECK(JsonPool::size() == 1);
	JsonPool::clear();
	CHECK(JsonPool::size() == 0);
}

void testBadParams()
{
	// Calls with too few parameters (outdated include) fail safely.
	CHECK(call("JsonGetString", { 0, 0 }) == 1);
	CHECK(call("JsonInt", {}) == -1);
	// Invalid addresses.
	CHECK(call("JsonParse", { -4, -4 }) == 1);
	CHECK(call("JsonGetInt", { 0, 0x7FFFFFF0, 0x7FFFFFF0 }) == 1);
}
}

int main()
{
	table[PLUGIN_AMX_EXPORT_GetAddr] = reinterpret_cast<void*>(&fakeGetAddr);
	table[PLUGIN_AMX_EXPORT_Register] = reinterpret_cast<void*>(&fakeRegister);
	AmxApi::setFunctionTable(table);

	const struct
	{
		const char* name;
		void (*run)();
	} tests[] = {
		{ "encoding helpers", testEncodingHelpers },
		{ "accents (cp1252 mode)", testAccentsCp1252 },
		{ "accents (utf8 mode)", testAccentsUtf8 },
		{ "parse and getters", testParse },
		{ "builders and setters", testBuilders },
		{ "garbage collection", testGc },
		{ "script unload", testScriptUnload },
		{ "bad parameters", testBadParams },
	};

	for (const auto& test : tests)
	{
		resetAmx();
		Encoding::setMode(Encoding::Mode::Cp1252);
		const int before = failures;
		test.run();
		std::printf("[%s] %s\n", failures == before ? " OK " : "FAIL", test.name);
	}

	std::printf("\n%d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
