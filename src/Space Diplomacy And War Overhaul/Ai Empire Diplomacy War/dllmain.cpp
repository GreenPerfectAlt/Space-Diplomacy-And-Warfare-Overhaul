// dllmain.cpp : Defines the entry point for the DLL application.
#include "stdafx.h"
#include "Diplomacy\DebugDiplomacy.h"
#include <Spore/Simulator/SubSystem/CommManager.h>
#include <Spore/App/cMessageManager.h>
#include <Spore/App/IMessageManager.h>
#include "Diplomacy\cDiplomacySystem.h"
#include <Spore/App/cCameraManager.h>
#include <Spore/Swarm/cEffectsManager.h>
#include <Spore/Simulator/cCity.h>
#include <Spore/Simulator/cBuilding.h>
#include <Spore/CommonIDs.h>
#include <Spore-Mod-Utils/Include/SporeModUtils.h>
#include "Warfare/DebugWarfare.h"
#include "Warfare/cWarfareSystem.h"
#include "Warfare/cToolInvasionStrategy.h"
#include "Diplomacy/AffinityTextProc.h"
#include "cCompositionRoot.h"
#include "cPersistedEvent.h"
#include "Diplomacy/PersistedEvent/cPersistedDiplomacyEvent.h"
#include "Diplomacy/PersistedEvent/cNeighborsInPeaceEvent.h"
#include "Diplomacy/PersistedEvent/cMadePeaceEvent.h"
#include "Diplomacy/PersistedEvent/cUpliftedByMonolithEvent.h"
#include "Diplomacy/PersistedEvent/cFormedAllianceEvent.h"
#include "Diplomacy/PersistedEvent/cDefeatedEnemyTogetherEvent.h"
#include "Diplomacy/DiplomacyDetours.h"
#include "cPersistedObject.h"
#include "cPersistenceState.h"
#include "cDatabaseManager.h"
#include "Diplomacy/PersistedEvent/cPreparingToDeclareWarEvent.h"
#include <EASTL/map.h>
#include <cmath>

using namespace SporeModUtils;
using namespace Simulator;

void Initialize()
{
	// CheatManager.AddCheat("DebugDiplomacy", new DebugDiplomacy());
	// CheatManager.AddCheat("DebugWarfare", new DebugWarfare());
	cSimulatorSystem::Get()->AddStrategy(new cCompositionRoot(), cCompositionRoot::NOUN_ID);
	cSimulatorSystem::Get()->AddStrategy(new cDiplomacySystem(), cDiplomacySystem::NOUN_ID);
	cSimulatorSystem::Get()->AddStrategy(new cWarfareSystem(), cWarfareSystem::NOUN_ID);
	cSimulatorSystem::Get()->AddStrategy(new cDatabaseManager(), cDatabaseManager::NOUN_ID);
	ToolManager.AddStrategy(new cToolInvasionStrategy(), cToolInvasionStrategy::TYPE);

	ClassManager.AddFactory(new cPersistedObjectFactory());
	ClassManager.AddFactory(new cPersistenceStateFactory());
	ClassManager.AddFactory(new cPersistedEventFactory());
	ClassManager.AddFactory(new cPersistedDiplomacyEventFactory());
	ClassManager.AddFactory(new cNeighborsInPeaceEventFactory());
	ClassManager.AddFactory(new cMadePeaceEventFactory());
	ClassManager.AddFactory(new cUpliftedByMonolithEventFactory());
	ClassManager.AddFactory(new cFormedAllianceEventFactory());
	ClassManager.AddFactory(new cDefeatedEnemyTogetherEventFactory());
	ClassManager.AddFactory(new cPreparingForWarEventFactory());
}

void Dispose()
{
	// This method is called when the game is closing
}

member_detour(ShouldSpawnRaiderUFO__detour, cRaidEvent, bool())
{
	bool detoured()
	{
		bool ret = original_function(this);
		return false;
	}
};

static bool sIsGeneratingInitialPlanetGarrison = false;
static eastl::map<cPlanetRecordPtr, uint32_t> sNpcPlanetUprisingCooldowns;

static float ReadExternalConfigFloat(const char16_t* propName, uint32_t propID, float defaultVal)
{
	float val = defaultVal;
	PropertyListPtr propList;
	if (PropManager.GetPropertyList(id(propName), 0x02ae0c7e, propList) && propList != nullptr) {
		App::Property::GetFloat(propList.get(), propID, val);
	}
	return val;
}

static float ReadWarfareConfigFloat(uint32_t propID, float defaultVal)
{
	PropertyListPtr configKeys;
	if (PropManager.GetPropertyList(id("ConfigurationKeys"), id("SdoConfig"), configKeys) && configKeys != nullptr) {
		ResourceKey warfareKey;
		if (App::Property::GetKey(configKeys.get(), 0x0FCF340F, warfareKey)) {
			PropertyListPtr warfareProp;
			if (PropManager.GetPropertyList(warfareKey.instanceID, warfareKey.groupID, warfareProp) && warfareProp != nullptr) {
				float val = defaultVal;
				if (App::Property::GetFloat(warfareProp.get(), propID, val)) {
					return val;
				}
			}
		}
	}
	return defaultVal;
}

static float ReadWarfareRangeForEmpire(cEmpire* empire, float defaultRange)
{
	if (!empire) return defaultRange;
	PropertyListPtr configKeys;
	if (PropManager.GetPropertyList(id("ConfigurationKeys"), id("SdoConfig"), configKeys) && configKeys != nullptr) {
		ResourceKey warfareKey;
		if (App::Property::GetKey(configKeys.get(), 0x0FCF340F, warfareKey)) {
			PropertyListPtr warfareProp;
			if (PropManager.GetPropertyList(warfareKey.instanceID, warfareKey.groupID, warfareProp) && warfareProp != nullptr) {
				eastl::vector<float> ranges;
				if (App::Property::GetArrayFloat(warfareProp.get(), 0x4BC711F0, ranges) && !ranges.empty()) {
					int lvl = empire->GetWeaponryLevel();
					if (lvl < 0) return ranges.front();
					if (lvl >= static_cast<int>(ranges.size())) return ranges.back();
					return ranges[lvl];
				}
			}
		}
	}
	return defaultRange;
}

static int GetDynamicMaxTreasuryForEmpire(cEmpire* empire, cCompositionRoot* root)
{
	if (!empire) return 150000;

	float produced = 2.0f;
	if (root && root->warfareStrengthAnalyzer) {
		produced = max(root->warfareStrengthAnalyzer->GetBombersProducedByEmpire(empire), 1.0f);
	}

	float redSpiceRate = max(ReadWarfareConfigFloat(0xD07B1F69, 1.0f), 0.1f);
	int unitCost = max(static_cast<int>(round(1000.0f / redSpiceRate)), 250);

	Simulator::Archetypes baseArch = ArchetypeUtils::GetBaseArchetype(empire->mArchetype);
	float tradeMult = (baseArch == Archetypes::kArchetypeTrader) ? 1.75f : 1.0f;

	int cycleIncome = max(static_cast<int>(round(produced * static_cast<float>(unitCost) * 0.45f * tradeMult)), 2500);
	int starBaseCap = 50000 + static_cast<int>(empire->mStars.size()) * 25000;

	int maxCap = static_cast<int>(round(static_cast<float>(starBaseCap + cycleIncome * 10) * tradeMult));
	return max(maxCap, 80000);
}

static float GetPlanetSpicePrice(cPlanetRecord* planet)
{
	if (!planet) return 0.0f;

	uint32_t spiceID = planet->mSpiceGen.instanceID;
	if (spiceID == 0) return 0.0f;

	PropertyListPtr spiceProp;
	if (PropManager.GetPropertyList(spiceID, 0x034D97FA, spiceProp) && spiceProp != nullptr) {
		float baseCost = 0.0f;
		if (App::Property::GetFloat(spiceProp.get(), 0x00B2CCCB, baseCost) && baseCost > 0.0f) {
			return baseCost;
		}
	}

	if (spiceID == 0x0289B841 || spiceID == 0x0289B842) return 3000.0f; // Purple, Pink
	if (spiceID == 0x0289B83F || spiceID == 0x0289B840) return 1200.0f; // Blue, Green
	return 300.0f; // Red, Yellow
}

static float GetNpcPlanetaryCityUnrestFactor(cPlanetRecord* planet)
{
	if (!planet) return 0.0f;

	float unrestMult = ReadExternalConfigFloat(u"UnrestConfig", 0x07A1BC04, 1.0f);
	if (unrestMult <= 0.01f) return 0.0f;

	if (GetCurrentContext() != SpaceContext::Planet || planet != GetActivePlanetRecord()) {
		return 0.0f;
	}

	const auto& cities = GetData<cCity>();
	int totalCities = 0;
	int unhappyCitizens = 0;
	int totalFactories = 0;
	int totalEntertainment = 0;

	for (const auto& city : cities) {
		if (!city) continue;
		totalCities++;
		unhappyCitizens += max(static_cast<int>(city->mUnhappyCount), 0);

		for (const auto& bld : city->mBuildings) {
			if (!bld) continue;
			uint32_t noun = bld->GetNounID();
			if (noun == Simulator::GameNounIDs::kBuildingIndustry) {
				totalFactories++;
			} else if (noun == Simulator::GameNounIDs::kBuildingEntertainment) {
				totalEntertainment++;
			}
		}
	}

	if (totalCities <= 0) return 0.0f;

	float avgUnhappy = static_cast<float>(unhappyCitizens) / static_cast<float>(totalCities);
	float unhappyScore = min(avgUnhappy * 0.22f, 0.85f);

	float factoryStrain = 0.0f;
	if (totalFactories > totalEntertainment * 2 + totalCities) {
		int excessFactories = totalFactories - (totalEntertainment * 2 + totalCities);
		factoryStrain = min(static_cast<float>(excessFactories) * 0.06f, 0.45f);
	}

	float rawUnrest = min(unhappyScore + factoryStrain, 1.0f);
	return min(rawUnrest * unrestMult, 1.5f);
}

static bool IsNpcPlanetInCombat(cPlanetRecord* planet, cEmpire* ownerEmpire)
{
	if (!planet || !ownerEmpire) return false;

	cEmpire* playerEmp = GetPlayerEmpire();
	if (playerEmp && RelationshipManager.IsAtWar(ownerEmpire, playerEmp)) {
		return true;
	}

	const auto& ufos = GetData<cGameDataUFO>();
	for (const auto& ufo : ufos) {
		if (ufo && ufo->mHealthPoints > 0 && ufo->mPoliticalID != 0 && ufo->mPoliticalID != ownerEmpire->GetEmpireID()) {
			cEmpire* otherEmp = StarManager.GetEmpire(ufo->mPoliticalID);
			if (!otherEmp) {
				return true; // Pirates or active rebel separatists
			}
			if (RelationshipManager.IsAtWar(ownerEmpire, otherEmp)) {
				return true;
			}
		}
	}
	return false;
}

static float ComputeNpcPlanetRebellionChance(cPlanetRecord* activePlanet, cEmpire* ownerEmpire, cCompositionRoot* root, bool peacetimeCheck, cGameDataUFO* newlySpawnedUfo)
{
	if (!activePlanet || !ownerEmpire || !root || !root->spaceCombatMetrics) {
		return 0.0f;
	}

	float rebellionMultiplier = ReadExternalConfigFloat(u"RebellionConfig", 0x07A1BC02, 1.0f);
	if (rebellionMultiplier <= 0.01f) {
		return 0.0f;
	}

	// 1. Zealots and Grox never rebel
	Simulator::Archetypes baseArch = ArchetypeUtils::GetBaseArchetype(ownerEmpire->mArchetype);
	if (baseArch == Archetypes::kArchetypeZealot || baseArch == Archetypes::kArchetypeGrob) {
		return 0.0f;
	}

	// 2. Crisis checks: bankruptcy (if upkeep enabled), city strikes, or combat losses
	float upkeepMult = ReadExternalConfigFloat(u"UpkeepConfig", 0x07A1BC03, 1.0f);
	int maxCap = GetDynamicMaxTreasuryForEmpire(ownerEmpire, root);
	int crisisThreshold = static_cast<int>(round(static_cast<float>(maxCap) * 0.15f));
	bool isBankrupt = (upkeepMult > 0.01f) && (ownerEmpire->mEmpireMoney > 0 && ownerEmpire->mEmpireMoney < crisisThreshold);

	float cityUnrest = GetNpcPlanetaryCityUnrestFactor(activePlanet);
	bool severeCityStrike = (cityUnrest >= 0.45f);

	bool heavyLosses = false;
	if (!peacetimeCheck) {
		int targetDefenders = min(root->spaceCombatMetrics->GetDefendersInPlanet(activePlanet), 24);
		int aliveDefenders = 0;
		const auto& ufos = GetData<cGameDataUFO>();
		for (const auto& ufo : ufos) {
			if (ufo && ufo.get() != newlySpawnedUfo && ufo->mHealthPoints > 0 &&
				ufo->mPoliticalID == ownerEmpire->GetEmpireID() &&
				ufo->mUFOType == static_cast<int>(UfoType::Defender)) {
				aliveDefenders++;
			}
		}
		heavyLosses = (targetDefenders <= 2) ? (aliveDefenders == 0) : (aliveDefenders < (targetDefenders / 2));
	}

	if (peacetimeCheck) {
		if (!isBankrupt && !severeCityStrike) {
			return 0.0f;
		}
		// Capital homeworlds don't randomly revolt in peacetime unless severe strikes hit
		if (PlanetUtils::Homeworld(activePlanet) && !severeCityStrike) {
			return 0.0f;
		}
	} else {
		if (!isBankrupt && !heavyLosses && !severeCityStrike) {
			return 0.0f;
		}
	}

	// 3. Base philosophy rebellion rate
	float rebellionChance = 0.25f;
	if (baseArch == Archetypes::kArchetypeWarrior) {
		rebellionChance = 0.50f;
	} else if (baseArch == Archetypes::kArchetypeTrader) {
		rebellionChance = 0.40f;
	} else if (baseArch == Archetypes::kArchetypeScientist || baseArch == Archetypes::kArchetypeEcologist) {
		rebellionChance = 0.15f;
	} else if (baseArch == Archetypes::kArchetypeDiplomat || baseArch == Archetypes::kArchetypeBard || baseArch == Archetypes::kArchetypeShaman) {
		rebellionChance = 0.20f;
	}

	if (severeCityStrike) {
		rebellionChance += cityUnrest * 0.20f;
	}

	// 4. Local Terraforming Tier & Homeworld Loyalty
	float planetTierFactor = 1.0f;
	if (PlanetUtils::Homeworld(activePlanet)) {
		planetTierFactor = 0.20f;
	} else if (activePlanet->mType == PlanetType::T0) {
		planetTierFactor = 1.55f;
	} else if (activePlanet->mType == PlanetType::T1) {
		planetTierFactor = 1.25f;
	} else if (activePlanet->mType == PlanetType::T3) {
		planetTierFactor = 0.60f;
	}

	// 5. Local Spice Valuation
	float spiceFactor = 1.0f;
	float spicePrice = GetPlanetSpicePrice(activePlanet);
	if (spicePrice > 3500.0f) {
		spiceFactor = 0.65f;
	} else if (spicePrice >= 2000.0f) {
		spiceFactor = 0.75f;
	} else if (spicePrice > 0.0f && spicePrice <= 450.0f) {
		spiceFactor = 1.15f;
	}

	// 6. City & Turret Garrison Control (Unlimited-Colony aware)
	int cityCount = 1;
	int turretCount = 0;
	const auto& cities = GetData<cCity>();
	int loadedCities = 0;
	for (const auto& city : cities) {
		if (city) {
			loadedCities++;
			turretCount += static_cast<int>(city->mTurrets.size());
		}
	}
	if (loadedCities > 0) {
		cityCount = loadedCities;
	}

	float infrastructureFactor = 1.0f;
	if (cityCount <= 1) {
		infrastructureFactor = 1.15f;
	} else {
		infrastructureFactor = max(1.0f - static_cast<float>(cityCount - 1) * 0.08f, 0.50f);
	}
	if (turretCount >= 12) {
		infrastructureFactor *= 0.85f;
	}

	// 7. Distance from Capital & Philosophy Frontier Doctrine
	float distanceFactor = 1.0f;
	cStarRecord* homeStar = EmpireUtils::GetHomeStar(ownerEmpire);
	cStarRecord* currentStar = activePlanet->GetStarRecord();
	if (homeStar && currentStar && homeStar != currentStar) {
		float dist = StarUtils::GetDistanceBetweenStars(homeStar, currentStar);
		float coreRadius = ReadWarfareRangeForEmpire(ownerEmpire, 12.0f);
		if (coreRadius <= 0.0f) coreRadius = 12.0f;

		if (baseArch == Archetypes::kArchetypeWarrior || baseArch == Archetypes::kArchetypeTrader) {
			if (dist > coreRadius) {
				distanceFactor = min(1.0f + (dist - coreRadius) * 0.03f, 1.60f);
			}
		} else if (baseArch == Archetypes::kArchetypeDiplomat || baseArch == Archetypes::kArchetypeEcologist ||
		           baseArch == Archetypes::kArchetypeBard || baseArch == Archetypes::kArchetypeShaman) {
			if (dist > coreRadius * 0.8f) {
				distanceFactor = max(1.0f - (dist - coreRadius * 0.8f) * 0.02f, 0.65f);
			}
		} else if (baseArch == Archetypes::kArchetypeScientist) {
			float relayRange = max(coreRadius * 1.8f, 25.0f);
			if (dist > relayRange) {
				distanceFactor = min(1.0f + (dist - relayRange) * 0.015f, 1.20f);
			}
		}
	}

	// 8. Cultural Subversion (Affinity with Player during combat sieges)
	float subversionFactor = 1.0f;
	if (!peacetimeCheck && root->empireRelationsAnalyzer && GetPlayerEmpire() && ownerEmpire != GetPlayerEmpire()) {
		int affinityWithPlayer = root->empireRelationsAnalyzer->EmpiresAffinity(ownerEmpire, GetPlayerEmpire());
		if (affinityWithPlayer >= 2) {
			subversionFactor = 1.25f;
		} else if (affinityWithPlayer == 1) {
			subversionFactor = 1.10f;
		} else if (affinityWithPlayer <= -2) {
			subversionFactor = 0.85f;
		}
	}

	float finalChance = rebellionChance * planetTierFactor * spiceFactor * infrastructureFactor * distanceFactor * subversionFactor * rebellionMultiplier;
	if (peacetimeCheck) {
		finalChance *= 0.70f;
	}
	return min(finalChance, 0.90f);
}

static bool ShouldTriggerNpcRebellion(cEmpire* ownerEmpire, cCompositionRoot* root, cGameDataUFO* newlySpawnedUfo)
{
	if (GetCurrentContext() != SpaceContext::Planet) return false;
	cPlanetRecord* activePlanet = GetActivePlanetRecord();
	if (!activePlanet || !activePlanet->GetStarRecord() ||
		activePlanet->GetStarRecord()->mEmpireID != ownerEmpire->GetEmpireID()) {
		return false;
	}
	if (!IsNpcPlanetInCombat(activePlanet, ownerEmpire)) {
		return false;
	}

	float chance = ComputeNpcPlanetRebellionChance(activePlanet, ownerEmpire, root, false, newlySpawnedUfo);
	if (chance <= 0.001f) return false;
	return (Math::randf() < chance);
}

static void TryTriggerNpcPeacetimeColonialUprising(cPlanetRecord* planet, cEmpire* planetEmpire, cCompositionRoot* root)
{
	if (!planet || !planetEmpire || !root) return;
	if (IsNpcPlanetInCombat(planet, planetEmpire)) return;

	uint32_t currentSpaceTime = (root->persistenceState != nullptr) ? root->persistenceState->GetCurrentSpaceTime() : 0;
	auto itCd = sNpcPlanetUprisingCooldowns.find(planet);
	if (itCd != sNpcPlanetUprisingCooldowns.end() && currentSpaceTime > 0 && currentSpaceTime < itCd->second) {
		return;
	}

	float chance = ComputeNpcPlanetRebellionChance(planet, planetEmpire, root, true, nullptr);
	if (chance <= 0.001f) return;

	if (Math::randf() >= chance) {
		if (currentSpaceTime > 0) {
			sNpcPlanetUprisingCooldowns[cPlanetRecordPtr(planet)] = currentSpaceTime + 90000;
		}
		return;
	}

	eastl::vector<cGameDataUFO*> loyalDefenders;
	const auto& ufos = GetData<cGameDataUFO>();
	for (const auto& ufo : ufos) {
		if (ufo && ufo->mHealthPoints > 0 &&
			ufo->mPoliticalID == planetEmpire->GetEmpireID() &&
			ufo->mUFOType == static_cast<int>(UfoType::Defender)) {
			loyalDefenders.push_back(ufo.get());
		}
	}

	if (loyalDefenders.size() < 2) return;

	float rebelFraction = min(0.28f + GetNpcPlanetaryCityUnrestFactor(planet) * 0.20f, 0.45f);
	int rebelCount = max(static_cast<int>(round(static_cast<float>(loyalDefenders.size()) * rebelFraction)), 1);
	rebelCount = min(rebelCount, static_cast<int>(loyalDefenders.size()) - 1);

	for (int i = 0; i < rebelCount; ++i) {
		cGameDataUFO* traitor = loyalDefenders[i];
		traitor->mPoliticalID = 0xFFFFFFFF;
		traitor->mUFOType = static_cast<int>(UfoType::Bomber);
	}

	if (currentSpaceTime > 0) {
		sNpcPlanetUprisingCooldowns[cPlanetRecordPtr(planet)] = currentSpaceTime + 240000;
	}
}

static_detour(SpawnUFO__detour, cGameDataUFO*(UfoType, int*, ResourceKey*))
{
	cGameDataUFO* detoured(UfoType ufoType, int* pPoliticalID, ResourceKey* pUfoKey)
	{
		cGameDataUFO* ufo = original_function(ufoType, pPoliticalID, pUfoKey);
		if (!ufo || !IsSpaceGame() || !pPoliticalID || *pPoliticalID == 0) {
			return ufo;
		}

		cCompositionRoot* root = cCompositionRoot::Get();
		if (!root || !root->spaceCombatMetrics) {
			return ufo;
		}

		uint32_t politicalID = static_cast<uint32_t>(*pPoliticalID);
		cEmpire* ownerEmpire = StarManager.GetEmpire(politicalID);
		if (!ownerEmpire) {
			return ufo;
		}

		// Player defenders are handled by Defensive Ships In Player Colonies
		if (ufoType == UfoType::Defender && ownerEmpire == GetPlayerEmpire()) {
			return ufo;
		}

		float customHP = 0.0f;
		int typeInt = static_cast<int>(ufoType);
		if (ufoType == UfoType::Bomber) {
			customHP = root->spaceCombatMetrics->GetBomberHealthByEmpire(ownerEmpire);
		}
		else if (ufoType == UfoType::Defender) {
			customHP = root->spaceCombatMetrics->GetDefenderHealthByEmpire(ownerEmpire);
		}
		else if (typeInt == 4 || typeInt == 3) {
			customHP = root->spaceCombatMetrics->GetFighterHealthByEmpire(ownerEmpire);
		}

		if (customHP > 1.0f) {
			int maxCap = GetDynamicMaxTreasuryForEmpire(ownerEmpire, root);
			int richThreshold = static_cast<int>(round(static_cast<float>(maxCap) * 0.80f));
			int crisisThreshold = static_cast<int>(round(static_cast<float>(maxCap) * 0.15f));

			if (ownerEmpire->mStars.size() >= 3 && ownerEmpire->mEmpireMoney >= richThreshold) {
				customHP *= 1.20f;
			} else if (ownerEmpire->mEmpireMoney > 0 && ownerEmpire->mEmpireMoney < crisisThreshold) {
				customHP *= 0.75f;
			}

			ufo->mHealthPoints = customHP;
			ufo->mMaxHealthPoints = customHP;
		}

		if (ufoType == UfoType::Defender && !sIsGeneratingInitialPlanetGarrison) {
			if (ShouldTriggerNpcRebellion(ownerEmpire, root, ufo)) {
				ufo->mPoliticalID = 0xFFFFFFFF;
				ufo->mUFOType = static_cast<int>(UfoType::Bomber);
			}
		}

		return ufo;
	}
};

member_detour(GenerateNpcPlanetDefenders__detour, cSimPlanetHighLOD, void(cPlanet* planet))
{
	void detoured(cPlanet* planet)
	{
		if (sIsGeneratingInitialPlanetGarrison) {
			original_function(this, planet);
			return;
		}

		sIsGeneratingInitialPlanetGarrison = true;
		original_function(this, planet);

		if (IsSpaceGame() && planet && planet->mpPlanetRecord) {
			cCompositionRoot* root = cCompositionRoot::Get();

			if (root && root->spaceCombatMetrics) {
				root->spaceCombatMetrics->UpdatePlanetDefenseCache(planet->mpPlanetRecord.get());
			}

			cEmpire* planetEmpire = planet->GetEmpire();
			if (!planetEmpire && planet->mpPlanetRecord->GetStarRecord() && planet->mpPlanetRecord->GetStarRecord()->mEmpireID != 0) {
				planetEmpire = StarManager.GetEmpire(planet->mpPlanetRecord->GetStarRecord()->mEmpireID);
			}

			if (planetEmpire && planetEmpire != GetPlayerEmpire() &&
				root && root->spaceCombatMetrics && root->simulationValidator &&
				root->simulationValidator->ValidEmpire(planetEmpire, false, true)) {

				int targetDefenders = root->spaceCombatMetrics->GetDefendersInPlanet(planet->mpPlanetRecord.get());
				if (targetDefenders > 24) {
					targetDefenders = 24;
				}

				if (targetDefenders > 0) {
					int existingDefenders = 0;
					const auto& ufos = GetData<cGameDataUFO>();
					for (const auto& ufo : ufos) {
						if (ufo && ufo->mHealthPoints > 0 &&
							ufo->mPoliticalID == planetEmpire->GetEmpireID() &&
							ufo->mUFOType == static_cast<int>(UfoType::Defender)) {
							existingDefenders++;
						}
					}

					int toSpawn = targetDefenders - existingDefenders;
					if (toSpawn > 0) {
						int* pPolID = &planetEmpire->mPoliticalID;
						ResourceKey* pKey = &planetEmpire->mUFOKey;
						for (int i = 0; i < toSpawn; ++i) {
							STATIC_CALL(Address(ModAPI::ChooseAddress(0x0102bbb0, 0x0102acc0)),
								cGameDataUFO*,
								Args(UfoType, int*, ResourceKey*),
								Args(UfoType::Defender, pPolID, pKey));
						}
					}
				}

				// Peacetime colonial uprising check on NPC planet entry (strikes / war debt)
				TryTriggerNpcPeacetimeColonialUprising(planet->mpPlanetRecord.get(), planetEmpire, root);
			}
		}

		sIsGeneratingInitialPlanetGarrison = false;
	}
};

void AttachDetours()
{
	ShowCommEvent__detour::attach(GetAddress(cCommManager, ShowCommEvent));
	ApplyRelationshipMonolith__detour::attach(GetAddress(cRelationshipManager, ApplyRelationship));
	DeclareWar__detour::attach(GetAddress(cRelationshipManager, DeclareWar));
	DeclareAlliance__detour::attach(GetAddress(cRelationshipManager, DeclareAlliance));
	BreakAlliance__detour::attach(GetAddress(cRelationshipManager, BreakAlliance));
	HandleSpaceCommAction__detour::attach(GetAddress(cCommManager, HandleSpaceCommAction));
	ShowEvent__detour::attach(GetAddress(cUIEventLog, ShowEvent));
	EmpireDestroyed__detour::attach(GetAddress(cEmpire, Destroy));
	ShouldSpawnRaiderUFO__detour::attach(Address(ModAPI::ChooseAddress(0x00c5adc0, 0x00c5b860)));
	SpawnUFO__detour::attach(Address(ModAPI::ChooseAddress(0x0102bbb0, 0x0102acc0)));
	GenerateNpcPlanetDefenders__detour::attach(Address(ModAPI::ChooseAddress(0x00ffa340, 0x00ff96b0)));
}

// Generally, you don't need to touch any code here
BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
					 )
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		ModAPI::AddPostInitFunction(Initialize);
		ModAPI::AddDisposeFunction(Dispose);

		PrepareDetours(hModule);
		AttachDetours();
		CommitDetours();
		break;

	case DLL_PROCESS_DETACH:
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
		break;
	}
	return TRUE;
}
