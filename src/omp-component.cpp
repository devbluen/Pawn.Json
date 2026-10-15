/*
 *  Pawn.Json - JSON natives for SA-MP and open.mp
 */

#include "omp-component.hpp"
#include "amx-api.hpp"
#include "json-pool.hpp"
#include "log.hpp"
#include "natives.hpp"
#include "version.hpp"

namespace
{
void printToCore(void* context, bool error, const char* message)
{
	ICore* core = static_cast<ICore*>(context);
	core->logLn(error ? LogLevel::Error : LogLevel::Message, "[Pawn.Json] %s", message);
}
}

StringView PawnJsonComponent::componentName() const
{
	return "Pawn.Json";
}

SemanticVersion PawnJsonComponent::componentVersion() const
{
	return SemanticVersion(PAWN_JSON_VERSION_MAJOR, PAWN_JSON_VERSION_MINOR, PAWN_JSON_VERSION_PATCH, 0);
}

void PawnJsonComponent::onLoad(ICore* core)
{
	core_ = core;
	if (core_)
	{
		Log::setOmpLogger(printToCore, core_);
	}
	Log::info("v%s loaded as open.mp component", PAWN_JSON_VERSION);
}

void PawnJsonComponent::onInit(IComponentList* components)
{
	pawn_ = components ? components->queryComponent<IPawnComponent>() : nullptr;
	if (!pawn_)
	{
		Log::error("Pawn component not found, natives will not be available");
		return;
	}
	AmxApi::setFunctionTable(pawn_->getAmxFunctions().data());
	pawn_->getEventDispatcher().addEventHandler(this);
}

void PawnJsonComponent::onFree(IComponent* component)
{
	if (pawn_ && component == pawn_)
	{
		pawn_->getEventDispatcher().removeEventHandler(this);
		pawn_ = nullptr;
		AmxApi::setFunctionTable(nullptr);
		JsonPool::clear();
	}
}

void PawnJsonComponent::free()
{
	if (pawn_)
	{
		pawn_->getEventDispatcher().removeEventHandler(this);
		pawn_ = nullptr;
	}
	AmxApi::setFunctionTable(nullptr);
	JsonPool::clear();
	Log::clear();
	delete this;
}

void PawnJsonComponent::reset()
{
	// Nodes are owned by scripts and released in onAmxUnload.
}

void PawnJsonComponent::onAmxLoad(IPawnScript& script)
{
	int count = 0;
	const AMX_NATIVE_INFO* natives = Natives::list(count);
	script.Register(natives, count);
}

void PawnJsonComponent::onAmxUnload(IPawnScript& script)
{
	JsonPool::eraseOwnedBy(script.GetAMX());
}
