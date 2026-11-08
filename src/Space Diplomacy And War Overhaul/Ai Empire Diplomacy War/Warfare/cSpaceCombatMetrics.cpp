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

	numDefendersInOutpost = defendersOutpost[0];
	numDefendersInColony = defendersColony[0];

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
	if (healthBomber.empty())
	{
		return 1.0f;
	}
	if (empireLevel < 0)
	{
		return healthBomber[0];
	}
	if (empireLevel >= int(healthBomber.size()))
	{
		return healthBomber.back();
	}
	return healthBomber[empireLevel];
}

float cSpaceCombatMetrics::GetFighterHealthByLevel(int empireLevel)
{
	if (healthFighter.empty())
	{
		return 1.0f;
	}
	if (empireLevel < 0)
	{
		return healthFighter[0];
	}
	if (empireLevel >= int(healthFighter.size()))
	{
		return healthFighter.back();
	}
	return healthFighter[empireLevel];
}

float cSpaceCombatMetrics::GetDefenderHealthByLevel(int empireLevel)
{
	if (healthDefender.empty())
	{
		return 1.0f;
	}
	if (empireLevel < 0)
	{
		return healthDefender[0];
	}
	if (empireLevel >= int(healthDefender.size()))
	{
		return healthDefender.back();
	}
	return healthDefender[empireLevel];
}

float cSpaceCombatMetrics::GetTurretHealthByLevel(int empireLevel)
{
	if (healthTurret.empty())
	{
		return 1.0f;
	}
	if (empireLevel < 0)
	{
		return healthTurret[0];
	}
	if (empireLevel >= int(healthTurret.size()))
	{
		return healthTurret.back();
	}
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
	if (empire == nullptr) {
		return baseHealth;
	}

	Simulator::Archetypes baseArchetype = ArchetypeUtils::GetBaseArchetype(empire->mArchetype);
	float multiplier = 1.0f;
	switch (baseArchetype) {
		case Archetypes::kArchetypeWarrior:
		case Archetypes::kArchetypeZealot:
			multiplier = 1.25f; // Военная броня
			break;
		case Archetypes::kArchetypeScientist:
			multiplier = 1.10f; // Технологические щиты
			break;
		case Archetypes::kArchetypeBard:
		case Archetypes::kArchetypeShaman:
		case Archetypes::kArchetypeDiplomat:
			multiplier = 0.75f; // Облегченные корпуса
			break;
		default:
			multiplier = 1.00f; // Торговцы, Экологи и база
			break;
	}
	return baseHealth * multiplier;
}

float cSpaceCombatMetrics::GetFighterHealthByEmpire(Simulator::cEmpire* empire)
{
	float baseHealth = GetFighterHealthByLevel(empire->GetWeaponryLevel());
	if (empire == nullptr) {
		return baseHealth;
	}

	Simulator::Archetypes baseArchetype = ArchetypeUtils::GetBaseArchetype(empire->mArchetype);
	float multiplier = 1.0f;
	switch (baseArchetype) {
		case Archetypes::kArchetypeWarrior:
		case Archetypes::kArchetypeZealot:
			multiplier = 1.20f;
			break;
		case Archetypes::kArchetypeScientist:
			multiplier = 1.10f;
			break;
		case Archetypes::kArchetypeBard:
		case Archetypes::kArchetypeShaman:
		case Archetypes::kArchetypeDiplomat:
			multiplier = 0.80f;
			break;
		default:
			multiplier = 1.00f;
			break;
	}
	return baseHealth * multiplier;
}

float cSpaceCombatMetrics::GetDefenderHealthByEmpire(Simulator::cEmpire* empire)
{
	return GetDefenderHealthByLevel(empire->GetWeaponryLevel());
}

float cSpaceCombatMetrics::GetTurretHealthByEmpire(Simulator::cEmpire* empire)
{
	return GetTurretHealthByLevel(empire->GetWeaponryLevel());
}

int cSpaceCombatMetrics::GetDefendersInPlanet(Simulator::cPlanetRecord* planet)
{
	if (planet == nullptr) {
		return GetDefendersInColony();
	}

	int baseDefenders = (planet->mType == PlanetType::T0) ? GetDefendersInOutpost() : GetDefendersInColony();
	int expectedColonies = (planet->mType == PlanetType::T0) ? 1 : ((planet->mType == PlanetType::T1) ? 1 : ((planet->mType == PlanetType::T2) ? 2 : 3));

	// Масштабирование гарнизона под Unlimited-Colony
	if (planet->GetPlanetView() && !planet->GetPlanetView()->mColonies.empty()) {
		int actualColonies = static_cast<int>(planet->GetPlanetView()->mColonies.size());
		if (actualColonies > expectedColonies) {
			baseDefenders += (actualColonies - expectedColonies) * 2;
		}
	}
	return baseDefenders;
}

int cSpaceCombatMetrics::GetTurretsInPlanet(Simulator::cPlanetRecord* planet)
{
	if (planet == nullptr) {
		return 0;
	}

	int baseTurrets = 0;
	int expectedColonies = 1;
	switch (planet->mType) {
		case(PlanetType::T1):
			baseTurrets = GetTurretsInT1();
			expectedColonies = 1;
			break;
		case(PlanetType::T2):
			baseTurrets = GetTurretsInT2();
			expectedColonies = 2;
			break;
		case(PlanetType::T3):
			baseTurrets = GetTurretsInT3();
			expectedColonies = 3;
			break;
		default:
			return 0;
	}

	// Масштабирование турелей под Unlimited-Colony (дополнительные города добавляют башни к расчету обороны)
	if (planet->GetPlanetView() && !planet->GetPlanetView()->mColonies.empty()) {
		int actualColonies = static_cast<int>(planet->GetPlanetView()->mColonies.size());
		if (actualColonies > expectedColonies) {
			int turretsPerColony = max(baseTurrets / expectedColonies, 4);
			baseTurrets += (actualColonies - expectedColonies) * turretsPerColony;
		}
	}
	return baseTurrets;
}
