/*
 *  Pawn.Json - JSON natives for SA-MP and open.mp
 *
 *  Text encoding between Pawn strings and JSON.
 *
 *  JSON is always stored as valid UTF-8 internally. Pawn strings are usually
 *  Windows-1252 (what the SA-MP client renders: "ação" is 0x61 0xE7 0xE3 0x6F),
 *  but scripts saved as UTF-8 send multi-byte sequences instead. Incoming text
 *  is therefore auto-detected: valid UTF-8 is kept, anything else is decoded
 *  as Windows-1252. Invalid input can never produce invalid UTF-8, which is
 *  which is what makes a JSON library throw and crash the server.
 */

#pragma once

#include <string>

namespace Encoding
{
enum class Mode : int
{
	// Strings returned to Pawn are Windows-1252; JsonStringify escapes every
	// non-ASCII character as \uXXXX so the JSON text is pure ASCII.
	Cp1252 = 0,
	// Strings returned to Pawn are raw UTF-8 bytes (one byte per cell).
	Utf8 = 1,
};

void setMode(Mode mode) noexcept;
Mode mode() noexcept;

void appendUtf8(std::string& out, unsigned int codepoint);

bool isValidUtf8(const std::string& bytes) noexcept;

// Windows-1252 bytes -> UTF-8.
std::string cp1252ToUtf8(const std::string& bytes);

// UTF-8 -> Windows-1252. Characters with no Windows-1252 form become '?'.
std::string utf8ToCp1252(const std::string& utf8);

// Raw Pawn bytes -> valid UTF-8 (auto-detection described above).
std::string fromPawn(const std::string& bytes);

// UTF-8 -> bytes for Pawn, according to the current mode.
std::string toPawn(const std::string& utf8);

// Largest prefix length <= maxBytes that does not split a UTF-8 sequence.
std::size_t utf8SafeCut(const std::string& utf8, std::size_t maxBytes) noexcept;
}
