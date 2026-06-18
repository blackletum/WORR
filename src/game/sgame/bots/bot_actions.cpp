// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "../g_local.hpp"
#include "bot_actions.hpp"

#include <algorithm>
#include <limits>

namespace {
constexpr int BOT_ACTION_USE_WORLD_PRIORITY = 60;
constexpr int BOT_ACTION_USE_INVENTORY_PRIORITY = 55;

BotActionStatus botActionStatus;

static_assert(static_cast<int>(BotActionIntent::None) == 0);
static_assert(static_cast<int>(BotItemDecisionKind::None) == 0);
static_assert(static_cast<int>(BotCombatDecisionKind::None) == 0);

int BotActions_ClampDistanceSquared(float distanceSquared) {
	if (distanceSquared <= 0.0f) {
		return 0;
	}
	if (distanceSquared >= static_cast<float>(std::numeric_limits<int>::max())) {
		return std::numeric_limits<int>::max();
	}
	return static_cast<int>(distanceSquared);
}

int BotActions_ArmorValue(const gclient_t *client) {
	if (client == nullptr) {
		return 0;
	}

	const auto &inventory = client->pers.inventory;
	return std::max(
		std::max(inventory[IT_ARMOR_BODY], inventory[IT_ARMOR_COMBAT]),
		std::max(inventory[IT_ARMOR_JACKET], inventory[IT_ARMOR_SHARD]));
}

int BotActions_WeaponAmmo(const gclient_t *client, const Item *weapon) {
	if (client == nullptr || weapon == nullptr || weapon->ammo == IT_NULL) {
		return 0;
	}
	return client->pers.inventory[weapon->ammo];
}

bool BotActions_WeaponReady(const gclient_t *client, const Item *weapon) {
	if (client == nullptr || weapon == nullptr) {
		return false;
	}
	if (weapon->ammo == IT_NULL) {
		return true;
	}
	const int requiredAmmo = std::max(weapon->quantity, 1);
	return client->pers.inventory[weapon->ammo] >= requiredAmmo;
}

bool BotActions_EnemyAlive(const gentity_t *enemy) {
	return enemy != nullptr && enemy->inUse && enemy->health > 0 && !enemy->deadFlag;
}

bool BotActions_DecisionMayTouchCommand(const BotActionDecision &decision) {
	switch (decision.intent) {
	case BotActionIntent::Attack:
		return decision.pressAttack && !decision.pressUse;
	case BotActionIntent::UseWorld:
		return decision.pressUse && !decision.pressAttack;
	default:
		return !decision.pressAttack && !decision.pressUse;
	}
}

bool BotActions_DecisionHasIntent(const BotActionDecision &decision) {
	return decision.intent != BotActionIntent::None && decision.priority > 0;
}

BotActionDecision BotActions_MakeMoveToItemDecision(const BotItemDecision &itemDecision) {
	if (itemDecision.kind != BotItemDecisionKind::SeekCandidate) {
		return {};
	}

	return {
		.intent = BotActionIntent::MoveToItem,
		.priority = itemDecision.priority,
		.item = itemDecision.item,
		.entity = itemDecision.entity,
		.reason = itemDecision.reason,
	};
}

BotActionDecision BotActions_MakeCombatDecision(const BotCombatDecision &combatDecision) {
	switch (combatDecision.kind) {
	case BotCombatDecisionKind::SwitchWeapon:
		return {
			.intent = BotActionIntent::SwitchWeapon,
			.priority = combatDecision.priority,
			.weaponItem = combatDecision.weaponItem,
			.wantsWeaponSwitch = true,
			.reason = combatDecision.reason,
		};
	case BotCombatDecisionKind::FireWeapon:
		return {
			.intent = BotActionIntent::Attack,
			.priority = combatDecision.priority,
			.weaponItem = combatDecision.weaponItem,
			.pressAttack = combatDecision.pressAttack,
			.reason = combatDecision.reason,
		};
	default:
		return {};
	}
}

BotActionDecision BotActions_HigherPriority(BotActionDecision current, const BotActionDecision &candidate) {
	if (BotActions_DecisionHasIntent(candidate) && candidate.priority > current.priority) {
		return candidate;
	}
	return current;
}

void BotActions_RecordDecision(const BotActionContext &context, const BotActionDecision &decision) {
	botActionStatus.lastClientIndex = context.clientIndex;
	botActionStatus.lastIntent = decision.intent;
	botActionStatus.lastPriority = decision.priority;
	botActionStatus.lastItem = decision.item;
	botActionStatus.lastEntity = decision.entity;
	botActionStatus.lastWeaponItem = decision.weaponItem;

	switch (decision.intent) {
	case BotActionIntent::MoveToItem:
		botActionStatus.moveToItemDecisions++;
		break;
	case BotActionIntent::SwitchWeapon:
		botActionStatus.weaponSwitchDecisions++;
		break;
	case BotActionIntent::Attack:
		botActionStatus.attackDecisions++;
		break;
	case BotActionIntent::UseWorld:
		botActionStatus.useWorldDecisions++;
		break;
	case BotActionIntent::UseInventory:
		botActionStatus.useInventoryDecisions++;
		break;
	default:
		botActionStatus.noopDecisions++;
		break;
	}
}
} // namespace

void BotActions_ResetStatus() {
	botActionStatus = {};
	BotItems_ResetStatus();
	BotCombat_ResetStatus();
}

BotActionContext BotActions_BuildContext(const gentity_t *bot) {
	BotActionContext context{};
	if (bot == nullptr || !bot->inUse || bot->client == nullptr) {
		return context;
	}

	const bool isBot = ((bot->svFlags & SVF_BOT) != 0) || bot->client->sess.is_a_bot;
	if (!isBot) {
		return context;
	}

	context.valid = true;
	context.alive = bot->health > 0 && !bot->deadFlag && !bot->client->eliminated;
	context.clientIndex = static_cast<int>(bot->s.number) - 1;
	context.health = bot->health;
	context.maxHealth = bot->maxHealth;
	context.armor = BotActions_ArmorValue(bot->client);

	context.item.lowHealth = context.maxHealth > 0 && context.health * 100 <= context.maxHealth * 45;
	context.item.lowArmor = context.armor > 0 && context.armor < 25;

	const Item *currentWeapon = bot->client->pers.weapon;
	const Item *pendingWeapon = bot->client->weapon.pending;
	const Item *preferredWeapon = pendingWeapon != nullptr ? pendingWeapon : currentWeapon;

	context.combat.currentWeaponItem = currentWeapon != nullptr ? currentWeapon->id : IT_NULL;
	context.combat.currentWeaponAmmo = BotActions_WeaponAmmo(bot->client, currentWeapon);
	context.combat.currentWeaponReady = BotActions_WeaponReady(bot->client, currentWeapon);
	context.combat.preferredWeaponItem = preferredWeapon != nullptr ? preferredWeapon->id : IT_NULL;
	context.combat.preferredWeaponAmmo = BotActions_WeaponAmmo(bot->client, preferredWeapon);
	context.combat.preferredWeaponReady = BotActions_WeaponReady(bot->client, preferredWeapon);

	if (BotActions_EnemyAlive(bot->enemy)) {
		context.combat.hasEnemy = true;
		const Vector3 delta = bot->enemy->s.origin - bot->s.origin;
		context.combat.enemyDistanceSquared = BotActions_ClampDistanceSquared(delta.lengthSquared());
	}

	return context;
}

BotActionDecision BotActions_Decide(const BotActionContext &context) {
	botActionStatus.evaluations++;
	if (!context.valid) {
		botActionStatus.invalidContexts++;
		return {};
	}
	if (!context.alive) {
		botActionStatus.deadContexts++;
		return {};
	}

	botActionStatus.itemEvaluations++;
	const BotItemDecision itemDecision = BotItems_Evaluate(context.item);

	botActionStatus.combatEvaluations++;
	const BotCombatDecision combatDecision = BotCombat_Evaluate(context.combat);

	BotActionDecision decision = BotActions_MakeMoveToItemDecision(itemDecision);
	decision = BotActions_HigherPriority(decision, BotActions_MakeCombatDecision(combatDecision));

	if (context.useWorldRequested) {
		decision = BotActions_HigherPriority(decision, {
			.intent = BotActionIntent::UseWorld,
			.priority = BOT_ACTION_USE_WORLD_PRIORITY,
			.pressUse = true,
			.reason = "world_use",
		});
	}

	if (context.inventoryUseRequested && context.inventoryItem > 0) {
		decision = BotActions_HigherPriority(decision, {
			.intent = BotActionIntent::UseInventory,
			.priority = BOT_ACTION_USE_INVENTORY_PRIORITY,
			.item = context.inventoryItem,
			.wantsInventoryUse = true,
			.reason = "inventory_use",
		});
	}

	BotActions_RecordDecision(context, decision);
	return decision;
}

bool BotActions_ApplyDecision(const BotActionDecision &decision, usercmd_t *cmd) {
	if (cmd == nullptr ||
		!BotActions_DecisionHasIntent(decision) ||
		!BotActions_DecisionMayTouchCommand(decision)) {
		return false;
	}

	bool applied = false;
	if (decision.pressAttack) {
		cmd->buttons |= BUTTON_ATTACK;
		botActionStatus.appliedAttackButtons++;
		applied = true;
	}
	if (decision.pressUse) {
		cmd->buttons |= BUTTON_USE;
		botActionStatus.appliedUseButtons++;
		applied = true;
	}
	if (decision.wantsWeaponSwitch) {
		botActionStatus.pendingWeaponSwitches++;
	}
	if (decision.wantsInventoryUse) {
		botActionStatus.pendingInventoryUses++;
	}
	if (applied) {
		botActionStatus.appliedCommands++;
	}
	return applied;
}

const BotActionStatus &BotActions_GetStatus() {
	return botActionStatus;
}

const char *BotActions_IntentName(BotActionIntent intent) {
	switch (intent) {
	case BotActionIntent::MoveToItem:
		return "move_to_item";
	case BotActionIntent::SwitchWeapon:
		return "switch_weapon";
	case BotActionIntent::Attack:
		return "attack";
	case BotActionIntent::UseWorld:
		return "use_world";
	case BotActionIntent::UseInventory:
		return "use_inventory";
	default:
		return "none";
	}
}
