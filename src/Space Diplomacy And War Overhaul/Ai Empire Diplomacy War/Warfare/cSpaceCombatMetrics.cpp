#include "stdafx.h"
#include "cSpaceCombatMetrics.h"
#include <Spore-Mod-Utils/Include/SporeModUtils.h>
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

	int numBuildingsT1 = 8;
	int numBuildingsT2 = 12;
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

int cSpaceCombatMetrics::AddRef()
{
	return DefaultRefCounted::AddRef();
}

int cSpaceCombatMetrics::Release()
{
	return DefaultRefCounted::Release();
}

void* cSpaceCombatMetrics::Cast(uint32_t type) const
{
	CLASS_CAST(Object);
	CLASS_CAST(cSpaceCombatMetrics);
	return nullptr;
}

static bool MatchSdoGadgetKey(uint32_t idVal, const char* n1, const char* n2, const char* n3 = nullptr, uint32_t raw1 = 0, uint32_t raw2 = 0)
{
	if (idVal == 0) return false;
	if (n1 && idVal == id(n1)) return true;
	if (n2 && idVal == id(n2)) return true;
	if (n3 && idVal == id(n3)) return true;
	if (raw1 != 0 && idVal == raw1) return true;
	if (raw2 != 0 && idVal == raw2) return true;
	return false;
}

static float GetExternalGadgetMultiplier()
{
	float mult = 1.0f;
	PropertyListPtr propList;
	if (PropManager.GetPropertyList(id(u"GadgetConfig"), 0x02ae0c7e, propList) && propList != nullptr) {
		App::Property::GetFloat(propList.get(), 0x07A1BC06, mult);
	}
	if (mult < 0.0f || mult > 5.0f) mult = 1.0f;
	return mult;
}

void cSpaceCombatMetrics::UpdatePlanetDefenseCache(Simulator::cPlanetRecord* planet)
{
	if (planet == nullptr) return;

	if (GetCurrentContext() == SpaceContext::Planet && planet == GetActivePlanetRecord()) {
		int cityCount = 0;
		int turretCount = 0;
		const auto& cities = Simulator::GetData<cCity>();
		for (const auto& city : cities) {
			if (city) {
				cityCount++;
				turretCount += static_cast<int>(city->mTurrets.size());
			}
		}
		if (cityCount > 0) {
			planetDefenseCache[cPlanetRecordPtr(planet)] = eastl::make_pair(cityCount, turretCount);
		}

		SdoPlanetGadgetInfo info;
		if (GetExternalGadgetMultiplier() > 0.01f) {
			const auto& spatials = Simulator::GetDataByCast<cSpatialObject>();
			for (const auto& sp : spatials) {
				if (!sp) continue;
				cCombatant* asComb = object_cast<cCombatant>(sp.get());
				if (asComb && asComb->mHealthPoints <= 0.0f) continue;

				uint32_t modelId = sp->GetModelKey().instanceID;
				if (MatchSdoGadgetKey(modelId, "loyaltybooster", "b_loyaltybooster", "pl_loyalty_booster", 0x04668B8A, 0x03F3E4A1)) {
					info.hasLoyaltyBooster = true;
				} else if (MatchSdoGadgetKey(modelId, "happinessbooster", "b_happinessbooster", "pl_happiness_booster", 0x04668B89, 0x03F3E4A0)) {
					info.hasHappinessBooster = true;
				} else if (MatchSdoGadgetKey(modelId, "uberturret", "b_uberturret", "super_turret", 0x0246F65C, 0x04668B8B)) {
					info.hasUberTurret = true;
				} else if (MatchSdoGadgetKey(modelId, "spicestorage", "b_spicestorage", "pl_spice_storage", 0x04668B8C, 0x03F3E4A2)) {
					info.hasSpiceStorage = true;
				} else if (MatchSdoGadgetKey(modelId, "bioprotector", "biostabilizer", "b_bioprotector", 0x04668B87, 0x04668B88)) {
					info.hasBioProtector = true;
				} else if (MatchSdoGadgetKey(modelId, "embassy", "b_embassy", "pl_embassy", 0x04668B8E, 0x03F3E4A5)) {
					info.hasEmbassy = true;
				}
			}
		}
		planetGadgetCache[cPlanetRecordPtr(planet)] = info;
	}
}

SdoPlanetGadgetInfo cSpaceCombatMetrics::GetPlanetGadgetInfo(Simulator::cPlanetRecord* planet)
{
	SdoPlanetGadgetInfo emptyInfo;
	if (planet == nullptr || GetExternalGadgetMultiplier() <= 0.01f) {
		return emptyInfo;
	}
	if (GetCurrentContext() == SpaceContext::Planet && planet == GetActivePlanetRecord()) {
		UpdatePlanetDefenseCache(planet);
	}
	auto it = planetGadgetCache.find(cPlanetRecordPtr(planet));
	if (it != planetGadgetCache.end()) {
		return it->second;
	}
	return emptyInfo;
}


int cSpaceCombatMetrics::GetCachedCityCount(Simulator::cPlanetRecord* planet) const
{
	if (planet == nullptr) return 0;
	auto it = planetDefenseCache.find(cPlanetRecordPtr(planet));
	if (it != planetDefenseCache.end()) {
		return it->second.first;
	}
	return 0;
}

int cSpaceCombatMetrics::GetCachedTurretCount(Simulator::cPlanetRecord* planet) const
{
	if (planet == nullptr) return 0;
	auto it = planetDefenseCache.find(cPlanetRecordPtr(planet));
	if (it != planetDefenseCache.end()) {
		return it->second.second;
	}
	return 0;
}

float cSpaceCombatMetrics::GetBomberHealthByLevel(int empireLevel)
{
	if (healthBomber.empty()) return 600.0f;
	if (empireLevel < 0) return healthBomber.front();
	if (empireLevel >= int(healthBomber.size())) return healthBomber.back();
	return healthBomber[empireLevel];
}

float cSpaceCombatMetrics::GetFighterHealthByLevel(int empireLevel)
{
	if (healthFighter.empty()) return 250.0f;
	if (empireLevel < 0) return healthFighter.front();
	if (empireLevel >= int(healthFighter.size())) return healthFighter.back();
	return healthFighter[empireLevel];
}

float cSpaceCombatMetrics::GetDefenderHealthByLevel(int empireLevel)
{
	if (healthDefender.empty()) return 500.0f;
	if (empireLevel < 0) return healthDefender.front();
	if (empireLevel >= int(healthDefender.size())) return healthDefender.back();
	return healthDefender[empireLevel];
}

float cSpaceCombatMetrics::GetTurretHealthByLevel(int empireLevel)
{
	if (healthTurret.empty()) return 800.0f;
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
	if (empire == nullptr) return GetBomberHealthByLevel(0);

	float baseHealth = GetBomberHealthByLevel(empire->GetWeaponryLevel());
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
	if (empire == nullptr) return GetFighterHealthByLevel(0);

	float baseHealth = GetFighterHealthByLevel(empire->GetWeaponryLevel());
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
	if (empire == nullptr) return GetDefenderHealthByLevel(0);

	// Авто-синхронизация брони защитников игрока с модом Defensive Ships In Player Colonies
	if (empire == GetPlayerEmpire()) {
		PropertyListPtr propListFleet;
		if (PropManager.GetPropertyList(id(u"FleetConfig"), 0x02ae0c7e, propListFleet) && propListFleet != nullptr) {
			int weaponryRank = max(empire->GetWeaponryLevel(), 1);
			int totalStars = static_cast<int>(empire->mStars.size());
			float industrialBonus = min(static_cast<float>(totalStars) * 8.0f, 400.0f);
			float dynBase = 450.0f + (static_cast<float>(weaponryRank) * 120.0f) + industrialBonus;

			int arch = empire->mArchetype;
			float archMult = 0.85f;
			if (arch == 1 || arch == 2 || arch == 3 || arch == 10 || arch == 11 || arch == 12 || arch == 13) {
				archMult = 1.35f;
			} else if (arch == 4 || arch == 5 || arch == 6 || arch == 14 || arch == 15 || arch == 16) {
				archMult = 1.10f;
			}
			return dynBase * archMult;
		}
	}

	float baseHealth = GetDefenderHealthByLevel(empire->GetWeaponryLevel());
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
	if (empire == nullptr) return GetTurretHealthByLevel(0);
	return GetTurretHealthByLevel(empire->GetWeaponryLevel());
}

int cSpaceCombatMetrics::GetDefendersInPlanet(Simulator::cPlanetRecord* planet)
{
	if (planet == nullptr) {
		return GetDefendersInColony();
	}

	UpdatePlanetDefenseCache(planet);
	int cachedCities = GetCachedCityCount(planet);

	cEmpire* empire = nullptr;
	if (planet->GetStarRecord() != nullptr) {
		empire = StarManager.GetEmpire(planet->GetStarRecord()->mEmpireID);
	}

	// 1. Авто-синхронизация с гарнизоном игрока из мода Defensive Ships In Player Colonies
	if (empire != nullptr && empire == GetPlayerEmpire()) {
		PropertyListPtr propListFleet;
		if (PropManager.GetPropertyList(id(u"FleetConfig"), 0x02ae0c7e, propListFleet) && propListFleet != nullptr) {
			float fleetMult = 1.0f;
			App::Property::GetFloat(propListFleet.get(), 0x07A1BC01, fleetMult);
			if (fleetMult <= 0.05f || fleetMult > 5.0f) fleetMult = 1.0f;

			int colonyCount = cachedCities;
			if (colonyCount <= 0) {
				if (PlanetUtils::Homeworld(planet) || planet->mType == PlanetType::T3) colonyCount = 3;
				else if (planet->mType == PlanetType::T2) colonyCount = 2;
				else colonyCount = 1;
			}

			int stepBonus = 1;
			if (planet->mType == PlanetType::T1) stepBonus = 2;
			else if (planet->mType == PlanetType::T2) stepBonus = 3;
			else if (planet->mType == PlanetType::T3) stepBonus = 4;

			int basePlayerDef = 10;
			if (PlanetUtils::Homeworld(planet)) basePlayerDef = 20;
			else if (planet->mType == PlanetType::T0) basePlayerDef = 5;

			int rawTotal = basePlayerDef + (colonyCount - 1) * stepBonus;
			if (GetPlanetGadgetInfo(planet).hasSpiceStorage) {
				rawTotal = static_cast<int>(round(static_cast<float>(rawTotal) * (1.0f + 0.25f * GetExternalGadgetMultiplier())));
			}
			rawTotal = static_cast<int>(round(static_cast<float>(rawTotal) * fleetMult));

			int totalSystems = static_cast<int>(empire->mStars.size());
			int weaponryRank = max(empire->GetWeaponryLevel(), 1);
			int dynamicCap = static_cast<int>(round(static_cast<float>(20 + static_cast<int>(totalSystems * 0.8f) + weaponryRank * 3) * fleetMult));
			if (dynamicCap < 12) dynamicCap = 12;
			if (dynamicCap > 120) dynamicCap = 120;

			if (rawTotal > dynamicCap) {
				float excess = static_cast<float>(rawTotal - dynamicCap);
				rawTotal = dynamicCap + static_cast<int>(round(std::sqrt(excess) * 4.0f));
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

	// Учет реальных или закэшированных колоний Unlimited-Colony
	int standardCities = (planet->mType == PlanetType::T3) ? 3 : ((planet->mType == PlanetType::T2) ? 2 : 1);
	if (cachedCities > standardCities) {
		baseDefenders += (cachedCities - standardCities) * max(baseColonyDefenders / 2, 1);
	}

	if (GetPlanetGadgetInfo(planet).hasSpiceStorage) {
		baseDefenders = static_cast<int>(round(static_cast<float>(baseDefenders) * (1.0f + 0.25f * GetExternalGadgetMultiplier())));
	}

	return baseDefenders;
}


int cSpaceCombatMetrics::GetTurretsInPlanet(Simulator::cPlanetRecord* planet)
{
	if (planet == nullptr) return 0;

	UpdatePlanetDefenseCache(planet);
	int cachedCities = GetCachedCityCount(planet);
	int cachedTurrets = GetCachedTurretCount(planet);

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
			baseTurrets = 0;
			standardCities = 1;
			break;
	}

	// Если планета была посещена в сессии — возвращаем точное число турелей из кэша (включая застройку Т0 и Unlimited-Colony)
	int finalTurrets = baseTurrets;
	if (cachedCities > 0) {
		if (cachedCities > standardCities || cachedTurrets > baseTurrets || planet->mType == PlanetType::T0) {
			finalTurrets = cachedTurrets;
		}
	}

	// Баланс Убербашни: эквивалент мощного орбитального щита (+14 тяжелых турелей в расчете штурма)
	if (GetPlanetGadgetInfo(planet).hasUberTurret) {
		finalTurrets += static_cast<int>(round(14.0f * GetExternalGadgetMultiplier()));
	}

	return finalTurrets;
}
