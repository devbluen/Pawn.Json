/*
 *  Pawn.Json - JSON natives for SA-MP and open.mp
 *
 *  SA-MP plugin entry points (also used by open.mp's legacy plugin loader
 *  when the binary is placed in `plugins/`).
 */

#include "amx.hpp"
#include "amx-api.hpp"
#include "json-pool.hpp"
#include "log.hpp"
#include "natives.hpp"
#include "version.hpp"
#include <plugincommon.h>

PLUGIN_EXPORT unsigned int PLUGIN_CALL Supports()
{
	return SUPPORTS_VERSION | SUPPORTS_AMX_NATIVES;
}

PLUGIN_EXPORT bool PLUGIN_CALL Load(void** data)
{
	if (!data)
	{
		return false;
	}
	AmxApi::setFunctionTable(static_cast<void* const*>(data[PLUGIN_DATA_AMX_EXPORTS]));
	Log::setSampLogger(reinterpret_cast<Log::SampLogprintf>(data[PLUGIN_DATA_LOGPRINTF]));
	Log::info("v%s loaded as SA-MP plugin", PAWN_JSON_VERSION);
	return true;
}

PLUGIN_EXPORT void PLUGIN_CALL Unload()
{
	JsonPool::clear();
	Log::info("plugin unloaded");
	Log::clear();
	AmxApi::setFunctionTable(nullptr);
}

PLUGIN_EXPORT int PLUGIN_CALL AmxLoad(AMX* amx)
{
	int count = 0;
	const AMX_NATIVE_INFO* natives = Natives::list(count);
	// amx_Register reports AMX_ERR_NOTFOUND while other plugins' natives are
	// still unresolved, which is normal at this point.
	AmxApi::registerNatives(amx, natives, count);
	return AMX_ERR_NONE;
}

PLUGIN_EXPORT int PLUGIN_CALL AmxUnload(AMX* amx)
{
	JsonPool::eraseOwnedBy(amx);
	return AMX_ERR_NONE;
}
