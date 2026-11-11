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
	if (star == nullptr) return false;
	for (cPlanetRecordPtr planet : star->GetPlanetRecords()) {
		if (planet != nullptr && MissionManager.ThereIsEventInPlanet(planet.get())) {
			return false;
		}
	}
	return true;
}

eastl::vector<cStarRecord*> cEmpireWarfare::GetEnemyStarsInRangeOfStar(Simulator::cStarRecord* star) {
	eastl::vector<cStarRecordPtr> closeStars;
	if (star == nullptr || empire == nullptr) return {};

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
		if (closeStar == nullptr) continue;
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
	attackPriorityMap.clear();
	if (empire == nullptr) return;

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
	if (planet == nullptr || empire == nullptr) return 0;

	int requiredBombers = warfareStrengthAnalyzer->GetBomberForceForPlanet(this->empire.get(), planet);

	// Штраф за одновременную атаку нескольких планет за один цикл
	float multiAttackPenalty = warfareConfig->GetBombersMultiPlanetPenaltyFactor() * std::log(static_cast<float>(attackedPlanets) + 1.0f);
	requiredBombers += static_cast<int>(std::round(static_cast<float>(requiredBombers) * multiAttackPenalty));

	// Логистический штраф: радиус рассчитывается динамически от дальности вооружения империи
	cStarRecord* homeworld = EmpireUtils::GetHomeStar(empire.get());
	if (homeworld != nullptr && planet->GetStarRecord() != nullptr) {
		float distanceToTarget = StarUtils::GetDistanceBetweenStars(homeworld, planet->GetStarRecord());
		float baseLogisticsRange = warfareConfig->GetWarfareRange(empire->GetWeaponryLevel());
		if (baseLogisticsRange <= 0.0f) {
			baseLogisticsRange = 15.0f;
		}

		if (distanceToTarget > baseLogisticsRange) {
			float logisticsStep = max(warfareConfig->GetBombersMultiPlanetPenaltyFactor() * 0.1f, 0.02f);
			float logisticsPenaltyRatio = (distanceToTarget - baseLogisticsRange) * logisticsStep;
			requiredBombers += static_cast<int>(std::round(static_cast<float>(requiredBombers) * logisticsPenaltyRatio));
		}
	}

	return max(requiredBombers, 1);
}

int cEmpireWarfare::GetEmpireBomberForceForSystem(Simulator::cStarRecord* star) {
	if (star == nullptr) return 0;

	int requiredBombers = 0;
	int simulatedAttacked = attackedPlanets;

	for (cPlanetRecordPtr planet : star->GetPlanetRecords()) {
		if (planet != nullptr && simulationValidator->ValidPlanet(planet.get()) &&
		    planet->GetTechLevel() == TechLevel::Empire && !MissionManager.ThereIsEventInPlanet(planet.get())) {

			int baseForce = warfareStrengthAnalyzer->GetBomberForceForPlanet(this->empire.get(), planet.get());
			float multiAttackPenalty = warfareConfig->GetBombersMultiPlanetPenaltyFactor() * std::log(static_cast<float>(simulatedAttacked) + 1.0f);
			int planetForce = baseForce + static_cast<int>(std::round(static_cast<float>(baseForce) * multiAttackPenalty));

			cStarRecord* homeworld = EmpireUtils::GetHomeStar(empire.get());
			if (homeworld != nullptr && planet->GetStarRecord() != nullptr) {
				float distanceToTarget = StarUtils::GetDistanceBetweenStars(homeworld, planet->GetStarRecord());
				float baseLogisticsRange = warfareConfig->GetWarfareRange(empire->GetWeaponryLevel());
				if (baseLogisticsRange <= 0.0f) baseLogisticsRange = 15.0f;

				if (distanceToTarget > baseLogisticsRange) {
					float logisticsStep = max(warfareConfig->GetBombersMultiPlanetPenaltyFactor() * 0.1f, 0.02f);
					float logisticsPenaltyRatio = (distanceToTarget - baseLogisticsRange) * logisticsStep;
					planetForce += static_cast<int>(std::round(static_cast<float>(planetForce) * logisticsPenaltyRatio));
				}
			}

			requiredBombers += max(planetForce, 1);
			simulatedAttacked++;
		}
	}
	return requiredBombers;
}

void cEmpireWarfare::AttackStar(Simulator::cStarRecord* star, int bombers) {
	if (star == nullptr || empire == nullptr || bombers <= 0) return;

	eastl::vector<eastl::pair<cPlanetRecordPtr, int>> bombersPerPlanet;
	int totalRequiredBombers = 0;
	for (cPlanetRecordPtr planet : star->GetPlanetRecords()) {
		if (planet != nullptr && simulationValidator->ValidPlanet(planet.get()) &&
		    planet->GetTechLevel() == TechLevel::Empire && !MissionManager.ThereIsEventInPlanet(planet.get())) {

			int planetRequiredBombers = warfareStrengthAnalyzer->GetBomberForceForPlanet(empire.get(), planet.get());
			totalRequiredBombers += planetRequiredBombers;
			bombersPerPlanet.push_back(eastl::make_pair(planet, planetRequiredBombers));
			attackedPlanets++;
		}
	}

	if (!bombersPerPlanet.empty()) {
		int diff = max(bombers - totalRequiredBombers, 0);
		int extraBombersPerPlanet = diff / static_cast<int>(bombersPerPlanet.size());
		int surplusBombers = diff % static_cast<int>(bombersPerPlanet.size());

		int maxBaseRaid = warfareConfig->GetMaxBombersInRaid();

		for (auto& bomberForPlanet : bombersPerPlanet) {
			bomberForPlanet.second += extraBombersPerPlanet;
			if (surplusBombers > 0) {
				bomberForPlanet.second++;
				surplusBombers--;
			}

			// Защита от сброса 80+ бомбардировщиков на одну слабую колонию
			int planetCap = PlanetUtils::Homeworld(bomberForPlanet.first.get())
				? static_cast<int>(std::round(static_cast<float>(maxBaseRaid) * 1.6f))
				: maxBaseRaid;
			if (bomberForPlanet.second > planetCap) {
				float excess = static_cast<float>(bomberForPlanet.second - planetCap);
				bomberForPlanet.second = planetCap + static_cast<int>(std::round(std::sqrt(excess) * 2.0f));
			}

			warfareEventDispatcher->DispatchPlanetAttackedEvent(empire.get(), bomberForPlanet.first.get(), max(bomberForPlanet.second, 1));
		}
	}
}

void cEmpireWarfare::SelectAndAttackTargets() {
	if (empire == nullptr) return;

	// 1. Налоговый приток в казну ИИ от промышленного потенциала планет (чтобы экономика ИИ не высыхала в 0 навсегда)
	float bombersPerCycle = warfareStrengthAnalyzer->GetBombersProducedByEmpire(empire.get());
	Simulator::Archetypes baseArch = ArchetypeUtils::GetBaseArchetype(empire->mArchetype);
	float tradeMultiplier = 1.0f;
	if (baseArch == Archetypes::kArchetypeTrader) {
		tradeMultiplier = 1.75f; // Торговцы генерируют сверхприбыль
	} else if (baseArch == Archetypes::kArchetypeScientist || baseArch == Archetypes::kArchetypeDiplomat) {
		tradeMultiplier = 1.20f;
	}

	int maxTreasuryCap = (baseArch == Archetypes::kArchetypeTrader) ? 3500000 : 2000000;
	if (empire->mEmpireMoney < maxTreasuryCap) {
		int cycleIncome = static_cast<int>(std::round((2500.0f + bombersPerCycle * 450.0f) * tradeMultiplier));
		empire->mEmpireMoney = min(empire->mEmpireMoney + cycleIncome, maxTreasuryCap);
	}

	if (empire->mEnemies.empty()) return;

	attackedPlanets = 0;
	range = warfareConfig->GetWarfareRange(empire->GetWeaponryLevel());
	CalculateAttackPriorities();

	float currentBombers = bombersPerCycle;
	int bomberUnitCost = max(static_cast<int>(std::round(1000.0f / max(warfareConfig->GetBombersPerRedSpiceFactory(), 0.1f))), 250);
	if (baseArch == Archetypes::kArchetypeTrader) {
		bomberUnitCost = static_cast<int>(std::round(static_cast<float>(bomberUnitCost) * 0.80f)); // Скидка наемников для Торговцев
	}

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

		// Чистая боевая потребность (для спавна бомберов) vs Полная логистическая цена операции (с дальностью и мульти-атаками)
		int combatBombersForSystem = warfareStrengthAnalyzer->GetBomberForceForSystem(empire.get(), bestTarget.get());
		int logisticsCostForSystem = GetEmpireBomberForceForSystem(bestTarget.get());
		if (combatBombersForSystem <= 0 || logisticsCostForSystem <= 0) {
			continue;
		}

		float totalOperationCost = static_cast<float>(logisticsCostForSystem);

		// 2. Экономическая мобилизация резервов казны с учетом логистической стоимости
		const int minReserveMoney = 15000; // Не тратим последние гроши для торговли с игроком
		if (totalOperationCost > currentBombers && empire->mEmpireMoney > (minReserveMoney + bomberUnitCost)) {
			int missingBombers = static_cast<int>(std::ceil(totalOperationCost - currentBombers));
			int spendableMoney = empire->mEmpireMoney - minReserveMoney;
			int affordableBombers = spendableMoney / bomberUnitCost;
			int purchasedBombers = min(missingBombers, affordableBombers);

			if (purchasedBombers > 0) {
				currentBombers += static_cast<float>(purchasedBombers);
				empire->mEmpireMoney -= purchasedBombers * bomberUnitCost;
			}
		}

		// 3. Отправка ударного флота со списанием полной логистики
		if (totalOperationCost > currentBombers) {
			float pOfAttack = currentBombers / totalOperationCost;
			if (pOfAttack > Math::randf()) {
				AttackStar(bestTarget.get(), combatBombersForSystem);
			}
			currentBombers = 0.0f;
		} else {
			if (attackPriorityMap.empty()) {
				// Если это последняя цель, даем умеренное усиление из остатка (до +35%), а не сваливаем весь пул империи
				int surplus = static_cast<int>(std::floor(currentBombers - totalOperationCost));
				int bonusWave = min(surplus, max(combatBombersForSystem / 3, 2));
				AttackStar(bestTarget.get(), combatBombersForSystem + bonusWave);
				currentBombers = 0.0f;
			} else {
				AttackStar(bestTarget.get(), combatBombersForSystem);
				currentBombers -= totalOperationCost;
			}
		}
	}
}
