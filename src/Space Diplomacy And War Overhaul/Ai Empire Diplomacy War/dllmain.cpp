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
#include <Spore/Simulator/cCity.h>
#include <Spore/Simulator/cBuilding.h>
#include <EASTL/map.h>
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
	PropertyListPtr propList;
	if (PropManager.GetPropertyList(id(propName), 0x02ae0c7e, propList) && propList != nullptr) {
		float val = defaultVal;
		if (App::Property::GetFloat(propList.get(), propID, val)) {
			return val;
		}
	}
	return defaultVal;
}

static int CalculateNpcMaxTreasury(cEmpire* empire, cCompositionRoot* root)
{
	if (!empire || !root || !root->warfareStrengthAnalyzer || !root->warfareConfig) {
		return 150000;
	}

	float bombersPerCycle = max(root->warfareStrengthAnalyzer->GetBombersProducedByEmpire(empire), 1.0f);
	int baseBomberCost = max(static_cast<int>(round(1000.0f / max(root->warfareConfig->GetBombersPerRedSpiceFactory(), 0.1f))), 250);
	int starCount = max(static_cast<int>(empire->mStars.size()), 1);

	Simulator::Archetypes baseArch = ArchetypeUtils::GetBaseArchetype(empire->mArchetype);
	float tradeMultiplier = (baseArch == Archetypes::kArchetypeTrader) ? 1.75f : 1.0f;

	int cycleIncome = static_cast<int>(round(bombersPerCycle * static_cast<float>(baseBomberCost) * 0.45f * tradeMultiplier));
	int maxTreasury = 50000 + (starCount * 25000) + static_cast<int>(round(static_cast<float>(cycleIncome * 10) * tradeMultiplier));
	return max(maxTreasury, 80000);
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

	if (spiceID == 0x0289B841 || spiceID == 0x0289B842) return 3000.0f;
	if (spiceID == 0x0289B83F || spiceID == 0x0289B840) return 1200.0f;
	return 300.0f;
}

static float GetNpcPlanetaryCityUnrestFactor(cPlanetRecord* planet)
{
	float unrestMultiplier = ReadExternalConfigFloat(u"UnrestConfig", 0x07A1BC04, 1.0f);
	if (!planet || unrestMultiplier <= 0.01f) {
		return 0.0f;
	}

	if (GetCurrentContext() != SpaceContext::Planet || planet != GetActivePlanetRecord()) {
		return 0.0f;
	}

	int totalCities = 0;
	int totalUnhappy = 0;
	int factoryCount = 0;
	int entertainmentCount = 0;

	const auto& cities = GetData<cCity>();
	for (const auto& city : cities) {
		if (!city) continue;
		totalCities++;
		totalUnhappy += max(city->mUnhappyCount, 0);

		for (const auto& bld : city->mBuildings) {
			if (!bld) continue;
			uint32_t nounID = bld->GetNounID();
			if (nounID == Simulator::GameNounIDs::kBuildingIndustry) {
				factoryCount++;
			}
			else if (nounID == Simulator::GameNounIDs::kBuildingEntertainment) {
				entertainmentCount++;
			}
		}
	}

	if (totalCities <= 0) {
		return 0.0f;
	}

	float avgUnhappy = static_cast<float>(totalUnhappy) / static_cast<float>(totalCities);
	float unhappyScore = min(avgUnhappy * 0.25f, 0.75f);

	float industrialOverload = 0.0f;
	if (factoryCount > entertainmentCount * 2) {
		float excessFactories = static_cast<float>(factoryCount - entertainmentCount * 2);
		industrialOverload = min((excessFactories / static_cast<float>(totalCities * 4)) * 0.45f, 0.45f);
	}

	float rawUnrest = min(unhappyScore + industrialOverload, 1.0f);
	return min(rawUnrest * unrestMultiplier, 1.5f);
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
		if (!ufo || ufo->mHealthPoints <= 0 || ufo->mPoliticalID == 0) {
			continue;
		}
		if (ufo->mPoliticalID == ownerEmpire->GetEmpireID()) {
			continue;
		}

		cEmpire* otherEmpire = StarManager.GetEmpire(ufo->mPoliticalID);
		if (!otherEmpire) {
			return true; // Пираты или мятежники (0xFFFFFFFF)
		}
		if (RelationshipManager.IsAtWar(ownerEmpire, otherEmpire)) {
			return true;
		}
	}
	return false;
}

static float CalculateNpcLocalInstabilityChance(cPlanetRecord* activePlanet, cEmpire* ownerEmpire, cCompositionRoot* root, float cityUnrest, bool isBankrupt)
{
	if (!activePlanet || !ownerEmpire || !root) {
		return 0.0f;
	}

	float rebellionMultiplier = ReadExternalConfigFloat(u"RebellionConfig", 0x07A1BC02, 1.0f);
	if (rebellionMultiplier <= 0.01f) {
		return 0.0f;
	}

	Simulator::Archetypes baseArch = ArchetypeUtils::GetBaseArchetype(ownerEmpire->mArchetype);
	if (baseArch == Archetypes::kArchetypeZealot || baseArch == Archetypes::kArchetypeGrob) {
		return 0.0f;
	}

	// 1. Базовый риск по философии
	float rebellionChance = 0.25f;
	if (baseArch == Archetypes::kArchetypeWarrior) {
		rebellionChance = 0.45f;
	}
	else if (baseArch == Archetypes::kArchetypeTrader) {
		rebellionChance = 0.38f;
	}
	else if (baseArch == Archetypes::kArchetypeScientist || baseArch == Archetypes::kArchetypeEcologist) {
		rebellionChance = 0.14f;
	}

	// 2. Терраиндекс и столица
	float planetTierFactor = 1.0f;
	if (PlanetUtils::Homeworld(activePlanet)) {
		planetTierFactor = 0.20f;
	}
	else if (activePlanet->mType == PlanetType::T0) {
		planetTierFactor = 1.55f;
	}
	else if (activePlanet->mType == PlanetType::T1) {
		planetTierFactor = 1.25f;
	}
	else if (activePlanet->mType == PlanetType::T3) {
		planetTierFactor = 0.60f;
	}

	// 3. Местная пряность
	float spicePrice = GetPlanetSpicePrice(activePlanet);
	float spiceStabilityFactor = 1.0f;
	if (spicePrice > 3500.0f) {
		spiceStabilityFactor = 0.65f;
	}
	else if (spicePrice >= 2000.0f) {
		spiceStabilityFactor = 0.75f;
	}
	else if (spicePrice > 0.0f && spicePrice < 600.0f) {
		spiceStabilityFactor = 1.15f;
	}

	// 4. Плотность ратушей и турелей ПВО
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

	float garrisonControlFactor = 1.0f;
	if (cityCount == 1) {
		garrisonControlFactor = 1.15f;
	}
	else if (cityCount > 1) {
		garrisonControlFactor = max(1.0f - static_cast<float>(cityCount - 1) * 0.08f, 0.50f);
	}
	if (turretCount >= 12) {
		garrisonControlFactor *= 0.85f;
	}

	// 5. Дистанция от столицы по философии
	float distanceFactor = 1.0f;
	cStarRecord* homeStar = EmpireUtils::GetHomeStar(ownerEmpire);
	if (!PlanetUtils::Homeworld(activePlanet) && homeStar != nullptr && activePlanet->GetStarRecord() != nullptr) {
		float dist = StarUtils::GetDistanceBetweenStars(homeStar, activePlanet->GetStarRecord());
		if (baseArch == Archetypes::kArchetypeWarrior || baseArch == Archetypes::kArchetypeTrader) {
			if (dist > 12.0f) {
				distanceFactor = min(1.0f + (dist - 12.0f) * 0.025f, 1.60f);
			}
		}
		else if (baseArch == Archetypes::kArchetypeDiplomat || baseArch == Archetypes::kArchetypeEcologist ||
			baseArch == Archetypes::kArchetypeBard || baseArch == Archetypes::kArchetypeShaman) {
			if (dist > 10.0f) {
				distanceFactor = max(1.0f - (dist - 10.0f) * 0.015f, 0.65f);
			}
		}
		else if (baseArch == Archetypes::kArchetypeScientist) {
			if (dist > 25.0f) {
				distanceFactor = min(1.0f + (dist - 25.0f) * 0.01f, 1.20f);
			}
		}
	}

	// 6. Забастовки городов и казна
	float crisisSeverity = 1.0f + (cityUnrest * 0.65f);
	if (isBankrupt) {
		crisisSeverity += 0.25f;
	}

	rebellionChance *= planetTierFactor * spiceStabilityFactor * garrisonControlFactor * distanceFactor * crisisSeverity * rebellionMultiplier;
	return min(rebellionChance, 0.85f);
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

	if (!IsNpcPlanetInCombat(activePlanet, ownerEmpire)) {
		return false;
	}

	float upkeepMult = ReadExternalConfigFloat(u"UpkeepConfig", 0x07A1BC03, 1.0f);
	int maxTreasury = CalculateNpcMaxTreasury(ownerEmpire, root);
	int crisisThreshold = static_cast<int>(round(static_cast<float>(maxTreasury) * 0.15f));
	bool isBankrupt = (upkeepMult > 0.01f) && (ownerEmpire->mEmpireMoney > 0 && ownerEmpire->mEmpireMoney < crisisThreshold);

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
		heavyLosses = (targetDefenders > 0 && aliveDefenders < targetDefenders);
	}
	else {
		heavyLosses = (aliveDefenders < (targetDefenders / 2));
	}

	float cityUnrest = GetNpcPlanetaryCityUnrestFactor(activePlanet);
	bool severeCityUnrest = (cityUnrest >= 0.45f);

	if (!isBankrupt && !heavyLosses && !severeCityUnrest) {
		return false;
	}

	float rebellionChance = CalculateNpcLocalInstabilityChance(activePlanet, ownerEmpire, root, cityUnrest, isBankrupt);
	if (rebellionChance <= 0.001f) {
		return false;
	}

	// Идеологическая вербовка, если штурмует игрок
	cEmpire* playerEmpire = GetPlayerEmpire();
	if (playerEmpire && root->empireRelationsAnalyzer && RelationshipManager.IsAtWar(ownerEmpire, playerEmpire)) {
		int affinityWithPlayer = root->empireRelationsAnalyzer->EmpiresAffinity(ownerEmpire, playerEmpire);
		if (affinityWithPlayer > 0) {
			rebellionChance *= 1.20f;
		}
		else if (affinityWithPlayer <= -2) {
			rebellionChance *= 0.85f;
		}
	}

	return (Math::randf() < min(rebellionChance, 0.85f));
}

static void TryTriggerNpcPeacetimeColonialUprising(cPlanetRecord* planet, cEmpire* ownerEmpire, cCompositionRoot* root)
{
	if (!planet || !ownerEmpire || !root || IsNpcPlanetInCombat(planet, ownerEmpire)) {
		return;
	}

	uint32_t currentSpaceTime = (root->persistenceState != nullptr) ? root->persistenceState->GetCurrentSpaceTime() : 0;
	auto itCd = sNpcPlanetUprisingCooldowns.find(planet);
	if (itCd != sNpcPlanetUprisingCooldowns.end() && currentSpaceTime > 0 && currentSpaceTime < itCd->second) {
		return;
	}

	float upkeepMult = ReadExternalConfigFloat(u"UpkeepConfig", 0x07A1BC03, 1.0f);
	int maxTreasury = CalculateNpcMaxTreasury(ownerEmpire, root);
	int crisisThreshold = static_cast<int>(round(static_cast<float>(maxTreasury) * 0.15f));
	bool isBankrupt = (upkeepMult > 0.01f) && (ownerEmpire->mEmpireMoney > 0 && ownerEmpire->mEmpireMoney < crisisThreshold);

	float cityUnrest = GetNpcPlanetaryCityUnrestFactor(planet);
	bool hasCityStrike = (cityUnrest >= 0.35f);

	// В мирное время восстание вспыхивает только при кризисе казны ИИ или забастовке заводов
	if (!isBankrupt && !hasCityStrike) {
		return;
	}

	// Столицы ИИ не бунтуют в мирное время без экстремальной забастовки городов
	if (PlanetUtils::Homeworld(planet) && cityUnrest < 0.80f) {
		return;
	}

	float uprisingChance = CalculateNpcLocalInstabilityChance(planet, ownerEmpire, root, cityUnrest, isBankrupt) * 0.65f;
	if (uprisingChance <= 0.02f) {
		return;
	}

	if (Math::randf() >= uprisingChance) {
		if (currentSpaceTime > 0) {
			sNpcPlanetUprisingCooldowns[cPlanetRecordPtr(planet)] = currentSpaceTime + 120000;
		}
		return;
	}

	eastl::vector<cGameDataUFO*> loyalPatrol;
	const auto& ufos = GetData<cGameDataUFO>();
	for (const auto& ufo : ufos) {
		if (ufo && ufo->mHealthPoints > 0 &&
			ufo->mPoliticalID == ownerEmpire->GetEmpireID() &&
			ufo->mUFOType == static_cast<int>(UfoType::Defender)) {
			loyalPatrol.push_back(ufo.get());
		}
	}

	if (loyalPatrol.size() < 2) {
		return;
	}

	// От 1/3 до половины орбитального патруля поднимает мятеж и атакует ратуши на глазах у лоялистов
	float mutinyShare = (cityUnrest >= 0.60f) ? 0.42f : 0.28f;
	int rebelCount = max(static_cast<int>(round(static_cast<float>(loyalPatrol.size()) * mutinyShare)), 1);
	rebelCount = min(rebelCount, static_cast<int>(loyalPatrol.size()) - 1);

	for (int i = 0; i < rebelCount; ++i) {
		cGameDataUFO* rebel = loyalPatrol[i];
		rebel->mPoliticalID = 0xFFFFFFFF;
		rebel->mUFOType = static_cast<int>(UfoType::Bomber);
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
			int maxTreasury = CalculateNpcMaxTreasury(ownerEmpire, root);
			int overfundedThreshold = static_cast<int>(round(static_cast<float>(maxTreasury) * 0.80f));
			int crisisThreshold = static_cast<int>(round(static_cast<float>(maxTreasury) * 0.15f));

			if (ownerEmpire->mStars.size() >= 3 && ownerEmpire->mEmpireMoney >= overfundedThreshold) {
				customHP *= 1.20f;
			}
			else if (ownerEmpire->mEmpireMoney > 0 && ownerEmpire->mEmpireMoney < crisisThreshold) {
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
		bool wasGenerating = sIsGeneratingInitialPlanetGarrison;
		sIsGeneratingInitialPlanetGarrison = true;

		original_function(this, planet);

		if (!IsSpaceGame() || wasGenerating || !planet || !planet->mpPlanetRecord) {
			sIsGeneratingInitialPlanetGarrison = wasGenerating;
			return;
		}

		cCompositionRoot* root = cCompositionRoot::Get();
		if (root && root->spaceCombatMetrics) {
			root->spaceCombatMetrics->UpdatePlanetDefenseCache(planet->mpPlanetRecord.get());
		}

		cEmpire* planetEmpire = planet->GetEmpire();
		if (!planetEmpire || planetEmpire == GetPlayerEmpire()) {
			sIsGeneratingInitialPlanetGarrison = wasGenerating;
			return;
		}

		if (!root || !root->spaceCombatMetrics || !root->simulationValidator ||
			!root->simulationValidator->ValidEmpire(planetEmpire, false, true)) {
			sIsGeneratingInitialPlanetGarrison = wasGenerating;
			return;
		}

		int targetDefenders = root->spaceCombatMetrics->GetDefendersInPlanet(planet->mpPlanetRecord.get());
		if (targetDefenders <= 0) {
			sIsGeneratingInitialPlanetGarrison = wasGenerating;
			return;
		}

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

		sIsGeneratingInitialPlanetGarrison = wasGenerating;

		// После генерации гарнизона проверяем мирное колониальное восстание (гражданскую войну) у НПС
		TryTriggerNpcPeacetimeColonialUprising(planet->mpPlanetRecord.get(), planetEmpire, root);
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
