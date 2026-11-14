#pragma once

#include <Spore\BasicIncludes.h>
#include <EASTL\map.h>
#include <EASTL\utility.h>

#define cSpaceCombatMetricsPtr intrusive_ptr<cSpaceCombatMetrics>

// Stores unit and defense stats used in space combat and
// calculates those values given specific empires or planets.
class cSpaceCombatMetrics 
	: public Object
	, public DefaultRefCounted
{
public:
	static const uint32_t TYPE = id("SpaceWarfareOverhaul::cSpaceCombatMetrics");
	
	cSpaceCombatMetrics(ResourceKey spaceCombatKey);
	~cSpaceCombatMetrics();

	int AddRef() override;
	int Release() override;
	void* Cast(uint32_t type) const override;

	float GetBomberHealthByLevel(int empireLevel);
	float GetFighterHealthByLevel(int empireLevel);
	float GetDefenderHealthByLevel(int empireLevel);
	float GetTurretHealthByLevel(int empireLevel);

	int GetDefendersInOutpost();
	int GetDefendersInColony();
	int GetFightersPerBomber();

	int GetTurretsInT1();
	int GetTurretsInT2();
	int GetTurretsInT3();

	float GetBomberHealthByEmpire(Simulator::cEmpire* empire);
	float GetFighterHealthByEmpire(Simulator::cEmpire* empire);
	float GetDefenderHealthByEmpire(Simulator::cEmpire* empire);
	float GetTurretHealthByEmpire(Simulator::cEmpire* empire);

	int GetDefendersInPlanet(Simulator::cPlanetRecord* planet);
	int GetTurretsInPlanet(Simulator::cPlanetRecord* planet);

	// Кэш посещенных планет для Unlimited-Colony (города и турели)
	void UpdatePlanetDefenseCache(Simulator::cPlanetRecord* planet);
	int GetCachedCityCount(Simulator::cPlanetRecord* planet) const;
	int GetCachedTurretCount(Simulator::cPlanetRecord* planet) const;

	eastl::vector<float> healthBomber;
	eastl::vector<float> healthFighter;
	eastl::vector<float> healthDefender;
	eastl::vector<float> healthTurret;

	int numDefendersInOutpost;
	int numDefendersInColony;
	int numFightersPerBomber;

	int numTurretsInT1;
	int numTurretsInT2;
	int numTurretsInT3;

private:
	eastl::map<cPlanetRecordPtr, eastl::pair<int, int>> planetDefenseCache;
};
