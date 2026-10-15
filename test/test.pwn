// Pawn.Json integration test.
//
// This file is saved as Windows-1252 on purpose: that is how most SA-MP
// gamemodes are encoded and the case that crashes other JSON plugins.
// Compile it as a gamemode and start the server; it prints a summary and
// shuts the server down.

#include <open.mp>
#include <PawnJson>

new gChecks, gFailures;

#define CHECK(%0) Check(!!(%0), __line)

Check(bool:ok, line) {
    gChecks++;
    if (!ok) {
        gFailures++;
        printf("[test] FAIL line %d", line);
    }
}

bool:Same(const a[], const b[]) {
    if (!a[0] || !b[0]) {
        return !a[0] && !b[0];
    }
    return strcmp(a, b) == 0;
}

main() {}

public OnGameModeInit() {
    print("[test] Pawn.Json integration test starting");

    TestAccentsBuild();
    TestAccentsParse();
    TestApi();
    TestArrays();
    TestScopeGC();
    TestUtf8Mode();

    printf("[test] RESULT: %d checks, %d failures", gChecks, gFailures);
    SetTimer("ShutdownServer", 250, false);
    return 1;
}

forward ShutdownServer();
public ShutdownServer() {
    SendRconCommand("exit");
}

TestAccentsBuild() {
    new Node:node = JsonObject(
        "mensagem", JsonString("Olá, ação não é fácil! Ç Ã Õ ü €"),
        "descrição", JsonInt(1)
    );
    new buf[256];
    CHECK(JsonStringify(node, buf) == 0);
    printf("[test] stringify: %s", buf);
    new const expected[] = "{\"mensagem\":\"Ol\\u00e1, a\\u00e7\\u00e3o n\\u00e3o \\u00e9 f\\u00e1cil! \\u00c7 \\u00c3 \\u00d5 \\u00fc \\u20ac\",\"descri\\u00e7\\u00e3o\":1}";
    CHECK(Same(buf, expected));

    new out[64];
    CHECK(JsonGetString(node, "mensagem", out) == 0);
    CHECK(Same(out, "Olá, ação não é fácil! Ç Ã Õ ü €"));
    printf("[test] round trip: %s", out);

    new value;
    CHECK(JsonGetInt(node, "descrição", value) == 0);
    CHECK(value == 1);

    CHECK(JsonSetString(node, "nome", "João") == 0);
    CHECK(JsonGetString(node, "nome", out) == 0);
    CHECK(Same(out, "João"));

    // Packed strings.
    new packed[32 char];
    strpack(packed, "pão de açúcar");
    new Node:p = JsonString(packed);
    CHECK(JsonGetNodeString(p, out) == 0);
    CHECK(Same(out, "pão de açúcar"));
}

TestAccentsParse() {
    new Node:node;
    // UTF-8 bytes, as received from an HTTP API, plus a \u escape.
    new const json[] = "{\"cidade\":\"S\xC3;\xA3;o Paulo\",\"esc\":\"\\u00e9\",\"lista\":[\"ma\xC3;\xA7;\xC3;\xA3;\",\"lim\xC3;\xA3;o\"]}";
    CHECK(JsonParse(json, node) == 0);

    new out[64];
    CHECK(JsonGetString(node, "cidade", out) == 0);
    CHECK(Same(out, "São Paulo"));
    CHECK(JsonGetString(node, "esc", out) == 0);
    CHECK(Same(out, "é"));

    new Node:list, Node:item, len;
    CHECK(JsonGetArray(node, "lista", list) == 0);
    CHECK(JsonArrayLength(list, len) == 0);
    CHECK(len == 2);
    CHECK(JsonArrayObject(list, 1, item) == 0);
    CHECK(JsonGetNodeString(item, out) == 0);
    CHECK(Same(out, "limão"));

    // Windows-1252 text inside JSON text.
    new Node:cp;
    CHECK(JsonParse("{\"rua\":\"Avenida Paulista, nº 1000\"}", cp) == 0);
    CHECK(JsonGetString(cp, "rua", out) == 0);
    CHECK(Same(out, "Avenida Paulista, nº 1000"));

    new Node:bad;
    CHECK(JsonParse("{isso não é json", bad) == 1);
}

TestApi() {
    new Node:node = JsonObject(
        "int", JsonInt(42),
        "float", JsonFloat(1.34),
        "bool", JsonBool(true),
        "arr", JsonArray(JsonInt(1), JsonString("dois"), JsonObject("k", JsonBool(false))),
        "obj", JsonObject("inner", JsonInt(-5))
    );

    new buf[256];
    JsonStringify(node, buf);
    printf("[test] stringify: %s", buf);
    new const expected[] = "{\"int\":42,\"float\":1.34,\"bool\":true,\"arr\":[1,\"dois\",{\"k\":false}],\"obj\":{\"inner\":-5}}";
    CHECK(Same(buf, expected));

    new i, Float:f, bool:b;
    CHECK(JsonGetInt(node, "int", i) == 0);
    CHECK(i == 42);
    CHECK(JsonGetFloat(node, "float", f) == 0);
    CHECK(f == 1.34);
    CHECK(JsonGetBool(node, "bool", b) == 0);
    CHECK(b == true);
    CHECK(JsonGetInt(node, "missing", i) == 2);
    CHECK(JsonGetInt(node, "bool", i) == 3);

    new Node:obj;
    CHECK(JsonGetObject(node, "obj", obj) == 0);
    CHECK(JsonGetInt(obj, "inner", i) == 0);
    CHECK(i == -5);
    CHECK(JsonNodeType(obj) == JSON_NODE_OBJECT);

    new Node:a = JsonArray(JsonInt(1), JsonInt(2));
    new Node:c = a + JsonArray(JsonInt(3));
    JsonStringify(c, buf);
    CHECK(Same(buf, "[1,2,3]"));

    CHECK(JsonSetInt(node, "int", 43) == 0);
    CHECK(JsonSetFloat(node, "float", 2.5) == 0);
    CHECK(JsonSetBool(node, "bool", false) == 0);
    CHECK(JsonSetObject(node, "novo", JsonObject("x", JsonInt(1))) == 0);
    CHECK(JsonGetInt(node, "int", i) == 0);
    CHECK(i == 43);
    CHECK(JsonGetFloat(node, "float", f) == 0);
    CHECK(f == 2.5);
    CHECK(JsonGetBool(node, "bool", b) == 0);
    CHECK(b == false);
    CHECK(JsonGetObject(node, "novo", obj) == 0);
}

new gRoomRules[2][4] = {
    {1, 0, 30, 5},
    {0, 1, 60, 10}
};

TestArrays() {
    // JsonSetArray, built from a 2D array like a room-rules table.
    new Node:rules = JsonObject();
    new key[8];
    for (new i = 0; i < sizeof(gRoomRules); i++) {
        format(key, sizeof(key), "sala%d", i);
        new ret = JsonSetArray(rules, key, JsonArray(
            JsonInt(gRoomRules[i][0]),
            JsonInt(gRoomRules[i][1]),
            JsonInt(gRoomRules[i][2]),
            JsonInt(gRoomRules[i][3])
        ));
        CHECK(ret == 0);
    }
    new buf[512];
    JsonStringify(rules, buf);
    printf("[test] stringify: %s", buf);
    CHECK(Same(buf, "{\"sala0\":[1,0,30,5],\"sala1\":[0,1,60,10]}"));
    CHECK(JsonSetArray(rules, "x", JsonInt(1)) == 3);

    // JsonArrayAppend creating the array on the first call.
    new Node:payload = JsonObject("cidade", JsonString("São Paulo"));
    new Float:position[2] = {1.5, -2.25};
    for (new i = 0; i < 2; i++) {
        new ret = JsonArrayAppend(payload, "properties", JsonObject(
            "id", JsonInt(i),
            "name", JsonString("Casa ação"),
            "owned", JsonBool(i == 1),
            "x", JsonFloat(position[0]),
            "y", JsonFloat(position[1])
        ));
        CHECK(ret == 0);
    }
    JsonStringify(payload, buf);
    printf("[test] stringify: %s", buf);
    new const expected[] = "{\"cidade\":\"S\\u00e3o Paulo\",\"properties\":[{\"id\":0,\"name\":\"Casa a\\u00e7\\u00e3o\",\"owned\":false,\"x\":1.5,\"y\":-2.25},{\"id\":1,\"name\":\"Casa a\\u00e7\\u00e3o\",\"owned\":true,\"x\":1.5,\"y\":-2.25}]}";
    CHECK(Same(buf, expected));
    CHECK(JsonArrayAppend(payload, "cidade", JsonInt(1)) == 4);

    new Node:list, len;
    CHECK(JsonGetArray(payload, "properties", list) == 0);
    CHECK(JsonArrayLength(list, len) == 0);
    CHECK(len == 2);
}

TestScopeGC() {
    new Node:node = JsonObject("key", JsonString("value"));
    JsonToggleGC(node, false);
    ScopeNodeGC(node);
    new value[6];
    JsonGetString(node, "key", value);
    CHECK(Same(value, "value"));
    CHECK(JsonCleanup(node) == 0);
    CHECK(JsonCleanup(node) == 1);

    // Without JsonToggleGC the node dies with the function parameter.
    new Node:temp = JsonObject("key", JsonString("value"));
    ScopeNodeGC(temp);
    CHECK(JsonCleanup(temp, true) == 1);
}

ScopeNodeGC(Node:node) {
    #pragma unused node
}

TestUtf8Mode() {
    CHECK(JsonSetEncoding(JSON_ENCODING_UTF8) == 0);
    CHECK(JsonGetEncoding() == JSON_ENCODING_UTF8);

    new Node:node = JsonObject("k", JsonString("ação"));
    new buf[64];
    JsonStringify(node, buf);
    // In UTF-8 mode the output is raw UTF-8: 'ç' is the two bytes C3 A7.
    CHECK(buf[7] == 0xC3);
    CHECK(buf[8] == 0xA7);

    JsonSetEncoding(JSON_ENCODING_CP1252);
    new out[16];
    CHECK(JsonGetString(node, "k", out) == 0);
    CHECK(Same(out, "ação"));
}
