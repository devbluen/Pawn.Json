/*
 *  Pawn.Json - JSON natives for SA-MP and open.mp
 */

#include "encoding.hpp"

namespace
{
Encoding::Mode currentMode = Encoding::Mode::Cp1252;

// Windows-1252 0x80..0x9F. Undefined slots (0x81, 0x8D, 0x8F, 0x90, 0x9D) map
// to the matching C1 control, like Windows' own MultiByteToWideChar does.
constexpr unsigned int kCp1252High[32] = {
	0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
	0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
	0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
	0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
};

// Decodes one UTF-8 sequence starting at `i`. Returns its length, or 0 when
// the sequence is invalid (overlong, surrogate, out of range, truncated).
std::size_t decodeUtf8(const std::string& s, std::size_t i, unsigned int& cp) noexcept
{
	const auto byte = [&s](std::size_t at) { return static_cast<unsigned char>(s[at]); };
	const unsigned char lead = byte(i);
	const std::size_t remaining = s.size() - i;

	if (lead < 0x80)
	{
		cp = lead;
		return 1;
	}

	std::size_t length;
	unsigned int min;
	if ((lead & 0xE0) == 0xC0)
	{
		length = 2;
		cp = lead & 0x1F;
		min = 0x80;
	}
	else if ((lead & 0xF0) == 0xE0)
	{
		length = 3;
		cp = lead & 0x0F;
		min = 0x800;
	}
	else if ((lead & 0xF8) == 0xF0)
	{
		length = 4;
		cp = lead & 0x07;
		min = 0x10000;
	}
	else
	{
		return 0;
	}

	if (remaining < length)
	{
		return 0;
	}
	for (std::size_t k = 1; k < length; ++k)
	{
		const unsigned char next = byte(i + k);
		if ((next & 0xC0) != 0x80)
		{
			return 0;
		}
		cp = (cp << 6) | (next & 0x3F);
	}
	if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
	{
		return 0;
	}
	return length;
}

int unicodeToCp1252(unsigned int cp) noexcept
{
	if (cp < 0x80 || (cp >= 0xA0 && cp <= 0xFF))
	{
		return static_cast<int>(cp);
	}
	for (int k = 0; k < 32; ++k)
	{
		if (kCp1252High[k] == cp)
		{
			return 0x80 + k;
		}
	}
	return -1;
}
}

namespace Encoding
{
void appendUtf8(std::string& out, unsigned int cp)
{
	if (cp < 0x80)
	{
		out.push_back(static_cast<char>(cp));
	}
	else if (cp < 0x800)
	{
		out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	}
	else if (cp < 0x10000)
	{
		out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	}
	else
	{
		out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	}
}

void setMode(Mode mode) noexcept
{
	currentMode = mode;
}

Mode mode() noexcept
{
	return currentMode;
}

bool isValidUtf8(const std::string& bytes) noexcept
{
	std::size_t i = 0;
	unsigned int cp = 0;
	while (i < bytes.size())
	{
		const std::size_t length = decodeUtf8(bytes, i, cp);
		if (length == 0)
		{
			return false;
		}
		i += length;
	}
	return true;
}

std::string cp1252ToUtf8(const std::string& bytes)
{
	std::string out;
	out.reserve(bytes.size() + bytes.size() / 2);
	for (const char c : bytes)
	{
		const unsigned char b = static_cast<unsigned char>(c);
		if (b < 0x80)
		{
			out.push_back(c);
		}
		else if (b < 0xA0)
		{
			appendUtf8(out, kCp1252High[b - 0x80]);
		}
		else
		{
			appendUtf8(out, b);
		}
	}
	return out;
}

std::string utf8ToCp1252(const std::string& utf8)
{
	std::string out;
	out.reserve(utf8.size());
	std::size_t i = 0;
	unsigned int cp = 0;
	while (i < utf8.size())
	{
		const std::size_t length = decodeUtf8(utf8, i, cp);
		if (length == 0)
		{
			out.push_back('?');
			++i;
			continue;
		}
		const int mapped = unicodeToCp1252(cp);
		out.push_back(mapped < 0 ? '?' : static_cast<char>(mapped));
		i += length;
	}
	return out;
}

std::string fromPawn(const std::string& bytes)
{
	bool ascii = true;
	for (const char c : bytes)
	{
		if (static_cast<unsigned char>(c) >= 0x80)
		{
			ascii = false;
			break;
		}
	}
	if (ascii || isValidUtf8(bytes))
	{
		return bytes;
	}
	return cp1252ToUtf8(bytes);
}

std::string toPawn(const std::string& utf8)
{
	if (currentMode == Mode::Utf8)
	{
		// Stored text is always valid UTF-8, nothing to convert.
		return utf8;
	}
	return utf8ToCp1252(utf8);
}

std::size_t utf8SafeCut(const std::string& utf8, std::size_t maxBytes) noexcept
{
	if (maxBytes >= utf8.size())
	{
		return utf8.size();
	}
	std::size_t cut = maxBytes;
	// Step back over continuation bytes so the cut lands on a sequence start.
	while (cut > 0 && (static_cast<unsigned char>(utf8[cut]) & 0xC0) == 0x80)
	{
		--cut;
	}
	return cut;
}
}
