// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "bot_items.hpp"

namespace {
constexpr int BOT_ITEM_LOW_HEALTH_PRIORITY_BOOST = 35;
constexpr int BOT_ITEM_LOW_ARMOR_PRIORITY_BOOST = 15;

BotItemStatus botItemStatus;

static_assert(static_cast<int>(BotItemDecisionKind::None) == 0);
} // namespace

void BotItems_ResetStatus() {
	botItemStatus = {};
}

BotItemDecision BotItems_Evaluate(const BotItemContext &context) {
	botItemStatus.evaluations++;

	if (!context.candidateAvailable || context.candidateEntity < 0 || context.candidateItem <= 0) {
		botItemStatus.invalidCandidates++;
		return {};
	}

	if (context.candidateReserved) {
		botItemStatus.reservedDeferrals++;
		return {};
	}

	int priority = context.candidateScore;
	const char *reason = "candidate";
	if (context.lowHealth) {
		priority += BOT_ITEM_LOW_HEALTH_PRIORITY_BOOST;
		reason = "low_health";
		botItemStatus.lowHealthBoosts++;
	} else if (context.lowArmor) {
		priority += BOT_ITEM_LOW_ARMOR_PRIORITY_BOOST;
		reason = "low_armor";
		botItemStatus.lowArmorBoosts++;
	}

	if (priority <= 0) {
		botItemStatus.invalidCandidates++;
		return {};
	}

	botItemStatus.seekDecisions++;
	botItemStatus.lastItem = context.candidateItem;
	botItemStatus.lastEntity = context.candidateEntity;
	botItemStatus.lastPriority = priority;

	return {
		.kind = BotItemDecisionKind::SeekCandidate,
		.priority = priority,
		.item = context.candidateItem,
		.entity = context.candidateEntity,
		.reason = reason,
	};
}

const BotItemStatus &BotItems_GetStatus() {
	return botItemStatus;
}

const char *BotItems_DecisionName(BotItemDecisionKind kind) {
	switch (kind) {
	case BotItemDecisionKind::SeekCandidate:
		return "seek_candidate";
	default:
		return "none";
	}
}
