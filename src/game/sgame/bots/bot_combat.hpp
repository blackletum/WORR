// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#pragma once

enum class BotCombatDecisionKind {
	None,
	SwitchWeapon,
	FireWeapon,
};

// Caller owns perception facts. hasEnemy alone is not enough to fire; visible
// and shootable are separate so future aim/perception policy stays explicit.
struct BotCombatContext {
	bool hasEnemy = false;
	bool enemyVisible = false;
	bool enemyShootable = false;
	bool currentWeaponReady = false;
	bool preferredWeaponReady = false;
	bool skillAllowsFire = true;
	int currentWeaponItem = 0;
	int preferredWeaponItem = 0;
	int currentWeaponAmmo = 0;
	int preferredWeaponAmmo = 0;
	int enemyDistanceSquared = 0;
};

// SwitchWeapon is intent-only. FireWeapon may request BUTTON_ATTACK through the
// action dispatcher, but does not aim or choose movement.
struct BotCombatDecision {
	BotCombatDecisionKind kind = BotCombatDecisionKind::None;
	int priority = 0;
	int weaponItem = 0;
	bool pressAttack = false;
	const char *reason = "none";
};

// Process-local counters accumulate until BotCombat_ResetStatus().
struct BotCombatStatus {
	int evaluations = 0;
	int noEnemy = 0;
	int blockedSight = 0;
	int weaponSwitchDecisions = 0;
	int fireDecisions = 0;
	int withheldFire = 0;
	int lastWeaponItem = 0;
	int lastPriority = 0;
	int lastEnemyDistanceSquared = 0;
};

void BotCombat_ResetStatus();
BotCombatDecision BotCombat_Evaluate(const BotCombatContext &context);
const BotCombatStatus &BotCombat_GetStatus();
const char *BotCombat_DecisionName(BotCombatDecisionKind kind);
