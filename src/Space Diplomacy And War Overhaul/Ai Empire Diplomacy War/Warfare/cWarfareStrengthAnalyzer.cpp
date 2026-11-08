#include "stdafx.h"
#include "cWarfareStrengthAnalyzer.h"

#include <Spore-Mod-Utils/Include/SporeModUtils.h>

using namespace SporeModUtils;
using namespace Simulator;

cWarfareStrengthAnalyzer::cWarfareStrengthAnalyzer(cSimulationValidator* simulationValidator,
    cWarfareConfig* warfareConfig, 
    cSpaceCombatMetrics* 
    spaceCombatMetrics, 
    cArchetypeStrengthConfig* archetypeStrengthConfig)
{
    this->simulationValidator = simulationValidator;
    this->warfareConfig = warfareConfig;
	this->spaceCombatMetrics = spaceCombatMetrics;
    this->archetypeStrengthConfig = archetypeStrengthConfig;
    SpiceUtils::GetSpawnableSpiceBaseCosts(spiceCosts);
}


cWarfareStrengthAnalyzer::~cWarfareStrengthAnalyzer()
{
}

// For internal use, do not modify.
int cWarfareStrengthAnalyzer::AddRef()
{
	return DefaultRefCounted::AddRef();
}

// For internal use, do not modify.
int cWarfareStrengthAnalyzer::Release()
{
	return DefaultRefCounted::Release();
}

// You can extend this function to return any other types your class implements.
void* cWarfareStrengthAnalyzer::Cast(uint32_t type) const
{
	CLASS_CAST(Object);
	CLASS_CAST(cWarfareStrengthAnalyzer);
	return nullptr;
}

int cWarfareStrengthAnalyzer::GetBomberForceForPlanet(Simulator::cEmpire* empire, Simulator::cPlanetRecord* planet){
    if (planet->GetTechLevel() != TechLevel::Empire) {
        return 0;
    }
    cEmpire* targetEmpire = StarManager.GetEmpire(planet->GetStarRecord()->mEmpireID);
    float empireBomberHealth = spaceCombatMetrics->GetBomberHealthByEmpire(empire);
    float empireFighterHealth = spaceCombatMetrics->GetFighterHealthByEmpire(empire);
    int empireFightersPerBomber = spaceCombatMetrics->GetFightersPerBomber();
    float targetTurretHealth = spaceCombatMetrics->GetTurretHealthByEmpire(targetEmpire);
    float targetDefenderHealth = spaceCombatMetrics->GetDefenderHealthByEmpire(targetEmpire);
    int planetDefenders = spaceCombatMetrics->GetDefendersInPlanet(planet);
    int planetTurrets = spaceCombatMetrics->GetTurretsInPlanet(planet);

    float fightersToDefeatDefenders = max((planetDefenders * targetDefenderHealth) / empireFighterHealth, static_cast<float>(empireFightersPerBomber));
    float bombersToDefeatTurrets = max((planetTurrets * targetTurretHealth) / (empireBomberHealth + empireFightersPerBomber * empireFighterHealth), 1.0f);

    float rawRequired = bombersToDefeatTurrets + fightersToDefeatDefenders / empireFightersPerBomber;

    // Модификатор штурмовой доктрины по философии атакующей империи
    float archetypeAssaultMultiplier = 1.0f;
    if (empire != nullptr) {
        Simulator::Archetypes baseArchetype = ArchetypeUtils::GetBaseArchetype(empire->mArchetype);
        switch (baseArchetype) {
            case Archetypes::kArchetypeWarrior:
            case Archetypes::kArchetypeZealot:
                archetypeAssaultMultiplier = 1.35f; // Военный кулак: подавление числом
                break;
            case Archetypes::kArchetypeScientist:
                archetypeAssaultMultiplier = 0.85f; // Высокоточные хирургические удары
                break;
            case Archetypes::kArchetypeBard:
            case Archetypes::kArchetypeShaman:
            case Archetypes::kArchetypeDiplomat:
                archetypeAssaultMultiplier = 0.75f; // Мирные архетипы отправляют минимальный эскорт
                break;
            default:
                archetypeAssaultMultiplier = 1.0f;
                break;
        }
    }

    int requiredBombers = static_cast<int>(round(rawRequired * archetypeAssaultMultiplier));
    int maxBombers = warfareConfig->GetMaxBombersInRaid();
    return min(requiredBombers, maxBombers);
}

int cWarfareStrengthAnalyzer::GetBomberForceForSystem(Simulator::cEmpire* empire, Simulator::cStarRecord* star){
    if (!simulationValidator->ValidStar(star)) {
        return 0;
    }
    int requiredBombers = 0;
    for (cPlanetRecordPtr planet : star->GetPlanetRecords()) {
        if (planet->GetTechLevel() == TechLevel::Empire) {
            requiredBombers += GetBomberForceForPlanet(empire, planet.get());
        }
    }
    return requiredBombers;
}

float cWarfareStrengthAnalyzer::GetBombersProducedByFactory(ResourceKey spiceKey){
    float cheapestSpiceCost = SpiceUtils::GetSpiceBaseCost(SpiceUtils::GetCheapestSpice(spiceCosts), spiceCosts);
    float spiceCost = SpiceUtils::GetSpiceBaseCost(spiceKey, spiceCosts);
    // Defaults to the cheapest spice if the given spice is not valid.
    if (spiceCost == 0) {
        spiceCost = cheapestSpiceCost;
    }
    float spiceCostRatio = spiceCost / cheapestSpiceCost;
    return warfareConfig->GetBombersPerRedSpiceFactory() * spiceCostRatio;
}

int cWarfareStrengthAnalyzer::GetFactoryCountInPlanet(Simulator::cPlanetRecord* planet){
    int baseColonies = 1;
    switch (planet->mType) {
        case(PlanetType::T1): baseColonies = 1; break;
        case(PlanetType::T2): baseColonies = 2; break;
        case(PlanetType::T3): 
            baseColonies = PlanetUtils::Homeworld(planet) ? warfareConfig->GetCitiesInHomeworld() : 3;
            break;
        default: return 0;
    }

    // Поддержка Unlimited-Colony: если колоний больше базового лимита, масштабируем заводы
    int actualColonies = baseColonies;
    if (planet->GetPlanetView() && !planet->GetPlanetView()->mColonies.empty()) {
        int viewCount = static_cast<int>(planet->GetPlanetView()->mColonies.size());
        if (viewCount > actualColonies) {
            actualColonies = viewCount;
        }
    }

    int buildingsPerColony = warfareConfig->GetBuildingsInT1Colony();
    if (planet->mType == PlanetType::T2) buildingsPerColony = warfareConfig->GetBuildingsInT2Colony();
    else if (planet->mType == PlanetType::T3) buildingsPerColony = warfareConfig->GetBuildingsInT3Colony();

    int totalBuildings = actualColonies * buildingsPerColony;
    return max(totalBuildings / warfareConfig->GetBuildingsPerFactoryRatio(), 1);
}

float cWarfareStrengthAnalyzer::GetBombersProducedByPlanet(Simulator::cPlanetRecord* planet){
    if (planet->GetTechLevel() == TechLevel::Empire) {
        return GetFactoryCountInPlanet(planet) * GetBombersProducedByFactory(planet->mSpiceGen);
    }
    else {
        return 0.0f;
    }
}

float cWarfareStrengthAnalyzer::GetBombersProducedBySystem(Simulator::cStarRecord* star){
    float bombersProduced = 0;
    for (cPlanetRecordPtr planet : star->GetPlanetRecords()) {
        if (simulationValidator->ValidPlanet(planet.get()) && planet->GetTechLevel() == TechLevel::Empire) {
            bombersProduced += GetBombersProducedByPlanet(planet.get());
        }
    }
    return bombersProduced;
}

float cWarfareStrengthAnalyzer::AdjustBombersForCaps(float bomberCount) {
    float softCap = warfareConfig->GetBombersSoftCap() * archetypeStrengthConfig->GetGlobalMultiplier();
    float excessBomberSoftCap = max(bomberCount - softCap, 0.0f);
    bomberCount = (bomberCount - excessBomberSoftCap) + (excessBomberSoftCap * warfareConfig->GetBombersSoftCapFactor());

    float hardCap = warfareConfig->GetBombersHardCap() * archetypeStrengthConfig->GetGlobalMultiplier();
    float excessBomberHardCap = max(bomberCount - hardCap, 0.0f);
    bomberCount = (bomberCount - excessBomberHardCap) + (excessBomberHardCap * warfareConfig->GetBombersHardCapFactor());

    return bomberCount;
}


float cWarfareStrengthAnalyzer::GetBombersProducedByEmpire(Simulator::cEmpire* empire){
    float bombersProduced = 0;
    for (cStarRecordPtr star : empire->mStars) {
        if (simulationValidator->ValidStar(star.get())) {
            bombersProduced += GetBombersProducedBySystem(star.get());
        }
    }
    float baseStrength = archetypeStrengthConfig->GetArchetypeBaseStrength(empire->mArchetype);
    float bonusStrength = archetypeStrengthConfig->GetArchetypeBonusStrength(empire->mArchetype);
    bombersProduced = (bombersProduced + bonusStrength) * baseStrength;
    bombersProduced = AdjustBombersForCaps(bombersProduced);
    return bombersProduced;
}

float cWarfareStrengthAnalyzer::GetEmpireStrenghtFactor(Simulator::cEmpire* empire){
    float bombersProducedByEmpire = GetBombersProducedByEmpire(empire);
    float empireBomberHealth = spaceCombatMetrics->GetBomberHealthByEmpire(empire);
    float MaxLevelBomberHealth = spaceCombatMetrics->GetBomberHealthByLevel(4);
    float bomberHealthRatio = MaxLevelBomberHealth / empireBomberHealth;
    return bombersProducedByEmpire * bomberHealthRatio;
}
