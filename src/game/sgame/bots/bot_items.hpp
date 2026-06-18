// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#pragma once

enum class BotItemDecisionKind {
	None,
	SeekCandidate,
};

// Caller supplies candidate facts from the current item/nav owner. This module
// scores intent only; it does not discover, reserve, or clear item goals.
struct BotItemContext {
	bool candidateAvailable = false;
	bool candidateReserved = false;
	bool lowHealth = false;
	bool lowArmor = false;
	int candidateEntity = -1;
	int candidateItem = 0;
	int candidateScore = 0;
};

// SeekCandidate means "prefer moving toward this pickup" and does not mutate
// route ownership. The caller decides whether/how to translate it to nav input.
struct BotItemDecision {
	BotItemDecisionKind kind = BotItemDecisionKind::None;
	int priority = 0;
	int item = 0;
	int entity = -1;
	const char *reason = "none";
};

// Process-local counters accumulate until BotItems_ResetStatus().
struct BotItemStatus {
	int evaluations = 0;
	int invalidCandidates = 0;
	int reservedDeferrals = 0;
	int seekDecisions = 0;
	int lowHealthBoosts = 0;
	int lowArmorBoosts = 0;
	int lastItem = 0;
	int lastEntity = -1;
	int lastPriority = 0;
};

void BotItems_ResetStatus();
BotItemDecision BotItems_Evaluate(const BotItemContext &context);
const BotItemStatus &BotItems_GetStatus();
const char *BotItems_DecisionName(BotItemDecisionKind kind);
