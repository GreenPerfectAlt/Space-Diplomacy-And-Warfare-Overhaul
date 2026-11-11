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

using namespace SporeModUtils;
using namespace Simulator;

namespace {
	bool gIsSpawningExtraNpcDefenders = false;
}

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
	ClassManager.AddFactory(new cPersistedDiplomacyEventFactory);
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

/// Prevent cosmetic galaxy Raider UFO crashes
member_detour(ShouldSpawnRaiderUFO__detour, cRaidEvent, bool())
{
	bool detoured()
	{
		bool ret = original_function(this);
		return false;
	}
};

/// Применяет реальные ХП по философии (Воины, Ученые, Барды и т.д.) ко всем создаваемым НЛО в бою,
/// не перезаписывая защитников игрока (чтобы не конфликтовать с модом Defensive Ships In Player Colonies).
static_detour(SpawnUFO__detour, cGameDataUFO*(UfoType, int*, ResourceKey*))
{
	cGameDataUFO* detoured(UfoType ufoType, int* pPoliticalID, ResourceKey* pUfoKey)
	{
		cGameDataUFO* ufo = original_function(ufoType, pPoliticalID, pUfoKey);
		if (!IsSpaceGame() || ufo == nullptr || pPoliticalID == nullptr) {
			return ufo;
		}

		cCompositionRoot* root = cCompositionRoot::Get();
		if (root == nullptr || root->spaceCombatMetrics == nullptr) {
			return ufo;
		}

		uint32_t polID = static_cast<uint32_t>(*pPoliticalID);
		if (polID == 0 || polID == 0xFFFFFFFF) {
			return ufo;
		}

		// Защитников на планетах игрока оставляем под управлением первого мода (Defensive Ships In Player Colonies)
		if (ufoType == UfoType::Defender && polID == GetPlayerEmpireID()) {
			return ufo;
		}

		cEmpire* ownerEmpire = StarManager.GetEmpire(polID);
		if (ownerEmpire == nullptr) {
			return ufo;
		}

		float customHP = 0.0f;
		if (ufoType == UfoType::Bomber || ufoType == UfoType::Raider) {
			customHP = root->spaceCombatMetrics->GetBomberHealthByEmpire(ownerEmpire);
		}
		else if (ufoType == UfoType::Fighter) {
			customHP = root->spaceCombatMetrics->GetFighterHealthByEmpire(ownerEmpire);
		}
		else if (ufoType == UfoType::Defender) {
			customHP = root->spaceCombatMetrics->GetDefenderHealthByEmpire(ownerEmpire);
		}

		if (customHP > 1.0f) {
			ufo->mHealthPoints = customHP;
			ufo->mMaxHealthPoints = customHP;
		}

		return ufo;
	}
};

/// Спавнит реальный усиленный гарнизон защитников на планетах ИИ-империй (Воины, Ученые, Столицы и т.д.),
/// когда игрок залетает на орбиту вражеской или союзной NPC-планеты.
member_detour(GenerateNpcPlanetDefenders__detour, cSimPlanetHighLOD, void(cPlanet* planet))
{
	void detoured(cPlanet* planet)
	{
		original_function(this, planet);

		if (!IsSpaceGame() || gIsSpawningExtraNpcDefenders || planet == nullptr || planet->mpPlanetRecord == nullptr) {
			return;
		}

		cEmpire* planetEmpire = planet->GetEmpire();
		if (planetEmpire == nullptr || planetEmpire == GetPlayerEmpire()) {
			return; // Планеты игрока обрабатывает мод Defensive Ships In Player Colonies
		}

		cPlanetRecord* planetRec = planet->mpPlanetRecord.get();
		if (planetRec->GetTechLevel() != TechLevel::Empire) {
			return;
		}

		cCompositionRoot* root = cCompositionRoot::Get();
		if (root == nullptr || root->spaceCombatMetrics == nullptr) {
			return;
		}

		int desiredDefenders = root->spaceCombatMetrics->GetDefendersInPlanet(planetRec);

		// Считаем, сколько ванильных защитников этой империи уже успела создать игра при входе на планету
		int existingDefenders = 0;
		const auto& ufos = GetData<cGameDataUFO>();
		for (const auto& ufo : ufos) {
			if (ufo != nullptr && ufo->mHealthPoints > 0 &&
			    ufo->mPoliticalID == planetEmpire->mPoliticalID &&
			    ufo->mUFOType == static_cast<int>(UfoType::Defender)) {
				existingDefenders++;
			}
		}

		int toSpawn = desiredDefenders - existingDefenders;
		if (toSpawn > 0) {
			// Ограничиваем разовый спавн на орбите разумным пределом для FPS
			if (toSpawn > 20) toSpawn = 20;

			gIsSpawningExtraNpcDefenders = true;
			int* pPolID = &planetEmpire->mPoliticalID;
			ResourceKey* pUfoKey = &planetEmpire->mUFOKey;

			for (int i = 0; i < toSpawn; ++i) {
				STATIC_CALL(
					Address(ModAPI::ChooseAddress(0x0102bbb0, 0x0102acc0)),
					cGameDataUFO*,
					Args(UfoType, int*, ResourceKey*),
					Args(UfoType::Defender, pPolID, pUfoKey)
				);
			}
			gIsSpawningExtraNpcDefenders = false;
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
