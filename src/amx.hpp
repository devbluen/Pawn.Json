/*
 *  Pawn.Json - JSON natives for SA-MP and open.mp
 *
 *  Include amx.h through this header: on Windows malloc.h must come first,
 *  otherwise amx.h's own alloca macro gets redefined by the CRT.
 */

#pragma once

#if defined(_WIN32)
	#include <malloc.h>
#endif
#include <amx/amx.h>
