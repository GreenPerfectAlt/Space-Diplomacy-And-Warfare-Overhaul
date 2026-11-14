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

static float GetPlanetSpicePrice(cPlanetRecord* planet)
{
	if (!planet || planet->mType == PlanetType::T0) {
		return 0.0f;
	}

	uint32_t spiceID = planet->mSpiceGen.instanceID;
	if (spiceID == 0) {
		return 0.0f;
	}

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

static int CalculateEmpireMaxTreasury(cEmpire* empire, cCompositionRoot* root)
{
	if (!empire || !root || !root->warfareStrengthAnalyzer) {
		return 200000;
	}

	float redSpiceRate = 0.5f;
	PropertyListPtr warfareProp;
	if (PropManager.GetPropertyList(id("WarfareConfig"), id("SwoConfig"), warfareProp) && warfareProp != nullptr) {
		App::Property::GetFloat(warfareProp.get(), 0xD07B1F69, redSpiceRate);
	}
	int unitCost = max(static_cast<int>(round(1000.0f / max(redSpiceRate, 0.1f))), 250);

	float bombersPerCycle = root->warfareStrengthAnalyzer->GetBombersProducedByEmpire(empire);
	Simulator::Archetypes baseArch = ArchetypeUtils::GetBaseArchetype(empire->mArchetype);
	float tradeMultiplier = (baseArch == Archetypes::kArchetypeTrader) ? 1.75f : 1.0f;

	int cycleIncome = static_cast<int>(round(bombersPerCycle * static_cast<float>(unitCost) * tradeMultiplier));
	int starCount = static_cast<int>(empire->mStars.size());

	int maxCap = static_cast<int>(round((50000.0f + starCount * 25000.0f + cycleIncome * 10.0f) * tradeMultiplier));
	return max(maxCap, 75000);
}

static bool IsNpcPlanetInCombat(cPlanetRecord* activePlanet, cEmpire* ownerEmpire)
{
	if (!activePlanet || !ownerEmpire) {
		return false;
	}

	cEmpire* playerEmpire = GetPlayerEmpire();
	if (playerEmpire && DiplomacyUtils::War(ownerEmpire, playerEmpire)) {
		return true;
	}

	if (MissionManager.ThereIsEventInPlanet(activePlanet)) {
		return true;
	}

	const auto& ufos = GetData<cGameDataUFO>();
	for (const auto& ufo : ufos) {
		if (ufo && ufo->mHealthPoints > 0 && ufo->mPoliticalID != 0 && ufo->mPoliticalID != ownerEmpire->GetEmpireID()) {
			if (ufo->mUFOType == static_cast<int>(UfoType::Defender)) {
				continue;
			}
			cEmpire* otherEmpire = StarManager.GetEmpire(ufo->mPoliticalID);
			if (!otherEmpire || DiplomacyUtils::War(ownerEmpire, otherEmpire)) {
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

	// Бунт возможен только во время реального боя на орбите
	if (!IsNpcPlanetInCombat(activePlanet, ownerEmpire)) {
		return false;
	}

	// Читаем настройку восстаний из RebellionConfig.prop (если установлен первый мод)
	float rebellionMultiplier = 1.0f;
	PropertyListPtr propListRebellion;
	if (PropManager.GetPropertyList(id(u"RebellionConfig"), 0x02ae0c7e, propListRebellion) && propListRebellion != nullptr) {
		App::Property::GetFloat(propListRebellion.get(), 0x07A1BC02, rebellionMultiplier);
	}
	if (rebellionMultiplier <= 0.01f) {
		return false;
	}

	// 1. Фанатики и Гроксы: иммунитет веры и коллективного разума
	Simulator::Archetypes baseArch = ArchetypeUtils::GetBaseArchetype(ownerEmpire->mArchetype);
	if (baseArch == Archetypes::kArchetypeZealot || baseArch == Archetypes::kArchetypeGrob) {
		return false;
	}

	// 2. Проверка финансового кризиса империи (< 15% от динамического потолка казны)
	int maxTreasury = CalculateEmpireMaxTreasury(ownerEmpire, root);
	int crisisThreshold = static_cast<int>(round(static_cast<float>(maxTreasury) * 0.15f));
	bool isBankrupt = (ownerEmpire->mEmpireMoney > 0 && ownerEmpire->mEmpireMoney < crisisThreshold);

	// 3. Проверка боевых потерь гарнизона (включая малые Т0/Т1 колонии!)
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
		// На малых Т0/Т1 аванпостах паника начинается, как только сбили хотя бы часть стартовой охраны
		heavyLosses = (aliveDefenders < targetDefenders);
	} else {
		heavyLosses = (aliveDefenders <= (targetDefenders / 2));
	}

	if (!isBankrupt && !heavyLosses) {
		return false;
	}

	// 4. Базовый шанс мятежа по философии
	float rebellionChance = 0.25f;
	if (baseArch == Archetypes::kArchetypeWarrior) {
		rebellionChance = 0.45f; // Военная хунта
	} else if (baseArch == Archetypes::kArchetypeTrader) {
		rebellionChance = 0.35f; // Наемный флот
	} else if (baseArch == Archetypes::kArchetypeScientist || baseArch == Archetypes::kArchetypeEcologist) {
		rebellionChance = 0.15f; // Технократическая дисциплина
	} else if (baseArch == Archetypes::kArchetypeDiplomat || baseArch == Archetypes::kArchetypeBard || baseArch == Archetypes::kArchetypeShaman) {
		rebellionChance = 0.18f; // Дружелюбное самоуправление
	}

	// 5. Фактор столицы и Терраиндекса планеты (Т0/Т1 каторги бунтуют сильно чаще, Т3 курорты лояльны)
	float planetFactor = 1.0f;
	if (PlanetUtils::Homeworld(activePlanet)) {
		planetFactor = 0.20f; // Столичная гвардия почти непоколебима
	} else if (activePlanet->mType == PlanetType::T0) {
		planetFactor = 1.55f; // Суровый каторжный аванпост без атмосферы
	} else if (activePlanet->mType == PlanetType::T1) {
		planetFactor = 1.25f; // Бедная пограничная колония
	} else if (activePlanet->mType == PlanetType::T3) {
		planetFactor = 0.60f; // Райский развитый мир
	}

	// 6. Фактор местной пряности (дорогая пряность кормит гарнизон, дешевая копит недовольство)
	float spiceFactor = 1.0f;
	float spicePrice = GetPlanetSpicePrice(activePlanet);
	if (spicePrice > 3500.0f) {
		spiceFactor = 0.65f; // Экзотическая пряность
	} else if (spicePrice >= 2000.0f) {
		spiceFactor = 0.75f; // Фиолетовая / Розовая
	} else if (spicePrice >= 800.0f) {
		spiceFactor = 0.90f; // Синяя / Зеленая
	} else {
		spiceFactor = 1.15f; // Дешевая Красная / Желтая или пустой Т0
	}

	// 7. Фактор ратушей и турелей (полицейский контроль мегаполисов vs одинокое поселение)
	int cityCount = 0;
	int turretCount = 0;
	const auto& cities = GetData<cCity>();
	for (const auto& city : cities) {
		if (city) {
			cityCount++;
			turretCount += static_cast<int>(city->mTurrets.size());
		}
	}
	float urbanControlFactor = 1.0f;
	if (cityCount <= 1 && !PlanetUtils::Homeworld(activePlanet)) {
		urbanControlFactor = 1.15f; // Одинокая колония без контроля центра
	} else if (cityCount > 1) {
		urbanControlFactor = max(0.50f, 1.0f - static_cast<float>(cityCount - 1) * 0.07f);
	}
	if (turretCount >= 12) {
		urbanControlFactor *= 0.85f; // Укрепленный бастион с гарнизонами ПВО
	}

	// 8. Удаленность от столицы + философская доктрина окраин!
	// Авторитарные/наемные империи теряют контроль вдали от столицы (сепаратизм растет),
	// а дружелюбные гармоничные философии дают колониям автономию (на окраинах спокойно!)
	float distanceFactor = 1.0f;
	cStarRecord* homeStar = EmpireUtils::GetHomeStar(ownerEmpire);
	cStarRecord* currentStar = activePlanet->GetStarRecord();
	if (homeStar && currentStar && homeStar != currentStar) {
		float dist = StarUtils::GetDistanceBetweenStars(homeStar, currentStar);
		const float coreRadius = 12.0f;

		if (dist > coreRadius) {
			float excessParsecs = min(dist - coreRadius, 40.0f);
			if (baseArch == Archetypes::kArchetypeWarrior || baseArch == Archetypes::kArchetypeTrader) {
				// Жесткий диктат Воинов и жадность Торговцев вызывают бунты на дальних рубежах (до +60%)
				distanceFactor = 1.0f + (excessParsecs * 0.015f);
			} else if (baseArch == Archetypes::kArchetypeDiplomat || baseArch == Archetypes::kArchetypeEcologist ||
			           baseArch == Archetypes::kArchetypeBard || baseArch == Archetypes::kArchetypeShaman) {
				// Дружелюбные философии живут в гармонии и самоуправлении — дальние миры еще спокойнее (до -35%)
				distanceFactor = max(0.65f, 1.0f - (excessParsecs * 0.009f));
			} else {
				// Ученые: умеренный рост риска из-за растянутой связи (до +20%)
				distanceFactor = 1.0f + (excessParsecs * 0.005f);
			}
		} else {
			// Внутри столичного сектора (< 12 пк) у всех повышенный порядок
			distanceFactor = 0.80f;
		}
	}

	// 9. Бонус/штраф симпатии (Affinity) к игроку, если штурмует сам игрок:
	// Если идеология атакующего игрока близка жителям колонии (положительный Affinity), гарнизон охотнее свергает своих правителей!
	float affinitySubversionFactor = 1.0f;
	cEmpire* playerEmpire = GetPlayerEmpire();
	if (playerEmpire && root->empireRelationsAnalyzer && DiplomacyUtils::War(ownerEmpire, playerEmpire)) {
		int affinity = root->empireRelationsAnalyzer->EmpiresAffinity(ownerEmpire, playerEmpire);
		if (affinity > 0) {
			affinitySubversionFactor = 1.0f + min(affinity, 4) * 0.08f; // Симпатия к культуре игрока усиливает дезертирство
		} else if (affinity <= -2) {
			affinitySubversionFactor = 0.85f; // Идеологическая ненависть к захватчику сплачивает защитников
		}
	}

	float finalChance = rebellionChance * planetFactor * spiceFactor * urbanControlFactor * distanceFactor * affinitySubversionFactor * rebellionMultiplier;
	finalChance = min(max(finalChance, 0.02f), 0.85f);

	return (Math::randf() < finalChance);
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

		// Защитников игрока обрабатывает первый мод Defensive Ships In Player Colonies
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
			int maxTreasury = CalculateEmpireMaxTreasury(ownerEmpire, root);
			int richThreshold = static_cast<int>(round(static_cast<float>(maxTreasury) * 0.80f));
			int crisisThreshold = static_cast<int>(round(static_cast<float>(maxTreasury) * 0.15f));

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
		sIsGeneratingInitialPlanetGarrison = true;
		original_function(this, planet);

		if (!IsSpaceGame() || !planet || !planet->mpPlanetRecord) {
			sIsGeneratingInitialPlanetGarrison = false;
			return;
		}

		cCompositionRoot* root = cCompositionRoot::Get();
		if (!root || !root->spaceCombatMetrics || !root->simulationValidator) {
			sIsGeneratingInitialPlanetGarrison = false;
			return;
		}

		// Записываем точное число городов и турелей в галактический кэш разведки
		root->spaceCombatMetrics->UpdatePlanetDefenseCache(planet->mpPlanetRecord.get());

		cEmpire* planetEmpire = planet->GetEmpire();
		if (!planetEmpire || planetEmpire == GetPlayerEmpire()) {
			sIsGeneratingInitialPlanetGarrison = false;
			return;
		}

		if (!root->simulationValidator->ValidEmpire(planetEmpire, false, true)) {
			sIsGeneratingInitialPlanetGarrison = false;
			return;
		}

		int targetDefenders = root->spaceCombatMetrics->GetDefendersInPlanet(planet->mpPlanetRecord.get());
		if (targetDefenders <= 0) {
			sIsGeneratingInitialPlanetGarrison = false;
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
