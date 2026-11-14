#include "stdafx.h"
#include "cWarfareStrengthAnalyzer.h"

#include <Spore-Mod-Utils/Include/SporeModUtils.h>
#include <cmath>

using namespace SporeModUtils;
using namespace Simulator;

cWarfareStrengthAnalyzer::cWarfareStrengthAnalyzer(cSimulationValidator* simulationValidator,
    cWarfareConfig* warfareConfig, 
    cSpaceCombatMetrics* spaceCombatMetrics, 
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

int cWarfareStrengthAnalyzer::AddRef()
{
	return DefaultRefCounted::AddRef();
}

int cWarfareStrengthAnalyzer::Release()
{
	return DefaultRefCounted::Release();
}

void* cWarfareStrengthAnalyzer::Cast(uint32_t type) const
{
	CLASS_CAST(Object);
	CLASS_CAST(cWarfareStrengthAnalyzer);
	return nullptr;
}

int cWarfareStrengthAnalyzer::GetBomberForceForPlanet(Simulator::cEmpire* empire, Simulator::cPlanetRecord* planet){
    if (planet == nullptr || planet->GetTechLevel() != TechLevel::Empire) {
        return 0;
    }
    cEmpire* targetEmpire = (planet->GetStarRecord() != nullptr) ? StarManager.GetEmpire(planet->GetStarRecord()->mEmpireID) : nullptr;
    float empireBomberHealth = max(spaceCombatMetrics->GetBomberHealthByEmpire(empire), 1.0f);
    float empireFighterHealth = max(spaceCombatMetrics->GetFighterHealthByEmpire(empire), 1.0f);
    int empireFightersPerBomber = max(spaceCombatMetrics->GetFightersPerBomber(), 1);
    float targetTurretHealth = spaceCombatMetrics->GetTurretHealthByEmpire(targetEmpire);
    float targetDefenderHealth = spaceCombatMetrics->GetDefenderHealthByEmpire(targetEmpire);
    int planetDefenders = spaceCombatMetrics->GetDefendersInPlanet(planet);
    int planetTurrets = spaceCombatMetrics->GetTurretsInPlanet(planet);

    float fightersToDefeatDefenders = max((planetDefenders * targetDefenderHealth) / empireFighterHealth, static_cast<float>(empireFightersPerBomber));
    float bombersToDefeatTurrets = max((planetTurrets * targetTurretHealth) / (empireBomberHealth + empireFightersPerBomber * empireFighterHealth), 1.0f);

    float rawRequired = bombersToDefeatTurrets + fightersToDefeatDefenders / static_cast<float>(empireFightersPerBomber);

    // Тактическая штурмовая доктрина по философии (независимо от ползунка глобального производства)
    float archetypeAssaultMultiplier = 1.0f;
    if (empire != nullptr) {
        Simulator::Archetypes baseArch = ArchetypeUtils::GetBaseArchetype(empire->mArchetype);
        switch (baseArch) {
            case Archetypes::kArchetypeWarrior:
            case Archetypes::kArchetypeZealot:
            case Archetypes::kArchetypeGrob:
                archetypeAssaultMultiplier = 1.35f; // Подавление численностью (+35%)
                break;
            case Archetypes::kArchetypeScientist:
                archetypeAssaultMultiplier = 0.85f; // Высокоточный удар (-15%)
                break;
            case Archetypes::kArchetypeBard:
            case Archetypes::kArchetypeShaman:
            case Archetypes::kArchetypeDiplomat:
                archetypeAssaultMultiplier = 0.90f; // Ограниченный контингент, но без суицида об турели
                break;
            default:
                archetypeAssaultMultiplier = 1.0f;
                break;
        }
    }

    int minBreakthroughFloor = max(static_cast<int>(std::ceil(bombersToDefeatTurrets)), 1);
    int requiredBombers = max(static_cast<int>(std::round(rawRequired * archetypeAssaultMultiplier)), minBreakthroughFloor);
    int maxBombers = warfareConfig->GetMaxBombersInRaid();

    // 1. Относительное масштабирование для столиц от пользовательского конфига
    if (PlanetUtils::Homeworld(planet)) {
        maxBombers = static_cast<int>(std::round(static_cast<float>(maxBombers) * 1.6f));
    }

    // 2. Эластичный мягкий потолок рейда (Soft-Cap) вместо жесткого обрезания
    if (requiredBombers > maxBombers) {
        float excess = static_cast<float>(requiredBombers - maxBombers);
        requiredBombers = maxBombers + static_cast<int>(std::round(std::sqrt(excess) * 2.0f));
    }

    return max(requiredBombers, 1);
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
    float cheapestSpiceCost = max(SpiceUtils::GetSpiceBaseCost(SpiceUtils::GetCheapestSpice(spiceCosts), spiceCosts), 1.0f);
    float spiceCost = SpiceUtils::GetSpiceBaseCost(spiceKey, spiceCosts);
    if (spiceCost <= 0.0f) {
        spiceCost = cheapestSpiceCost;
    }
    float spiceCostRatio = spiceCost / cheapestSpiceCost;
    return warfareConfig->GetBombersPerRedSpiceFactory() * spiceCostRatio;
}

int cWarfareStrengthAnalyzer::GetFactoryCountInPlanet(Simulator::cPlanetRecord* planet){
    if (planet == nullptr) return 0;

    spaceCombatMetrics->UpdatePlanetDefenseCache(planet);

    int baseColonies = 1;
    switch (planet->mType) {
    case(PlanetType::T1):
        baseColonies = 1;
        break;
    case(PlanetType::T2):
        baseColonies = 2;
        break;
    case(PlanetType::T3):
        baseColonies = PlanetUtils::Homeworld(planet) ? warfareConfig->GetCitiesInHomeworld() : 3;
        break;
    default:
        return 0;
    }

    // Учет реальных и закэшированных колоний из Unlimited-Colony даже в фоновом режиме галактики
    int actualColonies = baseColonies;
    int cachedColonies = spaceCombatMetrics->GetCachedCityCount(planet);
    if (cachedColonies > actualColonies) {
        actualColonies = cachedColonies;
    }

    int buildingsPerColony = warfareConfig->GetBuildingsInT1Colony();
    if (planet->mType == PlanetType::T2) {
        buildingsPerColony = warfareConfig->GetBuildingsInT2Colony();
    }
    else if (planet->mType == PlanetType::T3) {
        buildingsPerColony = warfareConfig->GetBuildingsInT3Colony();
    }

    int totalBuildings = actualColonies * buildingsPerColony;
    int ratio = max(warfareConfig->GetBuildingsPerFactoryRatio(), 1);
    return max(totalBuildings / ratio, 1);
}

float cWarfareStrengthAnalyzer::GetBombersProducedByPlanet(Simulator::cPlanetRecord* planet){
    if (planet != nullptr && planet->GetTechLevel() == TechLevel::Empire) {
        return GetFactoryCountInPlanet(planet) * GetBombersProducedByFactory(planet->mSpiceGen);
    }
    else {
        return 0.0f;
    }
}

float cWarfareStrengthAnalyzer::GetBombersProducedBySystem(Simulator::cStarRecord* star){
    float bombersProduced = 0;
    if (star == nullptr) return 0.0f;
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
    if (empire == nullptr) return 0.0f;
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
    if (empire == nullptr) return 0.0f;
    float bombersProducedByEmpire = GetBombersProducedByEmpire(empire);
    float empireBomberHealth = spaceCombatMetrics->GetBomberHealthByEmpire(empire);
    float maxLevelBomberHealth = max(spaceCombatMetrics->GetBomberHealthByLevel(4), 1.0f);
    float bomberHealthRatio = empireBomberHealth / maxLevelBomberHealth;
    return bombersProducedByEmpire * bomberHealthRatio;
}
