/*
 *  Pawn.Json - JSON natives for SA-MP and open.mp
 */

#include "amx-api.hpp"
#include "encoding.hpp"
#include <plugincommon.h>

namespace
{
void* const* functionTable = nullptr;

template <typename Return, typename... Args>
Return invoke(std::size_t index, Return fallback, Args... args) noexcept
{
	if (!functionTable)
	{
		return fallback;
	}
	using Function = Return(AMXAPI*)(Args...);
	const auto function = reinterpret_cast<Function>(functionTable[index]);
	return function ? function(args...) : fallback;
}
}

namespace AmxApi
{
void setFunctionTable(void* const* table) noexcept
{
	functionTable = table;
}

bool available() noexcept
{
	return functionTable != nullptr;
}

int registerNatives(AMX* amx, const AMX_NATIVE_INFO* natives, int count) noexcept
{
	return invoke<int>(PLUGIN_AMX_EXPORT_Register, AMX_ERR_NOTFOUND, amx, natives, count);
}

cell* getAddr(AMX* amx, cell address, std::size_t* availableCells) noexcept
{
	if (!amx)
	{
		return nullptr;
	}

	cell* physical = nullptr;
	if (invoke<int>(PLUGIN_AMX_EXPORT_GetAddr, AMX_ERR_NOTFOUND, amx, address, &physical) != AMX_ERR_NONE || !physical)
	{
		return nullptr;
	}

	if (availableCells)
	{
		// amx_GetAddr accepts any address below the stack top, so that is the
		// hard limit for anything read or written from here.
		const cell top = amx->stp;
		*availableCells = (address >= 0 && address < top)
			? static_cast<std::size_t>(top - address) / sizeof(cell)
			: 0;
	}
	return physical;
}

bool readRawString(AMX* amx, cell address, std::string& out) noexcept
{
	out.clear();
	std::size_t available = 0;
	const cell* source = getAddr(amx, address, &available);
	if (!source || available == 0)
	{
		return false;
	}

	try
	{
		if (static_cast<ucell>(source[0]) > UNPACKEDMAX)
		{
			// Packed string: four characters per cell, most significant byte first.
			for (std::size_t i = 0; i < available; ++i)
			{
				const ucell value = static_cast<ucell>(source[i]);
				for (int shift = static_cast<int>(sizeof(cell) - 1) * 8; shift >= 0; shift -= 8)
				{
					const unsigned char byte = static_cast<unsigned char>((value >> shift) & 0xFF);
					if (byte == 0)
					{
						return true;
					}
					out.push_back(static_cast<char>(byte));
				}
			}
			return true;
		}

		for (std::size_t i = 0; i < available; ++i)
		{
			const cell value = source[i];
			if (value == 0)
			{
				return true;
			}
			if (value < 0)
			{
				// Sign-extended char (e.g. 'é' stored as -23).
				out.push_back(static_cast<char>(value & 0xFF));
			}
			else if (value <= 0xFF)
			{
				out.push_back(static_cast<char>(value));
			}
			else if (value <= 0x10FFFF)
			{
				Encoding::appendUtf8(out, static_cast<unsigned int>(value));
			}
			else
			{
				out.push_back('?');
			}
		}
		return true;
	}
	catch (...)
	{
		return false;
	}
}

int writeRawString(AMX* amx, cell address, const std::string& bytes, cell size) noexcept
{
	if (size <= 0)
	{
		return -1;
	}
	std::size_t available = 0;
	cell* dest = getAddr(amx, address, &available);
	if (!dest || available == 0)
	{
		return -1;
	}

	std::size_t capacity = static_cast<std::size_t>(size);
	if (capacity > available)
	{
		capacity = available;
	}

	const std::size_t count = bytes.size() < capacity - 1 ? bytes.size() : capacity - 1;
	for (std::size_t i = 0; i < count; ++i)
	{
		dest[i] = static_cast<cell>(static_cast<unsigned char>(bytes[i]));
	}
	dest[count] = 0;
	return static_cast<int>(count);
}

bool writeCell(AMX* amx, cell address, cell value) noexcept
{
	std::size_t available = 0;
	cell* dest = getAddr(amx, address, &available);
	if (!dest || available == 0)
	{
		return false;
	}
	*dest = value;
	return true;
}
}
