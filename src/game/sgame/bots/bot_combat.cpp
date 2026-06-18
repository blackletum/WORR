// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "bot_combat.hpp"

namespace {
constexpr int BOT_COMBAT_SWITCH_WEAPON_PRIORITY = 80;
constexpr int BOT_COMBAT_FIRE_PRIORITY = 70;
constexpr int BOT_COMBAT_CLOSE_RANGE_BONUS = 10;
constexpr int BOT_COMBAT_CLOSE_RANGE_DIST_SQUARED = 512 * 512;

BotCombatStatus botCombatStatus;

static_assert(static_cast<int>(BotCombatDecisionKind::None) == 0);
} // namespace

void BotCombat_ResetStatus() {
	botCombatStatus = {};
}

BotCombatDecision BotCombat_Evaluate(const BotCombatContext &context) {
	botCombatStatus.evaluations++;
	botCombatStatus.lastEnemyDistanceSquared = context.enemyDistanceSquared;

	if (!context.hasEnemy) {
		botCombatStatus.noEnemy++;
		return {};
	}

	if (context.preferredWeaponReady &&
		context.preferredWeaponItem > 0 &&
		context.currentWeaponItem > 0 &&
		context.preferredWeaponItem != context.currentWeaponItem) {
		botCombatStatus.weaponSwitchDecisions++;
		botCombatStatus.lastWeaponItem = context.preferredWeaponItem;
		botCombatStatus.lastPriority = BOT_COMBAT_SWITCH_WEAPON_PRIORITY;
		return {
			.kind = BotCombatDecisionKind::SwitchWeapon,
			.priority = BOT_COMBAT_SWITCH_WEAPON_PRIORITY,
			.weaponItem = context.preferredWeaponItem,
			.reason = "preferred_weapon",
		};
	}

	if (!context.enemyVisible || !context.enemyShootable) {
		botCombatStatus.blockedSight++;
		return {};
	}

	if (!context.currentWeaponReady || !context.skillAllowsFire) {
		botCombatStatus.withheldFire++;
		return {};
	}

	int priority = BOT_COMBAT_FIRE_PRIORITY;
	const char *reason = "shootable_enemy";
	if (context.enemyDistanceSquared > 0 &&
		context.enemyDistanceSquared <= BOT_COMBAT_CLOSE_RANGE_DIST_SQUARED) {
		priority += BOT_COMBAT_CLOSE_RANGE_BONUS;
		reason = "close_enemy";
	}

	botCombatStatus.fireDecisions++;
	botCombatStatus.lastWeaponItem = context.currentWeaponItem;
	botCombatStatus.lastPriority = priority;
	return {
		.kind = BotCombatDecisionKind::FireWeapon,
		.priority = priority,
		.weaponItem = context.currentWeaponItem,
		.pressAttack = true,
		.reason = reason,
	};
}

const BotCombatStatus &BotCombat_GetStatus() {
	return botCombatStatus;
}

const char *BotCombat_DecisionName(BotCombatDecisionKind kind) {
	switch (kind) {
	case BotCombatDecisionKind::SwitchWeapon:
		return "switch_weapon";
	case BotCombatDecisionKind::FireWeapon:
		return "fire_weapon";
	default:
		return "none";
	}
}
