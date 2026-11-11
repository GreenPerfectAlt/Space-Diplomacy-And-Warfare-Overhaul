#include "stdafx.h"
#include "cSpaceCombatMetrics.h"
#include <Spore-Mod-Utils/Include/SporeModUtils.h>
#include <Spore/Simulator/cCity.h>
#include <cmath>

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

	App::Property::GetArrayInt32(relationshipEffectsProp.get(), 0x067B69D0, defendersOutpost);
	App::Property::GetArrayInt32(relationshipEffectsProp.get(), 0x067B69EB, defendersColony);

	numDefendersInOutpost = !defendersOutpost.empty() ? defendersOutpost.front() : 1;
	numDefendersInColony = !defendersColony.empty() ? defendersColony.front() : 3;

	PropertyListPtr RaidWarProp;
	PropManager.GetPropertyList(0xbbae6b99, 0x02ae1a8e, RaidWarProp);

	int maxFighters = 2;
	int minFighters = 2;
	App::Property::GetInt32(RaidWarProp.get(), 0x0339BBC5, maxFighters);
	App::Property::GetInt32(RaidWarProp.get(), 0x0339BBC1, minFighters);

	numFightersPerBomber = max((maxFighters + minFighters) / 2, 1);

	PropertyListPtr terraformTuningProp;
	PropManager.GetPropertyList(0xa6d1bc70, 0x02ae0c7e, terraformTuningProp);

	int numBuildingsT1 = 4;
	int numBuildingsT2 = 8;
	int numBuildingsT3 = 12;
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
	if (empire == nullptr) {
		return GetBomberHealthByLevel(0);
	}

	int weaponLevel = empire->GetWeaponryLevel();
	float baseHealth = GetBomberHealthByLevel(weaponLevel);
	float levelFactor = static_cast<float>(weaponLevel + 1) / 5.0f;
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
	if (empire == nullptr) {
		return GetFighterHealthByLevel(0);
	}

	int weaponLevel = empire->GetWeaponryLevel();
	float baseHealth = GetFighterHealthByLevel(weaponLevel);
	float levelFactor = static_cast<float>(weaponLevel + 1) / 5.0f;
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
	if (empire == nullptr) {
		return GetDefenderHealthByLevel(0);
	}

	// Синхронизация оценки ХП защитников игрока с модом Defensive Ships In Player Colonies
	if (empire == GetPlayerEmpire()) {
		PropertyListPtr propListFleet;
		if (PropManager.GetPropertyList(id(u"FleetConfig"), 0x02ae0c7e, propListFleet) && propListFleet != nullptr) {
			int weaponryRank = max(empire->GetWeaponryLevel(), 1);
			int totalStars = static_cast<int>(empire->mStars.size());
			float imperialIndustrialBonus = min(static_cast<float>(totalStars) * 8.0f, 400.0f);
			float dynamicBaseHealth = 450.0f + (static_cast<float>(weaponryRank) * 120.0f) + imperialIndustrialBonus;

			float archetypeMultiplier = 1.0f;
			int arch = empire->mArchetype;
			if (arch == 1 || arch == 2 || arch == 3 || arch == 10 || arch == 11 || arch == 12 || arch == 13) {
				archetypeMultiplier = 1.35f;
			} else if (arch == 4 || arch == 5 || arch == 6 || arch == 14 || arch == 15 || arch == 16) {
				archetypeMultiplier = 1.10f;
			} else {
				archetypeMultiplier = 0.85f;
			}
			return dynamicBaseHealth * archetypeMultiplier;
		}
	}

	int weaponLevel = empire->GetWeaponryLevel();
	float baseHealth = GetDefenderHealthByLevel(weaponLevel);
	float levelFactor = static_cast<float>(weaponLevel + 1) / 5.0f;
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
	if (empire == nullptr) {
		return GetTurretHealthByLevel(0);
	}
	return GetTurretHealthByLevel(empire->GetWeaponryLevel());
}

int cSpaceCombatMetrics::GetDefendersInPlanet(Simulator::cPlanetRecord* planet)
{
	if (planet == nullptr) {
		return GetDefendersInColony();
	}

	cEmpire* empire = nullptr;
	if (planet->GetStarRecord() != nullptr) {
		empire = StarManager.GetEmpire(planet->GetStarRecord()->mEmpireID);
	}

	// Автоматическая синхронизация с Defensive Ships In Player Colonies (если он установлен у игрока)
	if (empire != nullptr && empire == GetPlayerEmpire()) {
		PropertyListPtr propListFleet;
		if (PropManager.GetPropertyList(id(u"FleetConfig"), 0x02ae0c7e, propListFleet) && propListFleet != nullptr) {
			float fleetMultiplier = 1.0f;
			App::Property::GetFloat(propListFleet.get(), 0x07A1BC01, fleetMultiplier);
			if (fleetMultiplier <= 0.05f || fleetMultiplier > 5.0f) {
				fleetMultiplier = 1.0f;
			}

			int colonyCount = 1;
			if (planet->mbHomeWorld) colonyCount = 3;
			else if (planet->mType == PlanetType::T2) colonyCount = 2;
			else if (planet->mType == PlanetType::T3) colonyCount = 3;

			if (GetCurrentContext() == SpaceContext::Planet && planet == GetActivePlanetRecord()) {
				int loadedCities = 0;
				const auto& cities = Simulator::GetData<cCity>();
				for (const auto& city : cities) {
					if (city) loadedCities++;
				}
				if (loadedCities > 0) colonyCount = loadedCities;
			}

			int stepBonus = 1;
			if (planet->mType == PlanetType::T1) stepBonus = 2;
			else if (planet->mType == PlanetType::T2) stepBonus = 3;
			else if (planet->mType == PlanetType::T3) stepBonus = 4;

			int basePlayerDef = 10;
			if (planet->mbHomeWorld) basePlayerDef = 20;
			else if (planet->mType == PlanetType::T0) basePlayerDef = 5;

			int rawTotal = basePlayerDef + (colonyCount - 1) * stepBonus;
			rawTotal = static_cast<int>(round(static_cast<float>(rawTotal) * fleetMultiplier));

			int totalSystems = static_cast<int>(empire->mStars.size());
			int weaponryRank = max(empire->GetWeaponryLevel(), 1);
			int dynamicCap = static_cast<int>(round(static_cast<float>(20 + static_cast<int>(totalSystems * 0.8f) + (weaponryRank * 3)) * fleetMultiplier));
			if (dynamicCap < 12) dynamicCap = 12;
			if (dynamicCap > 120) dynamicCap = 120;

			if (rawTotal > dynamicCap) {
				float excess = static_cast<float>(rawTotal - dynamicCap);
				return dynamicCap + static_cast<int>(round(std::sqrt(excess) * 4.0f));
			}
			return max(rawTotal, 1);
		}
	}

	if (planet->mType == PlanetType::T0) {
		return GetDefendersInOutpost();
	}

	int baseColonyDefenders = GetDefendersInColony();
	int baseDefenders = baseColonyDefenders;

	if (empire != nullptr) {
		Simulator::Archetypes baseArchetype = ArchetypeUtils::GetBaseArchetype(empire->mArchetype);
		float archetypeMultiplier = 1.0f;

		switch (baseArchetype) {
			case Archetypes::kArchetypeWarrior:
			case Archetypes::kArchetypeZealot:
			case Archetypes::kArchetypeGrob:
				archetypeMultiplier = 2.5f;
				break;
			case Archetypes::kArchetypeScientist:
			case Archetypes::kArchetypeTrader:
				archetypeMultiplier = 1.6f;
				break;
			case Archetypes::kArchetypeEcologist:
				archetypeMultiplier = 1.2f;
				break;
			case Archetypes::kArchetypeShaman:
			case Archetypes::kArchetypeBard:
			case Archetypes::kArchetypeDiplomat:
			default:
				archetypeMultiplier = 0.8f;
				break;
		}

		float tScoreFactor = (planet->mType == PlanetType::T3) ? 1.5f : ((planet->mType == PlanetType::T2) ? 1.0f : 0.6f);
		if (PlanetUtils::Homeworld(planet)) {
			tScoreFactor *= 1.35f;
		}
		baseDefenders = max(static_cast<int>(round(static_cast<float>(baseColonyDefenders) * archetypeMultiplier * tScoreFactor)), 2);
	}

	// Учет реальных колоний Unlimited-Colony
	if (GetCurrentContext() == SpaceContext::Planet && planet == GetActivePlanetRecord()) {
		int cityCount = 0;
		const auto& cities = Simulator::GetData<cCity>();
		for (const auto& city : cities) {
			if (city) cityCount++;
		}
		int standardCities = (planet->mType == PlanetType::T3) ? 3 : ((planet->mType == PlanetType::T2) ? 2 : 1);
		if (cityCount > standardCities) {
			baseDefenders += (cityCount - standardCities) * max(baseColonyDefenders / 2, 1);
		}
	}

	return baseDefenders;
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
