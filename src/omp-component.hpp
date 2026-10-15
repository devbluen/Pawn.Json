/*
 *  Pawn.Json - JSON natives for SA-MP and open.mp
 *
 *  open.mp component. Loaded from `components/`, it registers the natives on
 *  every script through the Pawn component's event dispatcher.
 */

#pragma once

#include "amx.hpp"
#include <sdk.hpp>
#include <Server/Components/Pawn/pawn.hpp>

class PawnJsonComponent final
	: public IComponent
	, public PawnEventHandler
{
private:
	ICore* core_ = nullptr;
	IPawnComponent* pawn_ = nullptr;

public:
	PROVIDE_UID(0x4A50E7C1B2D93F68);

	StringView componentName() const override;
	SemanticVersion componentVersion() const override;
	void onLoad(ICore* core) override;
	void onInit(IComponentList* components) override;
	void onFree(IComponent* component) override;
	void free() override;
	void reset() override;

	void onAmxLoad(IPawnScript& script) override;
	void onAmxUnload(IPawnScript& script) override;
};
