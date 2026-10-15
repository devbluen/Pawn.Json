/*
 *  Pawn.Json - JSON natives for SA-MP and open.mp
 */

#include "log.hpp"
#include <cstdio>

namespace
{
Log::SampLogprintf sampLogger = nullptr;
Log::OmpPrinter ompPrinter = nullptr;
void* ompContext = nullptr;

void write(bool error, const char* format, va_list args)
{
	char message[1024];
	std::vsnprintf(message, sizeof(message), format, args);

	if (ompPrinter)
	{
		ompPrinter(ompContext, error, message);
	}
	else if (sampLogger)
	{
		sampLogger("[Pawn.Json] %s%s", error ? "ERROR: " : "", message);
	}
	else
	{
		std::fprintf(error ? stderr : stdout, "[Pawn.Json] %s%s\n", error ? "ERROR: " : "", message);
	}
}
}

namespace Log
{
void setSampLogger(SampLogprintf logger) noexcept
{
	sampLogger = logger;
}

void setOmpLogger(OmpPrinter printer, void* context) noexcept
{
	ompPrinter = printer;
	ompContext = context;
}

void clear() noexcept
{
	sampLogger = nullptr;
	ompPrinter = nullptr;
	ompContext = nullptr;
}

void info(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	write(false, format, args);
	va_end(args);
}

void error(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	write(true, format, args);
	va_end(args);
}
}
