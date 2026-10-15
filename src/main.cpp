/*
 *  Pawn.Json - JSON natives for SA-MP and open.mp
 *
 *  open.mp entry point. The SA-MP entry points live in samp-main.cpp; both are
 *  exported from the same binary, so it works in either server.
 */

#include "omp-component.hpp"

COMPONENT_ENTRY_POINT()
{
	return new PawnJsonComponent();
}
