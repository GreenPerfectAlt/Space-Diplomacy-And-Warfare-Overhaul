#include "stdafx.h"
#include "cSpaceCombatMetrics.h"
#include <Spore-Mod-Utils/Include/SporeModUtils.h>

using namespace SporeModUtils;
using namespace Simulator;

cSpaceCombatMetrics::cSpaceCombatMetrics(ResourceKey spaceCombatKey)
{
	PropertyListPtr spaceCombatProp;
	PropManager.GetPropertyList(spaceCombatKey.instanceID, spaceCombatKey.groupID, spaceCombatProp);
	App::Property::GetArrayFloat(spaceCombatProp.get(), 0x033B0A90, healthBomber);
	App::Property::GetArrayFloat(spaceCombatProp.get(), 0x033B0A8D, healthFighter);
	App::Property::GetArrayFloat(spaceCombatProp.get(), 0x05D67A28, healthDefender);
	App::Property::GetArrayFloat(spaceCombatProp.get(), 0x057E2CFF, healthTurret);

	PropertyListPtr relationshipEffectsProp;
	PropManager.GetPropertyList(0x4e5855b9, 0x0568de14, relationshipEffectsProp);
	eastl::vector<int> defendersOutpost;
	eastl::vector<int> defendersColony;
	eastl::vector<int> defendersHomeworld;

	App::Property::GetArrayInt32(relationshipEffectsProp.get(), 0x067B69D0, defendersOutpost);
	App::Property::GetArrayInt32(relationshipEffectsProp.get(), 0x067B69EB, defendersColony);

	numDefendersInOutpost = !defendersOutpost.empty() ? defendersOutpost.front() : 1;
	numDefendersInColony = !defendersColony.empty() ? defendersColony.front() : 3;

	PropertyListPtr RaidWarProp;
	PropManager.GetPropertyList(0xbbae6b99, 0x02ae1a8e, RaidWarProp);

	int maxFighters;
	int minFighters;
	App::Property::GetInt32(RaidWarProp.get(), 0x0339BBC5, maxFighters);
	App::Property::GetInt32(RaidWarProp.get(), 0x0339BBC1, minFighters);

	numFightersPerBomber = (maxFighters + minFighters) / 2;

	PropertyListPtr terraformTuningProp;
	PropManager.GetPropertyList(0xa6d1bc70, 0x02ae0c7e, terraformTuningProp);

	int numBuildingsT1;
	int numBuildingsT2;
	int numBuildingsT3;
	App::Property::GetInt32(terraformTuningProp.get(), 0x04A5D473, numBuildingsT1);
	App::Property::GetInt32(terraformTuningProp.get(), 0x04A5D46D, numBuildingsT2);
	App::Property::GetInt32(terraformTuningProp.get(), 0x052F046E, numBuildingsT3);

	numTurretsInT1 = min(numBuildingsT1 / 2, 8);
	numTurretsInT2 = min(numBuildingsT2, 16);
	numTurretsInT3 = min((numBuildingsT3 / 2) * 3, 24);
}

cSpaceCombatMetrics::~cSpaceCombatMetrics()
{
}

// For internal use, do not modify.
int cSpaceCombatMetrics::AddRef()
{
	return DefaultRefCounted::AddRef();
}

// For internal use, do not modify.
int cSpaceCombatMetrics::Release()
{
	return DefaultRefCounted::Release();
}

// You can extend this function to return any other types your class implements.
void* cSpaceCombatMetrics::Cast(uint32_t type) const
{
	CLASS_CAST(Object);
	CLASS_CAST(cSpaceCombatMetrics);
	return nullptr;
}

float cSpaceCombatMetrics::GetBomberHealthByLevel(int empireLevel)
{
	if (healthBomber.empty()) return 1.0f;
	if (empireLevel < 0) return healthBomber.front();
	if (empireLevel >= int(healthBomber.size())) return healthBomber.back();
	return healthBomber[empireLevel];
}

float cSpaceCombatMetrics::GetFighterHealthByLevel(int empireLevel)
{
	if (healthFighter.empty()) return 1.0f;
	if (empireLevel < 0) return healthFighter.front();
	if (empireLevel >= int(healthFighter.size())) return healthFighter.back();
	return healthFighter[empireLevel];
}

float cSpaceCombatMetrics::GetDefenderHealthByLevel(int empireLevel)
{
	if (healthDefender.empty()) return 1.0f;
	if (empireLevel < 0) return healthDefender.front();
	if (empireLevel >= int(healthDefender.size())) return healthDefender.back();
	return healthDefender[empireLevel];
}

float cSpaceCombatMetrics::GetTurretHealthByLevel(int empireLevel)
{
	if (healthTurret.empty()) return 1.0f;
	if (empireLevel < 0) return healthTurret.front();
	if (empireLevel >= int(healthTurret.size())) return healthTurret.back();
	return healthTurret[empireLevel];
}

int cSpaceCombatMetrics::GetDefendersInOutpost()
{
	return numDefendersInOutpost;
}

int cSpaceCombatMetrics::GetDefendersInColony()
{
	return numDefendersInColony;
}

int cSpaceCombatMetrics::GetFightersPerBomber()
{
	return numFightersPerBomber;
}

int cSpaceCombatMetrics::GetTurretsInT1()
{
	return numTurretsInT1;
}

int cSpaceCombatMetrics::GetTurretsInT2()
{
	return numTurretsInT2;
}

int cSpaceCombatMetrics::GetTurretsInT3()
{
	return numTurretsInT3;
}

float cSpaceCombatMetrics::GetBomberHealthByEmpire(Simulator::cEmpire* empire)
{
	float baseHealth = GetBomberHealthByLevel(empire->GetWeaponryLevel());
	if (empire == nullptr) return baseHealth;

	// Динамический модификатор относительно уровня вооружения и агрессивности
	float levelFactor = static_cast<float>(empire->GetWeaponryLevel() + 1) / 5.0f;
	Simulator::Archetypes baseArchetype = ArchetypeUtils::GetBaseArchetype(empire->mArchetype);

	switch (baseArchetype) {
		case Archetypes::kArchetypeWarrior:
		case Archetypes::kArchetypeZealot:
		case Archetypes::kArchetypeGrob:
			return baseHealth * (1.0f + (0.25f * levelFactor));
		case Archetypes::kArchetypeScientist:
			return baseHealth * (1.0f + (0.10f * levelFactor));
		case Archetypes::kArchetypeBard:
		case Archetypes::kArchetypeDiplomat:
			return baseHealth * (1.0f - (0.20f * (1.0f - levelFactor * 0.5f)));
		default:
			return baseHealth;
	}
}

float cSpaceCombatMetrics::GetFighterHealthByEmpire(Simulator::cEmpire* empire)
{
	float baseHealth = GetFighterHealthByLevel(empire->GetWeaponryLevel());
	if (empire == nullptr) return baseHealth;

	float levelFactor = static_cast<float>(empire->GetWeaponryLevel() + 1) / 5.0f;
	Simulator::Archetypes baseArchetype = ArchetypeUtils::GetBaseArchetype(empire->mArchetype);

	switch (baseArchetype) {
		case Archetypes::kArchetypeWarrior:
		case Archetypes::kArchetypeZealot:
		case Archetypes::kArchetypeGrob:
			return baseHealth * (1.0f + (0.20f * levelFactor));
		case Archetypes::kArchetypeScientist:
			return baseHealth * (1.0f + (0.10f * levelFactor));
		case Archetypes::kArchetypeBard:
		case Archetypes::kArchetypeDiplomat:
			return baseHealth * (1.0f - (0.15f * (1.0f - levelFactor * 0.5f)));
		default:
			return baseHealth;
	}
}

float cSpaceCombatMetrics::GetDefenderHealthByEmpire(Simulator::cEmpire* empire)
{
	float baseHealth = GetDefenderHealthByLevel(empire->GetWeaponryLevel());
	if (empire == nullptr) return baseHealth;

	float levelFactor = static_cast<float>(empire->GetWeaponryLevel() + 1) / 5.0f;
	Simulator::Archetypes baseArchetype = ArchetypeUtils::GetBaseArchetype(empire->mArchetype);

	switch (baseArchetype) {
		case Archetypes::kArchetypeWarrior:
		case Archetypes::kArchetypeZealot:
		case Archetypes::kArchetypeGrob:
			return baseHealth * (1.0f + (0.25f * levelFactor));
		case Archetypes::kArchetypeScientist:
			return baseHealth * (1.0f + (0.10f * levelFactor));
		case Archetypes::kArchetypeBard:
		case Archetypes::kArchetypeDiplomat:
			return baseHealth * (1.0f - (0.20f * (1.0f - levelFactor * 0.5f)));
		default:
			return baseHealth;
	}
}

float cSpaceCombatMetrics::GetTurretHealthByEmpire(Simulator::cEmpire* empire)
{
	return GetTurretHealthByLevel(empire->GetWeaponryLevel());
}

int cSpaceCombatMetrics::GetDefendersInPlanet(Simulator::cPlanetRecord* planet)
{
	if (planet == nullptr) return GetDefendersInColony();
	if (planet->mType == PlanetType::T0) return GetDefendersInOutpost();

	int baseColonyDefenders = max(GetDefendersInColony(), 1);
	int tierMultiplier = (planet->mType == PlanetType::T3) ? 3 : ((planet->mType == PlanetType::T2) ? 2 : 1);

	cEmpire* empire = nullptr;
	if (planet->GetStarRecord() != nullptr) {
		empire = StarManager.GetEmpire(planet->GetStarRecord()->mEmpireID);
	}

	float archetypeScale = 1.0f;
	if (empire != nullptr) {
		Simulator::Archetypes baseArchetype = ArchetypeUtils::GetBaseArchetype(empire->mArchetype);
		switch (baseArchetype) {
			case Archetypes::kArchetypeWarrior:
			case Archetypes::kArchetypeZealot:
			case Archetypes::kArchetypeGrob:
				archetypeScale = 1.5f;
				break;
			case Archetypes::kArchetypeScientist:
			case Archetypes::kArchetypeTrader:
				archetypeScale = 1.15f;
				break;
			case Archetypes::kArchetypeEcologist:
				archetypeScale = 1.0f;
				break;
			case Archetypes::kArchetypeShaman:
			case Archetypes::kArchetypeBard:
			case Archetypes::kArchetypeDiplomat:
			default:
				archetypeScale = 0.8f;
				break;
		}
	}

	int baseDefenders = static_cast<int>(round(static_cast<float>(baseColonyDefenders * tierMultiplier) * archetypeScale));

	// Поддержка Unlimited-Colony при нахождении в системе
	if (GetCurrentContext() == SpaceContext::Planet && planet == GetActivePlanetRecord()) {
		int cityCount = 0;
		const auto& cities = Simulator::GetData<cCity>();
		for (const auto& city : cities) {
			if (city) cityCount++;
		}
		int standardCities = (planet->mType == PlanetType::T3) ? 3 : ((planet->mType == PlanetType::T2) ? 2 : 1);
		if (cityCount > standardCities) {
			baseDefenders += (cityCount - standardCities) * baseColonyDefenders;
		}
	}

	return max(baseDefenders, 1);
}

int cSpaceCombatMetrics::GetTurretsInPlanet(Simulator::cPlanetRecord* planet)
{
	if (planet == nullptr) return 0;

	int baseTurrets = 0;
	int standardCities = 1;
	switch (planet->mType) {
		case(PlanetType::T1):
			baseTurrets = GetTurretsInT1();
			standardCities = 1;
			break;
		case(PlanetType::T2):
			baseTurrets = GetTurretsInT2();
			standardCities = 2;
			break;
		case(PlanetType::T3):
			baseTurrets = GetTurretsInT3();
			standardCities = 3;
			break;
		default:
			return 0;
	}

	if (GetCurrentContext() == SpaceContext::Planet && planet == GetActivePlanetRecord()) {
		int actualTurrets = 0;
		int cityCount = 0;
		const auto& cities = Simulator::GetData<cCity>();
		for (const auto& city : cities) {
			if (city) {
				cityCount++;
				actualTurrets += static_cast<int>(city->mTurrets.size());
			}
		}
		if (cityCount > standardCities && actualTurrets > 0) {
			baseTurrets = actualTurrets;
		}
	}

	return baseTurrets;
}
