// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#pragma once

#include "bot_combat.hpp"
#include "bot_items.hpp"

struct gentity_t;
struct usercmd_t;

// Decision vocabulary only. Navigation ownership stays with bot_nav.*, and
// weapon/inventory command dispatch must be provided by a later integration.
enum class BotActionIntent {
	None,
	MoveToItem,
	SwitchWeapon,
	Attack,
	UseWorld,
	UseInventory,
};

// Caller-owned frame facts. BotActions_BuildContext() fills the bot-local
// fields; future brain/perception/nav owners may enrich item/combat/use fields.
struct BotActionContext {
	bool valid = false;
	bool alive = false;
	bool useWorldRequested = false;
	bool inventoryUseRequested = false;
	int clientIndex = -1;
	int health = 0;
	int maxHealth = 0;
	int armor = 0;
	int inventoryItem = 0;
	BotItemContext item{};
	BotCombatContext combat{};
};

// ApplyDecision only mutates usercmd_t buttons for pressAttack/pressUse.
// wantsWeaponSwitch and wantsInventoryUse are intent-only telemetry today.
struct BotActionDecision {
	BotActionIntent intent = BotActionIntent::None;
	int priority = 0;
	int item = 0;
	int entity = -1;
	int weaponItem = 0;
	bool pressAttack = false;
	bool pressUse = false;
	bool wantsWeaponSwitch = false;
	bool wantsInventoryUse = false;
	const char *reason = "none";
};

// Process-local counters. They accumulate until BotActions_ResetStatus() and
// references returned by BotActions_GetStatus() are borrowed, not owned.
struct BotActionStatus {
	int evaluations = 0;
	int invalidContexts = 0;
	int deadContexts = 0;
	int itemEvaluations = 0;
	int combatEvaluations = 0;
	int moveToItemDecisions = 0;
	int weaponSwitchDecisions = 0;
	int attackDecisions = 0;
	int useWorldDecisions = 0;
	int useInventoryDecisions = 0;
	int noopDecisions = 0;
	int appliedCommands = 0;
	int appliedAttackButtons = 0;
	int appliedUseButtons = 0;
	int pendingWeaponSwitches = 0;
	int pendingInventoryUses = 0;
	int lastClientIndex = -1;
	int lastPriority = 0;
	int lastItem = 0;
	int lastEntity = -1;
	int lastWeaponItem = 0;
	BotActionIntent lastIntent = BotActionIntent::None;
};

void BotActions_ResetStatus();
BotActionContext BotActions_BuildContext(const gentity_t *bot);
BotActionDecision BotActions_Decide(const BotActionContext &context);
bool BotActions_ApplyDecision(const BotActionDecision &decision, usercmd_t *cmd);
const BotActionStatus &BotActions_GetStatus();
const char *BotActions_IntentName(BotActionIntent intent);
