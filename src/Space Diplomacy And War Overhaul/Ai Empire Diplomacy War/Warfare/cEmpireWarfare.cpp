#include "stdafx.h"
#include "cEmpireWarfare.h"
#include <Spore-Mod-Utils/Include/SporeModUtils.h>
#include <algorithm>
#include <EASTL/sort.h>
#include <cmath>

using namespace SporeModUtils;
using namespace Simulator;

cEmpireWarfare::cEmpireWarfare(Simulator::cEmpire* empire,
	cSimulationValidator* simulationValidator,
	cWarfareConfig* warfareConfig, 
	cWarfareStrengthAnalyzer* warfareStrengthAnalyzer, 
	cWarfareEventDispatcher* warfareEventDispatcher)
{
	this->empire = empire;
	this->range = 0;
	this->attackedPlanets = 0;
	this->simulationValidator = simulationValidator;
	this->warfareConfig = warfareConfig;
	this->warfareStrengthAnalyzer = warfareStrengthAnalyzer;
	this->warfareEventDispatcher = warfareEventDispatcher;
}

cEmpireWarfare::~cEmpireWarfare()
{
}

int cEmpireWarfare::AddRef()
{
	return DefaultRefCounted::AddRef();
}

int cEmpireWarfare::Release()
{
	return DefaultRefCounted::Release();
}

void* cEmpireWarfare::Cast(uint32_t type) const
{
	CLASS_CAST(Object);
	CLASS_CAST(cEmpireWarfare);
	return nullptr;
}

bool cEmpireWarfare::AtackableStar(Simulator::cStarRecord* star) {
	for (cPlanetRecordPtr planet : star->GetPlanetRecords()) {
		if (MissionManager.ThereIsEventInPlanet(planet.get())) {
			return false;
		}
	}
	return true;
}

eastl::vector<cStarRecord*> cEmpireWarfare::GetEnemyStarsInRangeOfStar(Simulator::cStarRecord* star) {
	eastl::vector<cStarRecordPtr> closeStars;

	StarRequestFilter filter;
	filter.RemoveStarType(Simulator::StarType::None);
	filter.RemoveStarType(Simulator::StarType::GalacticCore);
	filter.RemoveStarType(Simulator::StarType::ProtoPlanetary);
	filter.RemoveStarType(Simulator::StarType::BlackHole);
	filter.techLevels = 0;
	filter.AddTechLevel(Simulator::TechLevel::Empire);
	filter.minDistance = 0;
	filter.maxDistance = range;
	StarManager.FindStars(star->mPosition, filter, closeStars);

	eastl::vector<eastl::pair<float, Simulator::cStarRecord*>> enemyStars;

	for (cStarRecordPtr closeStar : closeStars) {
		cEmpire* starEmpire = StarManager.GetEmpire(closeStar->mEmpireID);
		if (simulationValidator->ValidEmpire(starEmpire, true)
			&& DiplomacyUtils::War(empire.get(), starEmpire)
			&& AtackableStar(closeStar.get()))
		{
			float distance = StarUtils::GetDistanceBetweenStars(star, closeStar.get());
			enemyStars.push_back({ distance, closeStar.get() });
		}
	}

	eastl::sort(enemyStars.begin(), enemyStars.end(),
		[](const auto& a, const auto& b) {
			return a.first < b.first;
		});

	eastl::vector<Simulator::cStarRecord*> orderedStars;
	orderedStars.reserve(enemyStars.size());
	for (auto& pair : enemyStars) {
		orderedStars.push_back(pair.second);
	}

	return orderedStars;
}

void cEmpireWarfare::CalculateAttackPriorities() {
	cStarRecord* homeworld = EmpireUtils::GetHomeStar(empire.get());

	for (cStarRecordPtr star : empire->mStars) {
		if (star != nullptr) {
			eastl::vector<cStarRecord*> enemyStarsInRangeOfStar = GetEnemyStarsInRangeOfStar(star.get());
			if (!enemyStarsInRangeOfStar.empty()) {
				cStarRecord* closestStar = enemyStarsInRangeOfStar.front();
				float distToHome = (homeworld != nullptr) ? max(StarUtils::GetDistanceBetweenStars(homeworld, closestStar), 0.1f) : 1.0f;
				attackPriorityMap[closestStar] += 100.0f / distToHome;

				for (auto it = enemyStarsInRangeOfStar.begin() + 1; it != enemyStarsInRangeOfStar.end(); ++it) {
					cStarRecord* closeStar = *it;
					float subDist = (homeworld != nullptr) ? max(StarUtils::GetDistanceBetweenStars(homeworld, closeStar), 0.1f) : 1.0f;
					attackPriorityMap[closeStar] += 1.0f / subDist;
				}
			}
		}
	}
}

int cEmpireWarfare::GetEmpireBomberForceForPlanet(Simulator::cPlanetRecord* planet) {
	int requiredBombers = warfareStrengthAnalyzer->GetBomberForceForPlanet(this->empire.get(), planet);

	// Штраф за одновременную атаку нескольких планет
	float multiAttackPenalty = warfareConfig->GetBombersMultiPlanetPenaltyFactor() * log(static_cast<float>(attackedPlanets) + 1.0f);
	requiredBombers += static_cast<int>(round(static_cast<float>(requiredBombers) * multiAttackPenalty));

	// Логистический штраф: радиус рассчитывается динамически от дальности вооружения империи
	cStarRecord* homeworld = EmpireUtils::GetHomeStar(empire.get());
	if (homeworld != nullptr && planet->GetStarRecord() != nullptr) {
		float distanceToTarget = StarUtils::GetDistanceBetweenStars(homeworld, planet->GetStarRecord());
		float baseLogisticsRange = warfareConfig->GetWarfareRange(empire->GetWeaponryLevel());
		if (baseLogisticsRange <= 0.0f) {
			baseLogisticsRange = 10.0f;
		}

		if (distanceToTarget > baseLogisticsRange) {
			float logisticsStep = warfareConfig->GetBombersMultiPlanetPenaltyFactor() * 0.1f;
			float logisticsPenaltyRatio = (distanceToTarget - baseLogisticsRange) * logisticsStep;
			requiredBombers += static_cast<int>(round(static_cast<float>(requiredBombers) * logisticsPenaltyRatio));
		}
	}

	return max(requiredBombers, 1);
}

int cEmpireWarfare::GetEmpireBomberForceForSystem(Simulator::cStarRecord* star) {
	int requiredBombers = 0;
	for (cPlanetRecordPtr planet : star->GetPlanetRecords()) {
		if (planet->GetTechLevel() == TechLevel::Empire) {
			requiredBombers += GetEmpireBomberForceForPlanet(planet.get());
			attackedPlanets++;
		}
	}
	return requiredBombers;
}

void cEmpireWarfare::AttackStar(Simulator::cStarRecord* star, int bombers) {
	eastl::vector<pair<cPlanetRecordPtr, int>> bombersPerPlanet;
	int totalRequiredBombers = 0;
	for (cPlanetRecordPtr planet : star->GetPlanetRecords()) {
		if (simulationValidator->ValidPlanet(planet.get()) && planet->GetTechLevel() == TechLevel::Empire && !MissionManager.ThereIsEventInPlanet(planet.get())) {
			int planetRequiredBombers = warfareStrengthAnalyzer->GetBomberForceForPlanet(empire.get(), planet.get());
			totalRequiredBombers += planetRequiredBombers;
			bombersPerPlanet.push_back(make_pair(planet, planetRequiredBombers));
		}
	}
	if (bombersPerPlanet.size() > 0) {
		int extraBombersPerPlanet = max(0, bombers - totalRequiredBombers) / static_cast<int>(bombersPerPlanet.size());
		int surplusBombers = max(0, bombers - totalRequiredBombers) % static_cast<int>(bombersPerPlanet.size());
		for (auto& bomberForPlanet : bombersPerPlanet) {
			bomberForPlanet.second += extraBombersPerPlanet;
			if (surplusBombers > 0) {
				bomberForPlanet.second++;
				surplusBombers--;
			}
			warfareEventDispatcher->DispatchPlanetAttackedEvent(empire.get(), bomberForPlanet.first.get(), max(bomberForPlanet.second, 1));
		}
	}
}

void cEmpireWarfare::SelectAndAttackTargets() {
	if (!empire) return;

	float bombersPerCycle = warfareStrengthAnalyzer->GetBombersProducedByEmpire(empire.get());
	Simulator::Archetypes baseArch = ArchetypeUtils::GetBaseArchetype(empire->mArchetype);

	// 1. Динамическая экономика НПС от реального числа звезд, заводов и рыночной цены пряности
	int baseUnitCost = max(static_cast<int>(round(1000.0f / max(warfareConfig->GetBombersPerRedSpiceFactory(), 0.1f))), 250);
	int bomberUnitCost = baseUnitCost;
	float taxMultiplier = 1.0f;

	if (baseArch == Archetypes::kArchetypeTrader) {
		taxMultiplier = 1.75f;
		bomberUnitCost = max(static_cast<int>(round(static_cast<float>(baseUnitCost) * 0.80f)), 200);
	}

	// Выработка империи (bombersPerCycle) уже учитывает количество планет, Т-индекс и цену пряности каждой планеты
	int numSystems = max(static_cast<int>(empire->mStars.size()), 1);
	int cycleTaxIncome = static_cast<int>(round(max(bombersPerCycle, 1.0f) * static_cast<float>(baseUnitCost) * 0.65f * taxMultiplier));

	// Динамический потолок казны: запас примерно на 10 циклов + подушка от количества звездных систем
	int dynamicMaxTreasury = static_cast<int>(round(
		(50000.0f + static_cast<float>(numSystems) * 25000.0f + static_cast<float>(cycleTaxIncome) * 10.0f) * taxMultiplier
	));
	dynamicMaxTreasury = min(max(dynamicMaxTreasury, 80000), 15000000);

	if (empire->mEmpireMoney < dynamicMaxTreasury) {
		empire->mEmpireMoney = min(empire->mEmpireMoney + cycleTaxIncome, dynamicMaxTreasury);
	}

	if (empire->mEnemies.empty()) return;

	// Порог финансового кризиса (15% от динамического потолка казны, синхронизирован с dllmain.cpp)
	int bankruptcyThreshold = max(static_cast<int>(round(static_cast<float>(dynamicMaxTreasury) * 0.15f)), baseUnitCost * 3);

	// Неприкосновенный резерв по доктрине философии:
	// - Воины и Фанатики сливают бюджет почти в ноль ради победы (опускаясь ниже черты кризиса и рискуя бунтом!)
	// - Торговцы и Ученые держат строгую подушку выше черты банкротства, чтобы не ловить дефолт и забастовки
	int minReserveMoney = bankruptcyThreshold;
	if (baseArch == Archetypes::kArchetypeWarrior || baseArch == Archetypes::kArchetypeZealot || baseArch == Archetypes::kArchetypeGrob) {
		minReserveMoney = static_cast<int>(round(static_cast<float>(bankruptcyThreshold) * 0.35f));
	} else if (baseArch == Archetypes::kArchetypeTrader || baseArch == Archetypes::kArchetypeScientist) {
		minReserveMoney = static_cast<int>(round(static_cast<float>(bankruptcyThreshold) * 1.25f));
	} else {
		minReserveMoney = static_cast<int>(round(static_cast<float>(bankruptcyThreshold) * 0.85f));
	}

	attackedPlanets = 0;
	attackPriorityMap.clear();
	range = warfareConfig->GetWarfareRange(empire->GetWeaponryLevel());
	CalculateAttackPriorities();

	float currentBombers = bombersPerCycle;

	while (currentBombers > 0.0f && !attackPriorityMap.empty()) {
		auto bestIt = attackPriorityMap.begin();
		float maxPriority = -1.0f;
		for (auto it = attackPriorityMap.begin(); it != attackPriorityMap.end(); ++it) {
			if (it->second > maxPriority) {
				maxPriority = it->second;
				bestIt = it;
			}
		}

		if (!simulationValidator->ValidStar(bestIt->first.get())) {
			attackPriorityMap.erase(bestIt);
			continue;
		}

		cStarRecordPtr bestTarget = bestIt->first;
		attackPriorityMap.erase(bestIt);

		// Разделение ударной волны (combatBombersForSystem) и полной логистической стоимости операции (logisticsCostForSystem)
		int combatBombersInt = warfareStrengthAnalyzer->GetBomberForceForSystem(empire.get(), bestTarget.get());
		if (combatBombersInt <= 0) {
			continue;
		}

		float combatBombersForSystem = static_cast<float>(combatBombersInt);
		float logisticsCostForSystem = static_cast<float>(GetEmpireBomberForceForSystem(bestTarget.get()));

		// Экономическая мобилизация из казны с учетом философии резерва
		if (logisticsCostForSystem > currentBombers && empire->mEmpireMoney > (minReserveMoney + bomberUnitCost)) {
			int missingBombers = static_cast<int>(ceil(logisticsCostForSystem - currentBombers));
			int spendableMoney = empire->mEmpireMoney - minReserveMoney;
			int affordableBombers = spendableMoney / bomberUnitCost;
			int purchasedBombers = min(missingBombers, affordableBombers);

			if (purchasedBombers > 0) {
				currentBombers += static_cast<float>(purchasedBombers);
				empire->mEmpireMoney -= purchasedBombers * bomberUnitCost;
			}
		}

		if (logisticsCostForSystem > currentBombers) {
			float pOfAttack = currentBombers / max(logisticsCostForSystem, 1.0f);
			if (pOfAttack > Math::randf()) {
				int scaledWave = max(static_cast<int>(round(combatBombersForSystem * pOfAttack)), 1);
				AttackStar(bestTarget.get(), scaledWave);
			}
			currentBombers = 0.0f;
		}
		else {
			currentBombers -= logisticsCostForSystem;

			if (attackPriorityMap.empty() && currentBombers > 0.0f) {
				// Мягкое усиление последней цели цикла без сброса всего пула на одну колонию
				int maxPlanetCap = warfareConfig->GetMaxBombersInRaid();
				int planetCount = 0;
				for (cPlanetRecordPtr p : bestTarget->GetPlanetRecords()) {
					if (p && p->GetTechLevel() == TechLevel::Empire) {
						planetCount++;
					}
				}
				int systemSoftCap = max(combatBombersInt, maxPlanetCap * max(planetCount, 1));
				int bonusWave = min(static_cast<int>(round(sqrt(currentBombers) * 1.5f)), max(0, systemSoftCap - combatBombersInt));
				AttackStar(bestTarget.get(), combatBombersInt + bonusWave);
				currentBombers = 0.0f;
			}
			else {
				AttackStar(bestTarget.get(), combatBombersInt);
			}
		}
	}
}
