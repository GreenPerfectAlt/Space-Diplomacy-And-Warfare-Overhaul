// dllmain.cpp : Defines the entry point for the DLL application.
#include "stdafx.h"
#include "Diplomacy\DebugDiplomacy.h"
#include <Spore/Simulator/SubSystem/CommManager.h>
#include <Spore/App/cMessageManager.h>
#include <Spore/App/IMessageManager.h>
#include "Diplomacy\cDiplomacySystem.h"
#include <Spore/App/cCameraManager.h>
#include <Spore/Swarm/cEffectsManager.h>
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
#include <Spore/Simulator/cCity.h>
#include <Spore/Simulator/cBuilding.h>
#include <EASTL/map.h>
#include <cmath>
#include <ctime>

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
static eastl::map<cPlanetRecordPtr, time_t> sNpcPlanetUprisingCooldowns;

static float ReadExternalConfigFloat(const char16_t* propName, uint32_t propID, float defaultVal)
{
	PropertyListPtr propList;
	if (PropManager.GetPropertyList(id(propName), 0x02ae0c7e, propList) && propList != nullptr) {
		float val = defaultVal;
		if (App::Property::GetFloat(propList.get(), propID, val)) {
			return val;
		}
	}
	return defaultVal;
}

static int CalculateDynamicMaxTreasury(cEmpire* empire, cCompositionRoot* root)
{
	if (!empire) return 100000;

	int starCount = static_cast<int>(empire->mStars.size());
	float bombersPerCycle = 2.0f;
	if (root && root->warfareStrengthAnalyzer) {
		bombersPerCycle = max(root->warfareStrengthAnalyzer->GetBombersProducedByEmpire(empire), 1.0f);
	}

	float redSpiceRate = 0.25f;
	PropertyListPtr warfareProp;
	if (PropManager.GetPropertyList(id("WarfareConfig"), id("SwoConfig"), warfareProp) && warfareProp != nullptr) {
		App::Property::GetFloat(warfareProp.get(), 0xD07B1F69, redSpiceRate);
	}
	int unitCost = max(static_cast<int>(round(1000.0f / max(redSpiceRate, 0.1f))), 250);

	Simulator::Archetypes baseArch = ArchetypeUtils::GetBaseArchetype(empire->mArchetype);
	float tradeMultiplier = (baseArch == Archetypes::kArchetypeTrader) ? 1.75f : 1.0f;

	int cycleValue = static_cast<int>(round(bombersPerCycle * static_cast<float>(unitCost) * tradeMultiplier));
	int maxCap = 50000 + (starCount * 25000) + (cycleValue * 10);

	if (maxCap < 60000) maxCap = 60000;
	if (maxCap > 15000000) maxCap = 15000000;
	return maxCap;
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

static float GetNpcPlanetaryCityUnrestFactor(cPlanetRecord* planet, float unrestMultiplier)
{
	if (!planet || unrestMultiplier <= 0.01f) {
		return 0.0f;
	}

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
		if (city->mUnhappyCount > 0) {
			unhappyCitizens += city->mUnhappyCount;
		}

		for (const auto& bld : city->mBuildings) {
			if (!bld) continue;
			int bType = bld->GetBuildingType();
			if (bType == kBuildingFactory) {
				totalFactories++;
			} else if (bType == kBuildingEntertainment) {
				totalEntertainment++;
			}
		}
	}

	if (totalCities == 0) return 0.0f;

	float unrestScore = 0.0f;
	if (unhappyCitizens > 0) {
		unrestScore += min(static_cast<float>(unhappyCitizens) / static_cast<float>(totalCities * 3), 1.2f);
	}

	if (totalFactories > totalEntertainment + totalCities) {
		float industrialExcess = static_cast<float>(totalFactories - (totalEntertainment + totalCities));
		unrestScore += min(industrialExcess * 0.12f, 0.8f);
	}

	return min(unrestScore * unrestMultiplier, 2.0f);
}

static float CalculateLocalNpcInstabilityMultiplier(cPlanetRecord* planet, cEmpire* ownerEmpire, cCompositionRoot* root, float cityUnrestScore)
{
	float stabilityMod = 1.0f;
	if (!planet || !ownerEmpire) return stabilityMod;

	Simulator::Archetypes baseArch = ArchetypeUtils::GetBaseArchetype(ownerEmpire->mArchetype);

	// 1. Столица и Терраиндекс (T0/T1 горячие точки, T3 стабильные миры)
	if (PlanetUtils::Homeworld(planet)) {
		stabilityMod *= 0.20f;
	} else if (planet->mType == PlanetType::T0) {
		stabilityMod *= 1.55f;
	} else if (planet->mType == PlanetType::T1) {
		stabilityMod *= 1.25f;
	} else if (planet->mType == PlanetType::T3) {
		stabilityMod *= 0.60f;
	}

	// 2. Рыночная цена местной пряности (включая T0-шахты)
	float spicePrice = GetPlanetSpicePrice(planet);
	if (spicePrice > 3500.0f) {
		stabilityMod *= 0.65f;
	} else if (spicePrice >= 2000.0f) {
		stabilityMod *= 0.75f;
	} else if (spicePrice > 0.0f && spicePrice <= 400.0f) {
		stabilityMod *= 1.15f;
	}

	// 3. Плотность ратушей и сеть ПВО турелей (Unlimited-Colony)
	int cityCount = 0;
	int turretCount = 0;
	const auto& cities = GetData<cCity>();
	for (const auto& city : cities) {
		if (city) {
			cityCount++;
			turretCount += static_cast<int>(city->mTurrets.size());
		}
	}
	if (cityCount <= 1 && !PlanetUtils::Homeworld(planet)) {
		stabilityMod *= 1.15f;
	} else if (cityCount > 1) {
		float cityControl = max(1.0f - static_cast<float>(cityCount - 1) * 0.07f, 0.55f);
		stabilityMod *= cityControl;
	}
	if (turretCount >= 12) {
		float turretControl = max(1.0f - static_cast<float>(turretCount - 10) * 0.015f, 0.70f);
		stabilityMod *= turretControl;
	}

	// 4. Удаленность звезды от столицы по философии
	cStarRecord* homeStar = EmpireUtils::GetHomeStar(ownerEmpire);
	cStarRecord* currentStar = planet->GetStarRecord();
	if (homeStar && currentStar && homeStar != currentStar) {
		float dist = StarUtils::GetDistanceBetweenStars(homeStar, currentStar);

		if (baseArch == Archetypes::kArchetypeWarrior || baseArch == Archetypes::kArchetypeTrader) {
			if (dist > 12.0f) {
				float rimPenalty = min((dist - 12.0f) * 0.025f, 0.60f);
				stabilityMod *= (1.0f + rimPenalty);
			}
		} else if (baseArch == Archetypes::kArchetypeScientist) {
			if (dist > 25.0f) {
				float deepSpacePragmatism = min((dist - 25.0f) * 0.015f, 0.20f);
				stabilityMod *= (1.0f + deepSpacePragmatism);
			}
		} else if (baseArch == Archetypes::kArchetypeDiplomat ||
		           baseArch == Archetypes::kArchetypeEcologist ||
		           baseArch == Archetypes::kArchetypeBard ||
		           baseArch == Archetypes::kArchetypeShaman) {
			if (dist > 12.0f) {
				float autonomyBonus = min((dist - 12.0f) * 0.018f, 0.35f);
				stabilityMod *= (1.0f - autonomyBonus);
			}
		}
	}

	// 5. Забастовки жителей и перекос фабрик без развлечений
	if (cityUnrestScore > 0.01f) {
		stabilityMod *= (1.0f + cityUnrestScore * 0.55f);
	}

	// 6. Идеологическая симпатия/вражда (Affinity) с игроком во время штурма
	cEmpire* playerEmpire = GetPlayerEmpire();
	if (playerEmpire && root && root->empireRelationsAnalyzer && DiplomacyUtils::War(ownerEmpire, playerEmpire)) {
		int affinity = root->empireRelationsAnalyzer->EmpiresAffinity(ownerEmpire, playerEmpire);
		if (affinity > 0) {
			stabilityMod *= (1.0f + min(static_cast<float>(affinity) * 0.10f, 0.30f));
		} else if (affinity <= -2) {
			stabilityMod *= 0.80f;
		}
	}

	return stabilityMod;
}

static bool IsNpcPlanetInCombat(cPlanetRecord* activePlanet, cEmpire* ownerEmpire)
{
	if (!activePlanet || !ownerEmpire) return false;

	cEmpire* playerEmpire = GetPlayerEmpire();
	if (playerEmpire && RelationshipManager.IsAtWar(ownerEmpire, playerEmpire)) {
		return true;
	}

	const auto& ufos = GetData<cGameDataUFO>();
	for (const auto& ufo : ufos) {
		if (ufo && ufo->mHealthPoints > 0 && ufo->mPoliticalID != 0 && ufo->mPoliticalID != ownerEmpire->GetEmpireID()) {
			if (playerEmpire && ufo->mPoliticalID == playerEmpire->GetEmpireID()) {
				continue;
			}
			cEmpire* otherEmpire = StarManager.GetEmpire(ufo->mPoliticalID);
			if (!otherEmpire || RelationshipManager.IsAtWar(ownerEmpire, otherEmpire)) {
				return true;
			}
		}
	}
	return false;
}

static float GetBaseArchetypeMutinyChance(Simulator::Archetypes baseArch)
{
	if (baseArch == Archetypes::kArchetypeWarrior) return 0.50f;
	if (baseArch == Archetypes::kArchetypeTrader) return 0.40f;
	if (baseArch == Archetypes::kArchetypeScientist || baseArch == Archetypes::kArchetypeEcologist) return 0.15f;
	return 0.25f;
}

static bool ShouldTriggerNpcRebellion(cEmpire* ownerEmpire, cCompositionRoot* root, cGameDataUFO* newlySpawnedUfo)
{
	if (!ownerEmpire || !root || !root->spaceCombatMetrics) {
		return false;
	}

	if (GetCurrentContext() != SpaceContext::Planet) {
		return false;
	}

	cPlanetRecord* activePlanet = GetActivePlanetRecord();
	if (!activePlanet || !activePlanet->GetStarRecord() ||
		activePlanet->GetStarRecord()->mEmpireID != ownerEmpire->GetEmpireID()) {
		return false;
	}

	float rebellionMultiplier = ReadExternalConfigFloat(u"RebellionConfig", 0x07A1BC02, 1.0f);
	if (rebellionMultiplier <= 0.01f) {
		return false;
	}

	Simulator::Archetypes baseArch = ArchetypeUtils::GetBaseArchetype(ownerEmpire->mArchetype);
	if (baseArch == Archetypes::kArchetypeZealot || baseArch == Archetypes::kArchetypeGrob) {
		return false;
	}

	if (!IsNpcPlanetInCombat(activePlanet, ownerEmpire)) {
		return false;
	}

	float unrestMultiplier = ReadExternalConfigFloat(u"UnrestConfig", 0x07A1BC04, 1.0f);
	float cityUnrest = GetNpcPlanetaryCityUnrestFactor(activePlanet, unrestMultiplier);

	int maxCap = CalculateDynamicMaxTreasury(ownerEmpire, root);
	int crisisThreshold = static_cast<int>(round(static_cast<float>(maxCap) * 0.15f));
	bool isBankrupt = (ownerEmpire->mEmpireMoney > 0 && ownerEmpire->mEmpireMoney < crisisThreshold);

	int targetDefenders = min(root->spaceCombatMetrics->GetDefendersInPlanet(activePlanet), 24);
	int aliveDefenders = 0;
	const auto& ufos = GetData<cGameDataUFO>();
	for (const auto& ufo : ufos) {
		if (ufo && ufo != newlySpawnedUfo && ufo->mHealthPoints > 0 &&
			ufo->mPoliticalID == ownerEmpire->GetEmpireID() &&
			ufo->mUFOType == static_cast<int>(UfoType::Defender)) {
			aliveDefenders++;
		}
	}

	bool heavyLosses = false;
	if (targetDefenders <= 2) {
		heavyLosses = (aliveDefenders < targetDefenders);
	} else {
		heavyLosses = (aliveDefenders < (targetDefenders / 2));
	}

	bool severeCityStrike = (cityUnrest >= 0.45f);
	if (!isBankrupt && !heavyLosses && !severeCityStrike) {
		return false;
	}

	float chance = GetBaseArchetypeMutinyChance(baseArch);
	chance *= CalculateLocalNpcInstabilityMultiplier(activePlanet, ownerEmpire, root, cityUnrest);
	chance *= rebellionMultiplier;
	chance = min(max(chance, 0.01f), 0.90f);

	return (Math::randf() < chance);
}

static void TryTriggerNpcPeacetimeColonialUprising(cPlanetRecord* planet, cEmpire* ownerEmpire, cCompositionRoot* root)
{
	if (!planet || !ownerEmpire || !root) return;

	float rebellionMultiplier = ReadExternalConfigFloat(u"RebellionConfig", 0x07A1BC02, 1.0f);
	if (rebellionMultiplier <= 0.01f) return;

	Simulator::Archetypes baseArch = ArchetypeUtils::GetBaseArchetype(ownerEmpire->mArchetype);
	if (baseArch == Archetypes::kArchetypeZealot || baseArch == Archetypes::kArchetypeGrob) {
		return;
	}

	// Защита кулдауном (не чаще раза в 4 минуты реального времени на одну планету НПС)
	time_t now = std::time(nullptr);
	auto itCool = sNpcPlanetUprisingCooldowns.find(planet);
	if (itCool != sNpcPlanetUprisingCooldowns.end() && (now - itCool->second) < 240) {
		return;
	}

	float unrestMultiplier = ReadExternalConfigFloat(u"UnrestConfig", 0x07A1BC04, 1.0f);
	float cityUnrest = GetNpcPlanetaryCityUnrestFactor(planet, unrestMultiplier);

	float upkeepMultiplier = ReadExternalConfigFloat(u"UpkeepConfig", 0x07A1BC03, 1.0f);
	int maxCap = CalculateDynamicMaxTreasury(ownerEmpire, root);
	int crisisThreshold = static_cast<int>(round(static_cast<float>(maxCap) * 0.15f));
	bool severeFiscalCrisis = (upkeepMultiplier > 0.01f && ownerEmpire->mEmpireMoney > 0 && ownerEmpire->mEmpireMoney < crisisThreshold);
	bool severeCityStrike = (cityUnrest >= 0.50f);

	// В мирное время столица НПС не поднимает стихийную гражданскую войну без жесткой забастовки городов
	if (PlanetUtils::Homeworld(planet) && !severeCityStrike) {
		return;
	}

	if (!severeFiscalCrisis && !severeCityStrike) {
		return;
	}

	float uprisingChance = GetBaseArchetypeMutinyChance(baseArch) * 0.55f;
	uprisingChance *= CalculateLocalNpcInstabilityMultiplier(planet, ownerEmpire, root, cityUnrest);
	uprisingChance *= rebellionMultiplier;
	uprisingChance = min(max(uprisingChance, 0.0f), 0.75f);

	sNpcPlanetUprisingCooldowns[cPlanetRecordPtr(planet)] = now;

	if (Math::randf() >= uprisingChance) {
		return;
	}

	eastl::vector<cGameDataUFO*> loyalDefenders;
	const auto& ufos = GetData<cGameDataUFO>();
	for (const auto& ufo : ufos) {
		if (ufo && ufo->mHealthPoints > 0 &&
			ufo->mPoliticalID == ownerEmpire->GetEmpireID() &&
			ufo->mUFOType == static_cast<int>(UfoType::Defender)) {
			loyalDefenders.push_back(ufo.get());
		}
	}

	if (loyalDefenders.size() < 2) {
		return;
	}

	// От 1/4 до 2/5 орбитального гарнизона НПС переходит к путчистам и атакует ратуши, остальные остаются лоялистами
	float rebelShare = (severeCityStrike && severeFiscalCrisis) ? 0.42f : 0.28f;
	int rebelCount = max(static_cast<int>(round(static_cast<float>(loyalDefenders.size()) * rebelShare)), 1);
	rebelCount = min(rebelCount, static_cast<int>(loyalDefenders.size()) - 1);

	for (int i = 0; i < rebelCount; ++i) {
		cGameDataUFO* defector = loyalDefenders[i];
		if (defector) {
			defector->mPoliticalID = 0xFFFFFFFF;
			defector->mUFOType = static_cast<int>(UfoType::Bomber);
		}
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

		// Защитников игрока обрабатывает мод Defensive Ships In Player Colonies
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
		else if (typeInt == 4 || typeInt == 3) { // Raider / Fighter escort in UfoType enum
			customHP = root->spaceCombatMetrics->GetFighterHealthByEmpire(ownerEmpire);
		}

		if (customHP > 1.0f) {
			int maxCap = CalculateDynamicMaxTreasury(ownerEmpire, root);
			int eliteThreshold = static_cast<int>(round(static_cast<float>(maxCap) * 0.80f));
			int crisisThreshold = static_cast<int>(round(static_cast<float>(maxCap) * 0.15f));

			if (ownerEmpire->mStars.size() >= 3 && ownerEmpire->mEmpireMoney >= eliteThreshold) {
				customHP *= 1.20f;
			} else if (ownerEmpire->mEmpireMoney > 0 && ownerEmpire->mEmpireMoney < crisisThreshold) {
				customHP *= 0.75f;
			}

			ufo->mHealthPoints = customHP;
			ufo->mMaxHealthPoints = customHP;
		}

		// Мятеж защитников НПС при выпуске подкреплений во время активного боя
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
		if (!IsSpaceGame() || sIsGeneratingInitialPlanetGarrison) {
			original_function(this, planet);
			return;
		}

		sIsGeneratingInitialPlanetGarrison = true;
		original_function(this, planet);

		if (planet && planet->mpPlanetRecord) {
			cCompositionRoot* root = cCompositionRoot::Get();
			if (root && root->spaceCombatMetrics) {
				// Записываем точное число городов и турелей в кэш (для планет игрока и НПС)
				root->spaceCombatMetrics->UpdatePlanetDefenseCache(planet->mpPlanetRecord.get());

				cEmpire* planetEmpire = planet->GetEmpire();
				if (!planetEmpire && planet->mpPlanetRecord->GetStarRecord()) {
					planetEmpire = StarManager.GetEmpire(planet->mpPlanetRecord->GetStarRecord()->mEmpireID);
				}

				if (planetEmpire && planetEmpire != GetPlayerEmpire() &&
					root->simulationValidator && root->simulationValidator->ValidEmpire(planetEmpire, false, true)) {

					int targetDefenders = root->spaceCombatMetrics->GetDefendersInPlanet(planet->mpPlanetRecord.get());
					if (targetDefenders > 24) {
						targetDefenders = 24;
					}

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

					// Если у НПС на колонии забастовка заводов или казна слита войной — шанс застать гражданскую войну на орбите
					if (!IsNpcPlanetInCombat(planet->mpPlanetRecord.get(), planetEmpire)) {
						TryTriggerNpcPeacetimeColonialUprising(planet->mpPlanetRecord.get(), planetEmpire, root);
					}
				}
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
