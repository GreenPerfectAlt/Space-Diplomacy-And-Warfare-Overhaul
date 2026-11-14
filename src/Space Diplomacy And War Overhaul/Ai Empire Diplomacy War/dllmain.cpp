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
#include <cmath>

using namespace SporeModUtils;
using namespace Simulator;

void Initialize()
{
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
static bool sIsSpawningExtraNpcDefenders = false;

static int CalculateDynamicMaxTreasury(cEmpire* empire, cCompositionRoot* root)
{
	if (!empire || !root || !root->warfareStrengthAnalyzer || !root->warfareStrengthAnalyzer->Cast(cWarfareStrengthAnalyzer::TYPE)) {
		return 2000000;
	}

	float redSpiceFactor = 1.0f;
	PropertyListPtr warfareConfigProp;
	if (PropManager.GetPropertyList(id("WarfareConfig"), id("SwoConfig"), warfareConfigProp) && warfareConfigProp != nullptr) {
		App::Property::GetFloat(warfareConfigProp.get(), 0xD07B1F69, redSpiceFactor);
	}
	int unitCost = max(static_cast<int>(std::round(1000.0f / max(redSpiceFactor, 0.1f))), 250);

	float bombersOutput = root->warfareStrengthAnalyzer->GetBombersProducedByEmpire(empire);
	int starCount = static_cast<int>(empire->mStars.size());

	Simulator::Archetypes baseArch = ArchetypeUtils::GetBaseArchetype(empire->mArchetype);
	float tradeMult = (baseArch == Archetypes::kArchetypeTrader) ? 1.75f : 1.0f;

	int cycleIncome = static_cast<int>(std::round(bombersOutput * static_cast<float>(unitCost) * tradeMult));
	int maxCap = static_cast<int>(std::round((50000.0f + starCount * 25000.0f + cycleIncome * 10.0f) * tradeMult));
	return max(maxCap, 60000);
}

static bool IsNpcPlanetInCombat(cEmpire* ownerEmpire)
{
	if (!ownerEmpire || GetCurrentContext() != SpaceContext::Planet) {
		return false;
	}

	cEmpire* playerEmp = GetPlayerEmpire();
	if (playerEmp && DiplomacyUtils::War(ownerEmpire, playerEmp)) {
		return true;
	}

	const auto& ufos = GetData<cGameDataUFO>();
	for (const auto& ufo : ufos) {
		if (ufo && ufo->mHealthPoints > 0 && ufo->mPoliticalID != 0 && ufo->mPoliticalID != ownerEmpire->GetEmpireID()) {
			if (ufo->mUFOType == static_cast<int>(UfoType::Defender)) {
				continue;
			}
			cEmpire* otherEmp = StarManager.GetEmpire(ufo->mPoliticalID);
			if (!otherEmp || DiplomacyUtils::War(ownerEmpire, otherEmp)) {
				return true;
			}
		}
	}
	return false;
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

	if (!IsNpcPlanetInCombat(ownerEmpire)) {
		return false;
	}

	float rebellionMultiplier = 1.0f;
	PropertyListPtr propListRebellion;
	if (PropManager.GetPropertyList(id(u"RebellionConfig"), 0x02ae0c7e, propListRebellion) && propListRebellion != nullptr) {
		App::Property::GetFloat(propListRebellion.get(), 0x07A1BC02, rebellionMultiplier);
	}
	if (rebellionMultiplier <= 0.01f) {
		return false;
	}

	Simulator::Archetypes baseArch = ArchetypeUtils::GetBaseArchetype(ownerEmpire->mArchetype);
	if (baseArch == Archetypes::kArchetypeZealot || baseArch == Archetypes::kArchetypeGrob) {
		return false;
	}

	int maxTreasury = CalculateDynamicMaxTreasury(ownerEmpire, root);
	int crisisThreshold = static_cast<int>(std::round(static_cast<float>(maxTreasury) * 0.15f));
	bool isBankrupt = (ownerEmpire->mEmpireMoney > 0 && ownerEmpire->mEmpireMoney < crisisThreshold);

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
	bool heavyLosses = (targetDefenders > 2 && aliveDefenders < (targetDefenders / 2));

	if (!isBankrupt && !heavyLosses) {
		return false;
	}

	float rebellionChance = 0.25f;
	if (baseArch == Archetypes::kArchetypeWarrior) {
		rebellionChance = 0.50f;
	} else if (baseArch == Archetypes::kArchetypeTrader) {
		rebellionChance = 0.40f;
	} else if (baseArch == Archetypes::kArchetypeScientist || baseArch == Archetypes::kArchetypeEcologist) {
		rebellionChance = 0.15f;
	}

	rebellionChance *= rebellionMultiplier;
	return (Math::randf() < rebellionChance);
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
		else if (typeInt == 4 || typeInt == 3) {
			customHP = root->spaceCombatMetrics->GetFighterHealthByEmpire(ownerEmpire);
		}

		if (customHP > 1.0f) {
			int maxTreasury = CalculateDynamicMaxTreasury(ownerEmpire, root);
			int wealthyThreshold = static_cast<int>(std::round(static_cast<float>(maxTreasury) * 0.80f));
			int crisisThreshold = static_cast<int>(std::round(static_cast<float>(maxTreasury) * 0.15f));

			if (ownerEmpire->mStars.size() >= 3 && ownerEmpire->mEmpireMoney > wealthyThreshold) {
				customHP *= 1.20f;
			} else if (ownerEmpire->mEmpireMoney > 0 && ownerEmpire->mEmpireMoney < crisisThreshold) {
				customHP *= 0.75f;
			}

			ufo->mHealthPoints = customHP;
			ufo->mMaxHealthPoints = customHP;
		}

		if (ufoType == UfoType::Defender && !sIsGeneratingInitialPlanetGarrison && !sIsSpawningExtraNpcDefenders) {
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
		sIsGeneratingInitialPlanetGarrison = true;
		original_function(this, planet);
		sIsGeneratingInitialPlanetGarrison = false;

		if (!IsSpaceGame() || sIsSpawningExtraNpcDefenders || !planet || !planet->mpPlanetRecord) {
			return;
		}

		cCompositionRoot* root = cCompositionRoot::Get();
		if (!root || !root->spaceCombatMetrics || !root->simulationValidator) {
			return;
		}

		// Сразу записываем в кэш число городов и турелей на любой посещенной планете (включая колонии игрока с Unlimited-Colony)
		root->spaceCombatMetrics->UpdatePlanetDefenseCache(planet->mpPlanetRecord.get());

		cEmpire* planetEmpire = planet->GetEmpire();
		if (!planetEmpire && planet->mpPlanetRecord->GetStarRecord() != nullptr) {
			planetEmpire = StarManager.GetEmpire(planet->mpPlanetRecord->GetStarRecord()->mEmpireID);
		}
		if (!planetEmpire || planetEmpire == GetPlayerEmpire()) {
			return;
		}

		if (!root->simulationValidator->ValidEmpire(planetEmpire, false, true)) {
			return;
		}

		int targetDefenders = root->spaceCombatMetrics->GetDefendersInPlanet(planet->mpPlanetRecord.get());
		if (targetDefenders <= 0) {
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
			sIsSpawningExtraNpcDefenders = true;
			int* pPolID = &planetEmpire->mPoliticalID;
			ResourceKey* pKey = &planetEmpire->mUFOKey;
			for (int i = 0; i < toSpawn; ++i) {
				STATIC_CALL(Address(ModAPI::ChooseAddress(0x0102bbb0, 0x0102acc0)),
					cGameDataUFO*,
					Args(UfoType, int*, ResourceKey*),
					Args(UfoType::Defender, pPolID, pKey));
			}
			sIsSpawningExtraNpcDefenders = false;
		}
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
