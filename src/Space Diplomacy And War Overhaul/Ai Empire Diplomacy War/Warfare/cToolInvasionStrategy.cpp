#include "stdafx.h"
#include "cToolInvasionStrategy.h"
#include <Spore-Mod-Utils/Include/SporeModUtils.h>
#include <cmath>

using namespace SporeModUtils;
using namespace Simulator;

cToolInvasionStrategy::cToolInvasionStrategy()
{
	simulationValidator = nullptr;
	warfareStrengthAnalyzer = nullptr;
	warfareEventDispatcher = nullptr;
	dependenciesInjected = false;

	PropertyListPtr toolInvasionProp;
	PropManager.GetPropertyList(0x3b04cb56, 0x30608f0b, toolInvasionProp);
	App::Property::GetInt32(toolInvasionProp.get(), 0x10609EDF, useCost);
}

cToolInvasionStrategy::~cToolInvasionStrategy()
{
}

bool cToolInvasionStrategy::OnSelect(Simulator::cSpaceToolData* pTool) {
	if (!Update(pTool, true)) return false;

	cEmpire* playerEmpire = GetPlayerEmpire();
	SpaceContext context = GetCurrentContext();
	bool inGalaxy = context == SpaceContext::Galaxy;
	bool inPlanetOrSystem = context == SpaceContext::Planet || context == SpaceContext::SolarSystem;
	int beforeUsingToolMoney = playerEmpire->mEmpireMoney;

	// OnSelect списывает рассчитанную стоимость
	if (!cToolStrategy::OnSelect(pTool) || beforeUsingToolMoney == playerEmpire->mEmpireMoney) return false;

	if (inGalaxy && simulationValidator->ValidStar(GetActiveStarRecord())) {
		for (cPlanetRecordPtr planet : GetActiveStarRecord()->GetPlanetRecords()) {
			if (simulationValidator->ValidPlanet(planet.get()) && planet->GetTechLevel() == TechLevel::Empire) {
				int bombers = warfareStrengthAnalyzer->GetBomberForceForPlanet(playerEmpire, planet.get());
				warfareEventDispatcher->DispatchPlanetAttackedEvent(
					playerEmpire,
					planet.get(),
					bombers
				);
			}
		}
	}
	else if (inPlanetOrSystem && simulationValidator->ValidPlanet(GetActivePlanetRecord())) {
		int bombers = warfareStrengthAnalyzer->GetBomberForceForPlanet(playerEmpire, GetActivePlanetRecord());
		warfareEventDispatcher->DispatchPlanetAttackedEvent(
			playerEmpire,
			GetActivePlanetRecord(),
			bombers
		);
	}

	return true;
}

bool cToolInvasionStrategy::Update(Simulator::cSpaceToolData* pTool, bool showErrors, const char16_t** ppFailText) {
	if (!dependenciesInjected || !cToolStrategy::Update(pTool, true, ppFailText))
		return false;

	SpaceContext context = GetCurrentContext();
	bool inGalaxy = context == SpaceContext::Galaxy;
	bool inPlanetOrSystem = context == SpaceContext::Planet || context == SpaceContext::SolarSystem;

	if (inGalaxy && !simulationValidator->ValidStar(GetActiveStarRecord()))
		return false;

	if (inPlanetOrSystem && !simulationValidator->ValidPlanet(GetActivePlanetRecord()))
		return false;

	cEmpire* playerEmpire = GetPlayerEmpire();
	cEmpire* targetEmpire = StarManager.GetEmpire(GetActiveStarRecord()->mEmpireID);

	if (!simulationValidator->ValidEmpire(targetEmpire, false, true)) return false;
	if (!DiplomacyUtils::War(playerEmpire, targetEmpire)) return false;

	if (inGalaxy) {
		for (cPlanetRecordPtr planet : GetActiveStarRecord()->GetPlanetRecords()) {
			if (MissionManager.ThereIsEventInPlanet(planet.get()))
				return false;
		}
	}
	else if (inPlanetOrSystem && MissionManager.ThereIsEventInPlanet(GetActivePlanetRecord())) {
		return false;
	}

	return true;
}

int cToolInvasionStrategy::ProcessCost(int baseUseCost) {
	if (warfareStrengthAnalyzer == nullptr) {
		return 0;
	}

	int bombersNeeded = 0;
	SpaceContext context = GetCurrentContext();
	bool inGalaxy = context == SpaceContext::Galaxy;
	bool inPlanetOrSystem = context == SpaceContext::Planet || context == SpaceContext::SolarSystem;

	cEmpire* playerEmpire = GetPlayerEmpire();
	cStarRecord* targetStar = GetActiveStarRecord();

	if (inGalaxy && simulationValidator->ValidStar(targetStar)) {
		bombersNeeded = warfareStrengthAnalyzer->GetBomberForceForSystem(playerEmpire, targetStar);
	}
	else if (inPlanetOrSystem && simulationValidator->ValidPlanet(GetActivePlanetRecord())) {
		bombersNeeded = warfareStrengthAnalyzer->GetBomberForceForPlanet(playerEmpire, GetActivePlanetRecord());
	}

	if (bombersNeeded <= 0) {
		return 0;
	}

	// 1. Динамический расчет скидки/наценки по архетипу через конфиг сил
	float archetypeCostMultiplier = 1.0f;
	if (playerEmpire != nullptr && warfareStrengthAnalyzer->archetypeStrengthConfig != nullptr) {
		Simulator::Archetypes baseArchetype = ArchetypeUtils::GetBaseArchetype(playerEmpire->mArchetype);
		float archetypeStrength = warfareStrengthAnalyzer->archetypeStrengthConfig->GetArchetypeBaseStrength(playerEmpire->mArchetype);

		if (baseArchetype == Archetypes::kArchetypeTrader) {
			// Торговцы получают фиксированную финансовую субсидию от своей доктрины
			archetypeCostMultiplier = 0.75f;
		}
		else if (archetypeStrength > 1.0f) {
			// Воинственные цивилизации получают пропорциональную скидку за счет подготовки
			archetypeCostMultiplier = 1.0f / archetypeStrength;
		}
		else if (archetypeStrength < 1.0f && archetypeStrength > 0.0f) {
			// Мирные цивилизации платят пропорциональную наценку за наем частных эскадр
			archetypeCostMultiplier = 1.0f / archetypeStrength;
		}
	}

	// 2. Динамическая логистика расстояния от родного мира через радиус вооружения
	float distancePenaltyMultiplier = 1.0f;
	if (playerEmpire != nullptr && targetStar != nullptr && warfareStrengthAnalyzer->warfareConfig != nullptr) {
		cStarRecord* homeworld = EmpireUtils::GetHomeStar(playerEmpire);
		if (homeworld != nullptr) {
			float distance = StarUtils::GetDistanceBetweenStars(homeworld, targetStar);
			// Базовый радиус снабжения равен дальности действия орудийных систем империи
			float baseSupplyRange = warfareStrengthAnalyzer->warfareConfig->GetWarfareRange(playerEmpire->GetWeaponryLevel());
			
			if (distance > baseSupplyRange && baseSupplyRange > 0.0f) {
				// Динамический коэффициент удорожания переброски от настройки штрафа мульти-атак
				float stepPenalty = warfareStrengthAnalyzer->warfareConfig->GetBombersMultiPlanetPenaltyFactor() * 0.1f;
				distancePenaltyMultiplier += (distance - baseSupplyRange) * stepPenalty;
			}
		}
	}

	float finalUnitCost = static_cast<float>(baseUseCost) * archetypeCostMultiplier * distancePenaltyMultiplier;
	int totalCost = static_cast<int>(round(finalUnitCost * static_cast<float>(bombersNeeded)));

	return max(totalCost, baseUseCost);
}

void cToolInvasionStrategy::InjectDependencies(cSimulationValidator* simulationValidator, cWarfareStrengthAnalyzer* warfareStrengthAnalyzer, cWarfareEventDispatcher* warfareEventDispatcher)
{
	this->simulationValidator = simulationValidator;
	this->warfareStrengthAnalyzer = warfareStrengthAnalyzer;
	this->warfareEventDispatcher = warfareEventDispatcher;
	dependenciesInjected = true;
}

void cToolInvasionStrategy::ResetDependencies()
{
	this->simulationValidator.reset();
	this->warfareStrengthAnalyzer.reset();
	this->warfareEventDispatcher.reset();
	dependenciesInjected = false;
}
