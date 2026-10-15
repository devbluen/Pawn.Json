# Pawn.Json

JSON plugin for **SA-MP** and **open.mp** with **full support for accented
text**.

- **One binary for both servers.** The same `PawnJson.dll`/`.so` loads as an
  open.mp component and as a classic SA-MP plugin.
- **Accents never crash the server.** `"ação"`, `"São Paulo"` and `"€"` work
  when building payloads, parsing and reading values.
- **No runtime dependencies.** No cpprestsdk, Boost, OpenSSL or Visual C++
  redistributable.

## How accented text is handled

Pawn strings usually reach a plugin as Windows-1252 bytes (`é` is the single
byte `0xE9`), because that is how most `.pwn` files are saved and what the
SA-MP client renders. A lone `0xE9` is not valid UTF-8. A JSON library that
treats these bytes as UTF-8 throws an exception, and if nothing catches it,
the server crashes.

Pawn.Json converts text on the way in and on the way out:

| Direction | What happens |
|---|---|
| Pawn → JSON | Strings that are already valid UTF-8 are kept. Anything else is read as Windows-1252. The `.pwn` file encoding does not matter. |
| JSON → Pawn (`JsonGetString`, ...) | Text comes back as Windows-1252 by default, so accents show correctly in `SendClientMessage`, dialogs and textdraws. |
| `JsonStringify` | By default, every non-ASCII character is escaped (`"ação"`). The output is valid JSON for any HTTP API, whatever carries it. |

No C++ exception can leave a native. Errors become return codes plus a log
message.

## Installation

Get `PawnJson.dll` (Windows) or `PawnJson.so` (Linux) and `PawnJson.inc`. A
package built with `build.ps1` already uses the folder layout of each server.

### open.mp

1. Copy `PawnJson.dll`/`PawnJson.so` to `components/`.
2. Copy `PawnJson.inc` to `qawno/include/`.

### SA-MP

1. Copy `PawnJson.dll`/`PawnJson.so` to `plugins/`.
2. In `server.cfg`:
   ```
   plugins PawnJson        # Windows
   plugins PawnJson.so     # Linux
   ```
3. Copy `PawnJson.inc` to `pawno/include/`.

> open.mp also accepts the binary in `plugins/` (through `legacy_plugins`),
> but `components/` is recommended. Do not put it in both places.

## Usage

```pawn
#include <open.mp>      // or <a_samp>
#include <PawnJson>

public OnGameModeInit()
{
    new Node:payload = JsonObject(
        "content", JsonString("Olá! O servidor está online."),
        "embeds", JsonArray(
            JsonObject(
                "title", JsonString("Situação"),
                "description", JsonString("Jogadores: 10 - Localização: São Paulo")
            )
        )
    );

    new body[1024];
    JsonStringify(payload, body);
    // body = {"content":"Olá! O servidor está online.","embeds":[...]}
    return 1;
}
```

Reading a response:

```pawn
new Node:root;
if (JsonParse(response, root) == 0)
{
    new name[64], level, Float:money;
    JsonGetString(root, "name", name);      // "João" in Windows-1252
    JsonGetInt(root, "level", level);
    JsonGetFloat(root, "money", money);

    new Node:items, Node:item, count;
    JsonGetArray(root, "items", items);
    JsonArrayLength(items, count);
    for (new i = 0; i < count; i++)
    {
        new itemName[32];
        JsonArrayObject(items, i, item);
        JsonGetNodeString(item, itemName);  // releases `item`
    }
}
```

Building lists step by step:

```pawn
new Node:payload = JsonObject("player", JsonString("João"));

// The first JsonArrayAppend creates the "properties" array.
for (new i = 0; i < count; i++)
{
    JsonArrayAppend(payload, "properties", JsonObject(
        "id", JsonInt(i),
        "city", JsonString("São Paulo"),
        "price", JsonInt(150000)
    ));
}

// Replaces (or creates) a whole array at once.
JsonSetArray(payload, "rules", JsonArray(JsonInt(1), JsonInt(0), JsonInt(1)));
// {"player":"João","properties":[{...},{...}],"rules":[1,0,1]}
```

Every native, with its return codes, is documented in
[PawnJson.inc](PawnJson.inc).

### Nodes and memory

- Each value lives inside the plugin. The script holds a `Node:` handle to it.
- A node passed as a value to `JsonObject`, `JsonArray`, `JsonAppend`,
  `JsonSetObject`, `JsonSetArray` or `JsonArrayAppend` is moved into the new
  value, and its old handle stops existing.
- When a `Node:` variable goes out of scope, its node is released by
  `operator~`. This includes function parameters.
- Use `JsonToggleGC(node, false)` to keep a node alive across functions, and
  release it later with `JsonCleanup(node)`.
- Any node still left is released when the script that created it unloads, so
  a forgotten handle cannot leak across gamemode restarts (GMX).

### Output encoding

```pawn
JsonSetEncoding(JSON_ENCODING_CP1252); // default: Windows-1252 strings, ASCII Stringify with \uXXXX
JsonSetEncoding(JSON_ENCODING_UTF8);   // raw UTF-8 strings and Stringify (one byte per cell)
```

Use `JSON_ENCODING_UTF8` when the text goes to something that expects UTF-8,
such as a file, a socket or another plugin. The setting is global, so the
gamemode and all filterscripts share it. Input is always auto-detected.

## Building

### `build.ps1` (Windows and Linux, from Windows)

One script builds both platforms on a Windows machine:

- **Windows** is compiled with Visual Studio, then the unit tests run.
- **Linux x86** is cross-compiled with [Zig](https://ziglang.org/) (clang plus
  a bundled glibc/libc++). No Linux machine, WSL or Docker is needed.
  - On first use, the script downloads Zig (and Ninja, if it is not on
    `PATH`) into `.tools/` and checks their SHA-256.
  - The resulting `.so` runs on any Linux with glibc 2.17 or newer.

Requirements:
- Visual Studio 2019, 2022 or 2026 with "Desktop development with C++".
- CMake 3.19+. If CMake is not on `PATH`, the copy bundled with Visual Studio
  is used.

```powershell
.\build.ps1                        # Windows + Linux, tests, dist folders and zips
.\build.ps1 -Target windows        # or -Target linux
.\build.ps1 -Version 1.1.0         # embed another version in the binaries and the include
.\build.ps1 -Clean                 # wipe the build folders first
.\build.ps1 -SkipTests -NoZip
```

If script execution is disabled on the machine, run
`powershell -ExecutionPolicy Bypass -File .\build.ps1`.

Result:

```
dist/
  PawnJson-<version>-windows-x86/
    open.mp/components/PawnJson.dll
    open.mp/qawno/include/PawnJson.inc
    samp/plugins/PawnJson.dll
    samp/pawno/include/PawnJson.inc
    README.md
  PawnJson-<version>-windows-x86.zip
  PawnJson-<version>-linux-x86/         (same layout with PawnJson.so)
  PawnJson-<version>-linux-x86.zip
```

The `open.mp/` and `samp/` folders mirror each server's directory layout, so
their contents can be copied straight into the server folder.

### Native Linux / manual CMake

CMake downloads the dependencies (open.mp SDK, SA-MP plugin SDK and
nlohmann/json) automatically.

```
# On Linux (needs gcc-multilib g++-multilib)
cmake --preset linux-x86
cmake --build --preset linux-x86
ctest --preset linux-x86

# On Windows
cmake --preset windows-x86
cmake --build --preset windows-x86
ctest --preset windows-x86
```

The binary is written to `build/<preset>/plugins/`.

### Releasing a new version

The GitHub Actions workflow (`.github/workflows/build.yml`) builds every push
and pull request. To publish a release:

1. Open **Actions → Build Pawn.Json → Run workflow**.
2. Fill in **version**, for example `1.1.0`. If you leave it empty, the version
   in `CMakeLists.txt` is used.
3. Check **publish_release** and run.

The workflow then does the following:
1. Builds Windows and Linux with `build.ps1`.
2. Runs the unit tests on a real 32-bit Linux build and checks the `.so`
   exports.
3. Only if everything passes, creates the release `v<version>` with
   `PawnJson-<version>-windows-x86.zip` and
   `PawnJson-<version>-linux-x86.zip` attached, plus auto-generated notes.

The release is refused if the tag already exists. A run without
**publish_release** only builds and uploads the zips as workflow artifacts.

### Tests

- `tests/tests.cpp` runs unit tests for every native on top of a simulated AMX.
  It covers encoding, parsing, builders, GC and invalid parameters.
- `test/test.pwn` is an integration gamemode, **saved as Windows-1252 on
  purpose**. Compile it, start the server and look for `RESULT` in the log. The
  server shuts itself down at the end.

## How the hybrid plugin works

The same binary exports both entry points:

- `ComponentEntryPoint`: open.mp loads the binary as a component. It hooks into
  `IPawnComponent` and registers the natives on every script (`onAmxLoad`).
- `Supports`, `Load`, `Unload`, `AmxLoad` and `AmxUnload`: SA-MP (or open.mp's
  legacy loader) loads it as a classic plugin.

Natives only touch AMX memory through the AMX export table. Both servers
provide that table with the same indices, so the native code is identical in
both modes. The approach comes from
[omp-Discord-Bridge](https://github.com/devbluen/omp-Discord-Bridge).

## Third-party code

- [nlohmann/json](https://github.com/nlohmann/json) (MIT).
- [open.mp SDK](https://github.com/openmultiplayer/open.mp-sdk) and
  [samp-plugin-sdk](https://github.com/maddinat0r/samp-plugin-sdk).
