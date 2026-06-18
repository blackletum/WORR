/*Copyright (c) 2024 ZeniMax Media Inc.
Licensed under the GNU General Public License 2.0.

bot_brain.cpp implementation.*/

#include "../g_local.hpp"
#include "bot_actions.hpp"
#include "bot_nav.hpp"
#include "botlib_adapter.hpp"
#include "bot_brain.hpp"
#include "bot_runtime.hpp"

namespace {

struct BotFrameCommandStatus {
	int frames = 0;
	int commands = 0;
	int skippedInvalid = 0;
	int skippedRuntime = 0;
	int skippedInactive = 0;
	int skippedNotBot = 0;
	int routeCommands = 0;
	int lookAheadAttempts = 0;
	int lookAheadUses = 0;
	int lastLookAheadIndex = -1;
	int lastLookAheadPointCount = 0;
	int velocityLeadAttempts = 0;
	int velocityLeadUses = 0;
	int lastVelocityLeadSpeedSquared = 0;
	int lastVelocityLeadOffsetSquared = 0;
	int movementStateAttempts = 0;
	int movementStateCommands = 0;
	int movementStateJumpCommands = 0;
	int movementStateCrouchCommands = 0;
	int movementStateSwimCommands = 0;
	int movementStateWaterJumpCommands = 0;
	int movementStateLadderCommands = 0;
	int movementStateUnsupported = 0;
	int naturalMovementStateCommands = 0;
	int naturalMovementStateCrouchCommands = 0;
	int naturalMovementStateSwimCommands = 0;
	int naturalMovementStateWaterJumpCommands = 0;
	int lastMovementStateTravelType = 0;
	int lastMovementStateForcedTravelType = 0;
	int lastMovementStateButtons = 0;
	int recoveryCommandUses = 0;
	int lastRecoveryForwardMove = 0;
	int lastRecoverySideMove = 0;
	int lastRecoveryFramesRemaining = 0;
	int interactionWaitCommandUses = 0;
	int interactionUseCommandUses = 0;
	int lastInteractionCommandAction = 0;
	int lastInteractionCommandEntity = -1;
	int travelTypeGoalStartWarps = 0;
	int lastTravelTypeGoalStartType = 0;
	int lastTravelTypeGoalStartArea = 0;
	int lastTravelTypeGoalStartGoalArea = 0;
};

BotFrameCommandStatus botFrameCommandStatus;

constexpr float BOT_COMMAND_LOOKAHEAD_DIST_SQUARED = 256.0f * 256.0f;
constexpr float BOT_COMMAND_VELOCITY_LEAD_SECONDS = 0.10f;
constexpr float BOT_COMMAND_VELOCITY_MIN_SPEED_SQUARED = 12.0f * 12.0f;
constexpr float BOT_COMMAND_VERTICAL_INTENT_EPSILON = 8.0f;
constexpr float BOT_COMMAND_STUCK_RECOVERY_FORWARD_MOVE = -80.0f;
constexpr float BOT_COMMAND_STUCK_RECOVERY_SIDE_MOVE = 140.0f;

constexpr int BOT_COMMAND_TRAVEL_WALK = 2;
constexpr int BOT_COMMAND_TRAVEL_CROUCH = 3;
constexpr int BOT_COMMAND_TRAVEL_BARRIER_JUMP = 4;
constexpr int BOT_COMMAND_TRAVEL_JUMP = 5;
constexpr int BOT_COMMAND_TRAVEL_LADDER = 6;
constexpr int BOT_COMMAND_TRAVEL_WALK_OFF_LEDGE = 7;
constexpr int BOT_COMMAND_TRAVEL_SWIM = 8;
constexpr int BOT_COMMAND_TRAVEL_WATER_JUMP = 9;
constexpr int BOT_COMMAND_TRAVEL_ELEVATOR = 11;
constexpr int BOT_COMMAND_TRAVEL_ROCKET_JUMP = 12;

byte Bot_CommandMsec() {
	const int frameTimeMs = gi.frameTimeMs > 0 ? static_cast<int>(gi.frameTimeMs) : 1;
	return static_cast<byte>(std::clamp(frameTimeMs, 1, 255));
}

int Bot_CommandSmokeForcedTravelType() {
	static cvar_t *smokeTravelType = nullptr;
	if (smokeTravelType == nullptr && gi.cvar != nullptr) {
		smokeTravelType = gi.cvar("sg_bot_frame_command_smoke_travel_type", "0", CVAR_NOFLAGS);
	}
	return smokeTravelType != nullptr ? smokeTravelType->integer : 0;
}

bool Bot_CommandPositionGoalEnabled() {
	static cvar_t *positionGoalEnable = nullptr;
	if (positionGoalEnable == nullptr && gi.cvar != nullptr) {
		positionGoalEnable = gi.cvar("sg_bot_nav_position_goal_enable", "0", CVAR_NOFLAGS);
	}
	return positionGoalEnable != nullptr && positionGoalEnable->integer > 0;
}

int Bot_CommandTravelTypeGoal() {
	static cvar_t *travelTypeGoal = nullptr;
	if (travelTypeGoal == nullptr && gi.cvar != nullptr) {
		travelTypeGoal = gi.cvar("sg_bot_nav_travel_type_goal", "0", CVAR_NOFLAGS);
	}
	return travelTypeGoal != nullptr ? travelTypeGoal->integer : 0;
}

bool Bot_CommandTravelTypeGoalWarpEnabled() {
	static cvar_t *travelTypeGoalWarp = nullptr;
	if (travelTypeGoalWarp == nullptr && gi.cvar != nullptr) {
		travelTypeGoalWarp = gi.cvar("sg_bot_nav_travel_type_goal_warp", "0", CVAR_NOFLAGS);
	}
	return travelTypeGoalWarp != nullptr && travelTypeGoalWarp->integer > 0;
}

bool Bot_CommandRocketJumpAllowed() {
	static cvar_t *allowRocketJump = nullptr;
	if (allowRocketJump == nullptr && gi.cvar != nullptr) {
		allowRocketJump = gi.cvar("sg_bot_allow_rocketjump", "0", CVAR_NOFLAGS);
	}
	return allowRocketJump != nullptr && allowRocketJump->integer > 0;
}

bool Bot_CommandTravelTypeGoalExpectBlocked() {
	static cvar_t *expectBlocked = nullptr;
	if (expectBlocked == nullptr && gi.cvar != nullptr) {
		expectBlocked = gi.cvar("sg_bot_nav_travel_type_goal_expect_blocked", "0", CVAR_NOFLAGS);
	}
	return expectBlocked != nullptr && expectBlocked->integer > 0;
}

bool Bot_CommandSmokeSoak() {
	static cvar_t *soak = nullptr;
	if (soak == nullptr && gi.cvar != nullptr) {
		soak = gi.cvar("sg_bot_frame_command_smoke_soak", "0", CVAR_NOFLAGS);
	}
	return soak != nullptr && soak->integer > 0;
}

void Bot_CommandBuildRouteRequest(BotNavRouteRequest *request) {
	if (request == nullptr) {
		return;
	}

	*request = {};
	const int travelTypeGoal = Bot_CommandTravelTypeGoal();
	if (travelTypeGoal > 0) {
		request->hasTravelTypeGoal = true;
		request->travelTypeGoal = travelTypeGoal;
	}
	if (!Bot_CommandPositionGoalEnabled()) {
		return;
	}

	static cvar_t *positionGoalX = nullptr;
	static cvar_t *positionGoalY = nullptr;
	static cvar_t *positionGoalZ = nullptr;
	if (positionGoalX == nullptr && gi.cvar != nullptr) {
		positionGoalX = gi.cvar("sg_bot_nav_position_goal_x", "0", CVAR_NOFLAGS);
		positionGoalY = gi.cvar("sg_bot_nav_position_goal_y", "0", CVAR_NOFLAGS);
		positionGoalZ = gi.cvar("sg_bot_nav_position_goal_z", "0", CVAR_NOFLAGS);
	}

	request->hasPositionGoal = true;
	request->positionGoal[0] = positionGoalX != nullptr ? positionGoalX->value : 0.0f;
	request->positionGoal[1] = positionGoalY != nullptr ? positionGoalY->value : 0.0f;
	request->positionGoal[2] = positionGoalZ != nullptr ? positionGoalZ->value : 0.0f;
}

void Bot_CommandMaybeWarpToTravelTypeGoalStart(gentity_t *bot, const BotNavRouteRequest &request) {
	if (bot == nullptr ||
		bot->client == nullptr ||
		!request.hasTravelTypeGoal ||
		request.travelTypeGoal <= 0 ||
		request.hasPositionGoal ||
		!Bot_CommandTravelTypeGoalWarpEnabled() ||
		botFrameCommandStatus.travelTypeGoalStartWarps > 0) {
		return;
	}

	float startOrigin[3] = {};
	int startArea = 0;
	int goalArea = 0;
	BotLibAdapter_SetRoutePolicy(Bot_CommandRocketJumpAllowed());
	if (!BotLibAdapter_FindRouteStartForTravelType(
			request.travelTypeGoal,
			startOrigin,
			&startArea,
			&goalArea) ||
		startArea <= 0 ||
		goalArea <= 0) {
		return;
	}

	const int clientIndex = static_cast<int>(bot->s.number) - 1;
	Vector3 teleportOrigin = { startOrigin[0], startOrigin[1], startOrigin[2] - 10.0f };
	TeleportPlayer(bot, teleportOrigin, vec3_origin);
	bot->velocity = vec3_origin;
	if (clientIndex >= 0 && clientIndex < static_cast<int>(game.maxClients)) {
		BotNav_ResetClient(clientIndex);
	}

	botFrameCommandStatus.travelTypeGoalStartWarps++;
	botFrameCommandStatus.lastTravelTypeGoalStartType = request.travelTypeGoal;
	botFrameCommandStatus.lastTravelTypeGoalStartArea = startArea;
	botFrameCommandStatus.lastTravelTypeGoalStartGoalArea = goalArea;
}

Vector3 Bot_CommandRoutePoint(const BotLibAdapterRouteSteer &route, int pointIndex) {
	return {
		route.routePoints[pointIndex][0],
		route.routePoints[pointIndex][1],
		route.routePoints[pointIndex][2]
	};
}

float Bot_CommandHorizontalDistanceSquared(const Vector3 &a, const Vector3 &b) {
	const float dx = a.x - b.x;
	const float dy = a.y - b.y;
	return dx * dx + dy * dy;
}

Vector3 Bot_CommandApplyVelocityLead(const gentity_t *bot, const Vector3 &target, const Vector3 &currentDirection) {
	Vector3 velocity = bot->velocity;
	velocity.z = 0.0f;

	const float speedSquared = velocity.lengthSquared();
	botFrameCommandStatus.velocityLeadAttempts++;
	botFrameCommandStatus.lastVelocityLeadSpeedSquared = static_cast<int>(speedSquared);
	botFrameCommandStatus.lastVelocityLeadOffsetSquared = 0;

	if (speedSquared < BOT_COMMAND_VELOCITY_MIN_SPEED_SQUARED) {
		return currentDirection;
	}

	const Vector3 leadOrigin = bot->s.origin + (velocity * BOT_COMMAND_VELOCITY_LEAD_SECONDS);
	const float leadOffsetSquared = Bot_CommandHorizontalDistanceSquared(bot->s.origin, leadOrigin);
	botFrameCommandStatus.lastVelocityLeadOffsetSquared = static_cast<int>(leadOffsetSquared);

	if (leadOffsetSquared >= currentDirection.lengthSquared()) {
		return currentDirection;
	}

	Vector3 leadDirection = target - leadOrigin;
	leadDirection.z = 0.0f;
	if (leadDirection.lengthSquared() < 1.0f) {
		return currentDirection;
	}

	botFrameCommandStatus.velocityLeadUses++;
	return leadDirection;
}

Vector3 Bot_CommandSelectRouteTarget(const gentity_t *bot, const BotLibAdapterRouteSteer &route) {
	const int routePointCount = std::clamp(route.routePointCount, 0, BOTLIB_ADAPTER_MAX_ROUTE_POINTS);
	Vector3 target = {
		route.moveTarget[0],
		route.moveTarget[1],
		route.moveTarget[2]
	};
	int selectedIndex = -1;

	botFrameCommandStatus.lastLookAheadPointCount = routePointCount;
	botFrameCommandStatus.lastLookAheadIndex = -1;
	if (routePointCount <= 0) {
		return target;
	}

	target = Bot_CommandRoutePoint(route, 0);
	selectedIndex = 0;

	if (routePointCount > 1) {
		botFrameCommandStatus.lookAheadAttempts++;
		for (int pointIndex = 1; pointIndex < routePointCount; ++pointIndex) {
			const Vector3 point = Bot_CommandRoutePoint(route, pointIndex);
			if (Bot_CommandHorizontalDistanceSquared(bot->s.origin, point) > BOT_COMMAND_LOOKAHEAD_DIST_SQUARED) {
				break;
			}

			target = point;
			selectedIndex = pointIndex;
		}
	}

	botFrameCommandStatus.lastLookAheadIndex = selectedIndex;
	if (selectedIndex > 0) {
		botFrameCommandStatus.lookAheadUses++;
	}
	return target;
}

Vector3 Bot_CommandAnglesToTarget( const gentity_t * bot, const BotLibAdapterRouteSteer & route ) {
	Vector3 target = Bot_CommandSelectRouteTarget(bot, route);
	Vector3 direction = target - bot->s.origin;
	direction.z = 0.0f;

	if (direction.lengthSquared() < 1.0f) {
		target = {
			route.goalOrigin[0],
			route.goalOrigin[1],
			route.goalOrigin[2]
		};
		direction = target - bot->s.origin;
		direction.z = 0.0f;
	}

	if (direction.lengthSquared() >= 1.0f) {
		direction = Bot_CommandApplyVelocityLead(bot, target, direction);
	}

	if (direction.lengthSquared() < 1.0f) {
		Vector3 angles = bot->client->vAngle;
		if (angles == vec3_origin) {
			angles = bot->s.angles;
		}
		angles[PITCH] = 0.0f;
		angles[ROLL] = 0.0f;
		return angles;
	}

	Vector3 angles = VectorToAngles(direction);
	angles[PITCH] = 0.0f;
	angles[ROLL] = 0.0f;
	angles[YAW] = anglemod(angles[YAW]);
	return angles;
}

bool Bot_CommandRouteTargetIsBelow(const gentity_t *bot, const BotLibAdapterRouteSteer &route) {
	if (bot == nullptr) {
		return false;
	}
	return route.moveTarget[2] < bot->s.origin.z - BOT_COMMAND_VERTICAL_INTENT_EPSILON;
}

void Bot_CommandPressJump(usercmd_t *cmd) {
	cmd->buttons |= BUTTON_JUMP;
	botFrameCommandStatus.movementStateJumpCommands++;
}

void Bot_CommandPressCrouch(usercmd_t *cmd) {
	cmd->buttons |= BUTTON_CROUCH;
	botFrameCommandStatus.movementStateCrouchCommands++;
}

void Bot_CommandRecordNaturalMovementState(int travelType) {
	botFrameCommandStatus.naturalMovementStateCommands++;
	switch (travelType) {
	case BOT_COMMAND_TRAVEL_CROUCH:
		botFrameCommandStatus.naturalMovementStateCrouchCommands++;
		break;
	case BOT_COMMAND_TRAVEL_SWIM:
		botFrameCommandStatus.naturalMovementStateSwimCommands++;
		break;
	case BOT_COMMAND_TRAVEL_WATER_JUMP:
		botFrameCommandStatus.naturalMovementStateWaterJumpCommands++;
		break;
	default:
		break;
	}
}

void Bot_CommandApplyMovementState(const gentity_t *bot, const BotLibAdapterRouteSteer &route, usercmd_t *cmd) {
	const int forcedTravelType = Bot_CommandSmokeForcedTravelType();
	const int travelType = forcedTravelType > 0 ? forcedTravelType : route.reachabilityTravelType;
	bool commandApplied = false;

	botFrameCommandStatus.movementStateAttempts++;
	botFrameCommandStatus.lastMovementStateTravelType = travelType;
	botFrameCommandStatus.lastMovementStateForcedTravelType = forcedTravelType;

	switch (travelType) {
	case BOT_COMMAND_TRAVEL_CROUCH:
		Bot_CommandPressCrouch(cmd);
		commandApplied = true;
		break;
	case BOT_COMMAND_TRAVEL_BARRIER_JUMP:
	case BOT_COMMAND_TRAVEL_JUMP:
		Bot_CommandPressJump(cmd);
		commandApplied = true;
		break;
	case BOT_COMMAND_TRAVEL_WATER_JUMP:
		Bot_CommandPressJump(cmd);
		botFrameCommandStatus.movementStateWaterJumpCommands++;
		commandApplied = true;
		break;
	case BOT_COMMAND_TRAVEL_LADDER:
		if (Bot_CommandRouteTargetIsBelow(bot, route)) {
			Bot_CommandPressCrouch(cmd);
		} else {
			Bot_CommandPressJump(cmd);
		}
		botFrameCommandStatus.movementStateLadderCommands++;
		commandApplied = true;
		break;
	case BOT_COMMAND_TRAVEL_SWIM:
		if (Bot_CommandRouteTargetIsBelow(bot, route)) {
			Bot_CommandPressCrouch(cmd);
		} else {
			Bot_CommandPressJump(cmd);
		}
		botFrameCommandStatus.movementStateSwimCommands++;
		commandApplied = true;
		break;
	case 0:
	case BOT_COMMAND_TRAVEL_WALK:
	case BOT_COMMAND_TRAVEL_WALK_OFF_LEDGE:
	case BOT_COMMAND_TRAVEL_ELEVATOR:
	case BOT_COMMAND_TRAVEL_ROCKET_JUMP:
		break;
	default:
		botFrameCommandStatus.movementStateUnsupported++;
		break;
	}

	if (commandApplied) {
		botFrameCommandStatus.movementStateCommands++;
		if (forcedTravelType <= 0) {
			Bot_CommandRecordNaturalMovementState(travelType);
		}
	}
	botFrameCommandStatus.lastMovementStateButtons = static_cast<int>(cmd->buttons);
}

void Bot_CommandApplyRecoveryMove(const gentity_t *bot, usercmd_t *cmd) {
	BotNavRecoveryMove recovery{};
	if (!BotNav_GetRecoveryMove(bot, &recovery)) {
		return;
	}

	if (recovery.wait) {
		cmd->forwardMove = 0.0f;
		cmd->sideMove = 0.0f;
		botFrameCommandStatus.interactionWaitCommandUses++;
		botFrameCommandStatus.lastInteractionCommandAction = recovery.interactionAction;
		botFrameCommandStatus.lastInteractionCommandEntity = recovery.interactionEntity;
	} else {
		cmd->forwardMove = BOT_COMMAND_STUCK_RECOVERY_FORWARD_MOVE;
		cmd->sideMove = BOT_COMMAND_STUCK_RECOVERY_SIDE_MOVE * static_cast<float>(recovery.sideSign);
	}

	if (recovery.use) {
		cmd->buttons |= BUTTON_USE;
		botFrameCommandStatus.interactionUseCommandUses++;
		botFrameCommandStatus.lastInteractionCommandAction = recovery.interactionAction;
		botFrameCommandStatus.lastInteractionCommandEntity = recovery.interactionEntity;
	}

	botFrameCommandStatus.recoveryCommandUses++;
	botFrameCommandStatus.lastRecoveryForwardMove = static_cast<int>(cmd->forwardMove);
	botFrameCommandStatus.lastRecoverySideMove = static_cast<int>(cmd->sideMove);
	botFrameCommandStatus.lastRecoveryFramesRemaining = recovery.framesRemaining;
}

bool Bot_CommandMovementStateForcedPass(int forcedTravelType, int expectedMinCommands) {
	if (expectedMinCommands <= 0 || forcedTravelType <= 0) {
		return true;
	}

	switch (forcedTravelType) {
	case BOT_COMMAND_TRAVEL_CROUCH:
		return botFrameCommandStatus.movementStateCrouchCommands >= expectedMinCommands;
	case BOT_COMMAND_TRAVEL_BARRIER_JUMP:
	case BOT_COMMAND_TRAVEL_JUMP:
		return botFrameCommandStatus.movementStateJumpCommands >= expectedMinCommands;
	case BOT_COMMAND_TRAVEL_WATER_JUMP:
		return botFrameCommandStatus.movementStateWaterJumpCommands >= expectedMinCommands;
	case BOT_COMMAND_TRAVEL_LADDER:
		return botFrameCommandStatus.movementStateLadderCommands >= expectedMinCommands;
	case BOT_COMMAND_TRAVEL_SWIM:
		return botFrameCommandStatus.movementStateSwimCommands >= expectedMinCommands;
	default:
		return false;
	}
}

bool Bot_CommandTravelTypeGoalUsesMovementState(int travelTypeGoal) {
	switch (travelTypeGoal) {
	case BOT_COMMAND_TRAVEL_CROUCH:
	case BOT_COMMAND_TRAVEL_BARRIER_JUMP:
	case BOT_COMMAND_TRAVEL_JUMP:
	case BOT_COMMAND_TRAVEL_WATER_JUMP:
	case BOT_COMMAND_TRAVEL_LADDER:
	case BOT_COMMAND_TRAVEL_SWIM:
		return true;
	default:
		return false;
	}
}

bool Bot_CommandTravelTypeGoalPass(
	const BotNavRouteStatus &routeStatus,
	int travelTypeGoal,
	int expectedMinCommands,
	bool expectBlocked) {
	if (expectedMinCommands <= 0 || travelTypeGoal <= 0) {
		if (!expectBlocked) {
			return true;
		}
		return travelTypeGoal > 0 &&
			routeStatus.travelTypeGoalRequests > 0 &&
			routeStatus.travelTypeGoalResolved == 0 &&
			routeStatus.travelTypeGoalAssignments == 0 &&
			botFrameCommandStatus.travelTypeGoalStartWarps == 0 &&
			botFrameCommandStatus.commands == 0 &&
			botFrameCommandStatus.routeCommands == 0 &&
			routeStatus.failures > 0;
	}
	if (botFrameCommandStatus.lastMovementStateForcedTravelType != 0) {
		return false;
	}
	if (routeStatus.travelTypeGoalAssignments <= 0 ||
		routeStatus.travelTypeGoalResolved <= 0 ||
		routeStatus.lastTravelTypeGoalType != travelTypeGoal ||
		routeStatus.lastTravelTypeGoalArea <= 0 ||
		routeStatus.lastReachabilityTravelType != travelTypeGoal) {
		return false;
	}
	if (!Bot_CommandTravelTypeGoalUsesMovementState(travelTypeGoal)) {
		return botFrameCommandStatus.routeCommands >= expectedMinCommands;
	}
	return Bot_CommandMovementStateForcedPass(travelTypeGoal, expectedMinCommands);
}

void Bot_CommandSampleActionDecision(gentity_t *bot) {
	BotActionContext actionContext = BotActions_BuildContext(bot);
	(void)BotActions_Decide(actionContext);
}

} // namespace

/*
================
BotBrain_BeginFrame
================
*/
void BotBrain_BeginFrame( gentity_t * bot ) {
	(void)bot;
	if (!Bot_RuntimeEnabled() || !Bot_RuntimeAasLoaded()) {
		return;
	}

}

/*
================
BotBrain_EndFrame
================
*/
void BotBrain_EndFrame( gentity_t * bot ) {
	(void)bot;
	if (!Bot_RuntimeEnabled() || !Bot_RuntimeAasLoaded()) {
		return;
	}

}

/*
================
BotBrain_BuildFrameCommand
================
*/
bool BotBrain_BuildFrameCommand( gentity_t * bot, usercmd_t * cmd ) {
	botFrameCommandStatus.frames++;

	if (!bot || !cmd || !bot->client) {
		botFrameCommandStatus.skippedInvalid++;
		return false;
	}

	if ((bot->svFlags & SVF_BOT) == 0 && !bot->client->sess.is_a_bot) {
		botFrameCommandStatus.skippedNotBot++;
		return false;
	}

	if (!Bot_RuntimeEnabled() || !Bot_RuntimeAasLoaded()) {
		botFrameCommandStatus.skippedRuntime++;
		return false;
	}

	if (!ClientIsPlaying(bot->client) || bot->client->eliminated || bot->deadFlag) {
		botFrameCommandStatus.skippedInactive++;
		return false;
	}

	Bot_CommandSampleActionDecision(bot);

	BotLibAdapterRouteSteer route{};
	BotNavRouteRequest routeRequest{};
	Bot_CommandBuildRouteRequest(&routeRequest);
	Bot_CommandMaybeWarpToTravelTypeGoalStart(bot, routeRequest);
	if (!BotNav_GetRouteSteer(bot, &routeRequest, &route)) {
		return false;
	}

	*cmd = {};
	cmd->msec = Bot_CommandMsec();
	cmd->angles = Bot_CommandAnglesToTarget(bot, route);
	cmd->forwardMove = 180.0f;
	cmd->serverFrame = gi.ServerFrame();
	Bot_CommandApplyMovementState(bot, route, cmd);
	Bot_CommandApplyRecoveryMove(bot, cmd);

	botFrameCommandStatus.commands++;
	botFrameCommandStatus.routeCommands++;
	return true;
}

/*
================
BotBrain_PrintFrameCommandStatus
================
*/
void BotBrain_PrintFrameCommandStatus( int expectedMinFrames, int expectedMinCommands ) {
	const BotNavRouteStatus &routeStatus = BotNav_GetRouteStatus();
	const BotActionStatus &actionStatus = BotActions_GetStatus();
	const BotItemStatus &itemStatus = BotItems_GetStatus();
	const BotCombatStatus &combatStatus = BotCombat_GetStatus();
	const int travelTypeGoal = Bot_CommandTravelTypeGoal();
	const bool reservationPass = Bot_CommandSmokeSoak() ||
		expectedMinCommands <= 1 ||
		Bot_CommandPositionGoalEnabled() ||
		travelTypeGoal > 0 ||
		(routeStatus.itemGoalReservationSkips > 0 &&
		 routeStatus.itemGoalPeakActiveReservations >= expectedMinCommands);
	const int forcedTravelType = Bot_CommandSmokeForcedTravelType();
	const bool movementStatePass = Bot_CommandMovementStateForcedPass(forcedTravelType, expectedMinCommands);
	const bool positionGoalPass = !Bot_CommandPositionGoalEnabled() ||
		(routeStatus.positionGoalAssignments >= expectedMinCommands &&
		 routeStatus.lastPositionGoalArea > 0);
	const bool expectTravelTypeGoalBlocked = Bot_CommandTravelTypeGoalExpectBlocked();
	const bool travelTypeGoalPass = Bot_CommandTravelTypeGoalPass(
		routeStatus,
		travelTypeGoal,
		expectedMinCommands,
		expectTravelTypeGoalBlocked);
	const bool commandCountPass = expectTravelTypeGoalBlocked ?
		botFrameCommandStatus.commands == 0 :
		botFrameCommandStatus.commands >= expectedMinCommands;
	const bool routeCommandCountPass = expectTravelTypeGoalBlocked ?
		botFrameCommandStatus.routeCommands == 0 :
		botFrameCommandStatus.routeCommands >= expectedMinCommands;
	const bool routeFailurePass = expectTravelTypeGoalBlocked ?
		routeStatus.failures > 0 :
		(expectedMinCommands <= 0 || routeStatus.failures == 0);
	const int pass = (botFrameCommandStatus.frames >= expectedMinFrames &&
		commandCountPass &&
		routeCommandCountPass &&
		routeFailurePass &&
		movementStatePass &&
		positionGoalPass &&
		travelTypeGoalPass &&
		reservationPass) ? 1 : 0;

	base_import.Com_Print(
		G_Fmt("q3a_bot_frame_command_status frames={} commands={} "
			  "route_requests={} route_queries={} route_refreshes={} "
			  "route_reuses={} route_commands={} route_failures={} "
			  "route_invalid_slots={} route_cadence_refreshes={} "
			  "route_target_refreshes={} route_drift_refreshes={} "
			  "route_preferred_goal_refreshes={} "
			  "stuck_checks={} stuck_stalls={} stuck_detections={} "
			  "stuck_repath_refreshes={} stuck_recovery_activations={} "
			  "stuck_recovery_frames={} "
			  "route_goal_requests={} route_goal_assignments={} "
			  "route_goal_cache_reuses={} route_goal_clears={} "
			  "route_goal_fallbacks={} failed_goal_events={} "
			  "item_goal_scans={} "
			  "item_goal_candidates={} item_goal_assignments={} "
			  "item_goal_reuses={} item_goal_clears={} "
			  "item_goal_reservation_skips={} "
			  "item_goal_active_reservations={} "
			  "item_goal_peak_active_reservations={} "
			  "item_goal_blacklist_activations={} "
			  "item_goal_blacklist_skips={} "
			  "item_goal_blacklist_active={} "
			  "position_goal_requests={} position_goal_resolved={} "
			  "position_goal_assignments={} position_goal_cache_reuses={} "
			  "position_goal_clears={} "
			  "travel_type_goal_requests={} travel_type_goal_resolved={} "
			  "travel_type_goal_assignments={} travel_type_goal_cache_reuses={} "
			  "travel_type_goal_clears={} "
			  "travel_type_goal_start_warps={} "
			  "travel_type_goal_expect_blocked={} "
			  "route_debug_frames={} "
			  "route_debug_routes={} route_debug_goals={} "
			  "route_debug_missing_frames={} route_debug_lines={} "
			  "route_debug_crosses={} route_debug_arrows={} "
			  "route_debug_labels={} route_debug_polyline_points={} "
			  "route_debug_polyline_segments={} "
			  "route_debug_filtered_slots={} route_debug_filter_miss_frames={} "
			  "last_route_client={} last_route_debug_client={} "
			  "last_debug_filter_client={} "
			  "last_persistent_goal_area={} last_item_goal_entity={} "
			  "last_item_goal_area={} last_item_goal_item={} "
			  "last_item_goal_score={} last_item_goal_reserved_entity={} "
			  "last_item_goal_reserved_by_client={} "
			  "last_item_goal_blacklisted_entity={} "
			  "last_item_goal_blacklisted_by_client={} "
			  "last_item_goal_blacklist_frames_remaining={} "
			  "last_position_goal_area={} last_position_goal_x={} "
			  "last_position_goal_y={} last_position_goal_z={} "
			  "last_travel_type_goal_type={} last_travel_type_goal_area={} "
			  "last_travel_type_goal_start_type={} "
			  "last_travel_type_goal_start_area={} "
			  "last_travel_type_goal_start_goal_area={} "
			  "last_goal_clear_reason={} "
			  "last_failed_goal_reason={} last_failed_goal_client={} "
			  "last_failed_goal_area={} last_failed_goal_entity={} "
			  "last_failed_goal_item={} "
			  "last_stuck_reason={} last_stuck_client={} "
			  "last_stuck_frames={} last_stuck_distance_sq={} "
			  "last_stuck_progress_delta={} "
			  "last_stuck_recovery_client={} last_stuck_recovery_side={} "
			  "last_stuck_recovery_frames_remaining={} "
			  "last_current_area={} last_start_area={} last_goal_area={} "
			  "last_route_end_area={} last_route_point_count={} last_travel_time={} "
			  "last_reachability={} last_reachability_type={} "
			  "last_reachability_flags={} last_reachability_end_area={} "
			  "last_stop_event={} "
			  "skipped_invalid={} skipped_not_bot={} skipped_runtime={} "
			  "skipped_inactive={} expected_min_frames={} "
			  "expected_min_commands={} lookahead_attempts={} "
			  "lookahead_uses={} last_lookahead_index={} "
			  "last_lookahead_point_count={} velocity_lead_attempts={} "
			  "velocity_lead_uses={} last_velocity_lead_speed_sq={} "
			  "last_velocity_lead_offset_sq={} movement_state_attempts={} "
			  "movement_state_commands={} movement_state_jump_commands={} "
			  "movement_state_crouch_commands={} movement_state_swim_commands={} "
			  "movement_state_ladder_commands={} movement_state_unsupported={} "
			  "last_movement_state_travel_type={} "
			  "last_movement_state_forced_travel_type={} "
			  "last_movement_state_buttons={} recovery_command_uses={} "
			  "last_recovery_forward_move={} last_recovery_side_move={} "
			  "last_recovery_frames_remaining={} pass={}\n",
			  botFrameCommandStatus.frames,
			  botFrameCommandStatus.commands,
			  routeStatus.requests,
			  routeStatus.queries,
			  routeStatus.refreshes,
			  routeStatus.reuses,
			  botFrameCommandStatus.routeCommands,
			  routeStatus.failures,
			  routeStatus.invalidSlots,
			  routeStatus.cadenceRefreshes,
			  routeStatus.targetRefreshes,
			  routeStatus.driftRefreshes,
			  routeStatus.preferredGoalRefreshes,
			  routeStatus.stuckChecks,
			  routeStatus.stuckStalls,
			  routeStatus.stuckDetections,
			  routeStatus.stuckRepathRefreshes,
			  routeStatus.stuckRecoveryActivations,
			  routeStatus.stuckRecoveryFrames,
			  routeStatus.persistentGoalRequests,
			  routeStatus.persistentGoalAssignments,
			  routeStatus.persistentGoalCacheReuses,
			  routeStatus.persistentGoalClears,
			  routeStatus.persistentGoalFallbacks,
			  routeStatus.failedGoalEvents,
			  routeStatus.itemGoalScans,
			  routeStatus.itemGoalCandidates,
			  routeStatus.itemGoalAssignments,
			  routeStatus.itemGoalReuses,
			  routeStatus.itemGoalClears,
			  routeStatus.itemGoalReservationSkips,
			  routeStatus.itemGoalActiveReservations,
			  routeStatus.itemGoalPeakActiveReservations,
			  routeStatus.itemGoalBlacklistActivations,
			  routeStatus.itemGoalBlacklistSkips,
			  routeStatus.itemGoalBlacklistActive,
			  routeStatus.positionGoalRequests,
			  routeStatus.positionGoalResolved,
			  routeStatus.positionGoalAssignments,
			  routeStatus.positionGoalCacheReuses,
			  routeStatus.positionGoalClears,
			  routeStatus.travelTypeGoalRequests,
			  routeStatus.travelTypeGoalResolved,
			  routeStatus.travelTypeGoalAssignments,
			  routeStatus.travelTypeGoalCacheReuses,
			  routeStatus.travelTypeGoalClears,
			  botFrameCommandStatus.travelTypeGoalStartWarps,
			  expectTravelTypeGoalBlocked ? 1 : 0,
			  routeStatus.debugOverlayFrames,
			  routeStatus.debugOverlayRoutes,
			  routeStatus.debugOverlayGoals,
			  routeStatus.debugOverlayMissingFrames,
			  routeStatus.debugOverlayLines,
			  routeStatus.debugOverlayCrosses,
			  routeStatus.debugOverlayArrows,
			  routeStatus.debugOverlayLabels,
			  routeStatus.debugOverlayPolylinePoints,
			  routeStatus.debugOverlayPolylineSegments,
			  routeStatus.debugOverlayFilteredSlots,
			  routeStatus.debugOverlayFilterMissFrames,
			  routeStatus.lastClient,
			  routeStatus.lastDebugClient,
			  routeStatus.lastDebugFilterClient,
			  routeStatus.lastPersistentGoalArea,
			  routeStatus.lastItemGoalEntity,
			  routeStatus.lastItemGoalArea,
			  routeStatus.lastItemGoalItem,
			  routeStatus.lastItemGoalScore,
			  routeStatus.lastItemGoalReservedEntity,
			  routeStatus.lastItemGoalReservedByClient,
			  routeStatus.lastItemGoalBlacklistedEntity,
			  routeStatus.lastItemGoalBlacklistedByClient,
			  routeStatus.lastItemGoalBlacklistFramesRemaining,
			  routeStatus.lastPositionGoalArea,
			  routeStatus.lastPositionGoalX,
			  routeStatus.lastPositionGoalY,
			  routeStatus.lastPositionGoalZ,
			  routeStatus.lastTravelTypeGoalType,
			  routeStatus.lastTravelTypeGoalArea,
			  botFrameCommandStatus.lastTravelTypeGoalStartType,
			  botFrameCommandStatus.lastTravelTypeGoalStartArea,
			  botFrameCommandStatus.lastTravelTypeGoalStartGoalArea,
			  routeStatus.lastGoalClearReason,
			  routeStatus.lastFailedGoalReason,
			  routeStatus.lastFailedGoalClient,
			  routeStatus.lastFailedGoalArea,
			  routeStatus.lastFailedGoalEntity,
			  routeStatus.lastFailedGoalItem,
			  routeStatus.lastStuckReason,
			  routeStatus.lastStuckClient,
			  routeStatus.lastStuckFrames,
			  routeStatus.lastStuckDistanceSq,
			  routeStatus.lastStuckProgressDelta,
			  routeStatus.lastStuckRecoveryClient,
			  routeStatus.lastStuckRecoverySide,
			  routeStatus.lastStuckRecoveryFramesRemaining,
			  routeStatus.lastCurrentArea,
			  routeStatus.lastStartArea,
			  routeStatus.lastGoalArea,
			  routeStatus.lastRouteEndArea,
			  routeStatus.lastRoutePointCount,
			  routeStatus.lastTravelTime,
			  routeStatus.lastReachability,
			  routeStatus.lastReachabilityTravelType,
			  routeStatus.lastReachabilityTravelFlags,
			  routeStatus.lastReachabilityEndArea,
			  routeStatus.lastStopEvent,
			  botFrameCommandStatus.skippedInvalid,
			  botFrameCommandStatus.skippedNotBot,
			  botFrameCommandStatus.skippedRuntime,
			  botFrameCommandStatus.skippedInactive,
			  expectedMinFrames,
			  expectedMinCommands,
			  botFrameCommandStatus.lookAheadAttempts,
			  botFrameCommandStatus.lookAheadUses,
			  botFrameCommandStatus.lastLookAheadIndex,
			  botFrameCommandStatus.lastLookAheadPointCount,
			  botFrameCommandStatus.velocityLeadAttempts,
			  botFrameCommandStatus.velocityLeadUses,
			  botFrameCommandStatus.lastVelocityLeadSpeedSquared,
			  botFrameCommandStatus.lastVelocityLeadOffsetSquared,
			  botFrameCommandStatus.movementStateAttempts,
			  botFrameCommandStatus.movementStateCommands,
			  botFrameCommandStatus.movementStateJumpCommands,
			  botFrameCommandStatus.movementStateCrouchCommands,
			  botFrameCommandStatus.movementStateSwimCommands,
			  botFrameCommandStatus.movementStateLadderCommands,
			  botFrameCommandStatus.movementStateUnsupported,
			  botFrameCommandStatus.lastMovementStateTravelType,
			  botFrameCommandStatus.lastMovementStateForcedTravelType,
			  botFrameCommandStatus.lastMovementStateButtons,
			  botFrameCommandStatus.recoveryCommandUses,
			  botFrameCommandStatus.lastRecoveryForwardMove,
			  botFrameCommandStatus.lastRecoverySideMove,
			  botFrameCommandStatus.lastRecoveryFramesRemaining,
			  pass)
			.data());

	base_import.Com_Print(
		G_Fmt("q3a_bot_nav_policy_status "
			  "travel_type_goal_support_checks={} travel_type_goal_supported={} "
			  "travel_type_goal_unsupported={} "
			  "last_travel_type_goal_support_type={} "
			  "last_travel_type_goal_support_area={} "
			  "last_travel_type_goal_support_goal_area={} "
			  "nav_interaction_checks={} nav_interaction_candidates={} "
			  "nav_interaction_activations={} "
			  "nav_interaction_stuck_activations={} "
			  "nav_interaction_elevator_activations={} "
			  "nav_interaction_wait_frames={} nav_interaction_use_frames={} "
			  "nav_interaction_misses={} "
			  "last_nav_interaction_action={} last_nav_interaction_kind={} "
			  "last_nav_interaction_entity={} "
			  "last_nav_interaction_distance_sq={} "
			  "last_nav_interaction_travel_type={} "
			  "last_nav_interaction_move_state={} "
			  "last_nav_interaction_frames_remaining={} "
			  "movement_state_waterjump_commands={} "
			  "natural_movement_state_commands={} "
			  "natural_movement_state_crouch_commands={} "
			  "natural_movement_state_swim_commands={} "
			  "natural_movement_state_waterjump_commands={} "
			  "interaction_wait_command_uses={} interaction_use_command_uses={} "
			  "last_interaction_command_action={} "
			  "last_interaction_command_entity={}\n",
			  routeStatus.travelTypeGoalSupportChecks,
			  routeStatus.travelTypeGoalSupported,
			  routeStatus.travelTypeGoalUnsupported,
			  routeStatus.lastTravelTypeGoalSupportType,
			  routeStatus.lastTravelTypeGoalSupportArea,
			  routeStatus.lastTravelTypeGoalSupportGoalArea,
			  routeStatus.interactionChecks,
			  routeStatus.interactionCandidates,
			  routeStatus.interactionActivations,
			  routeStatus.interactionStuckActivations,
			  routeStatus.interactionElevatorActivations,
			  routeStatus.interactionWaitFrames,
			  routeStatus.interactionUseFrames,
			  routeStatus.interactionMisses,
			  routeStatus.lastInteractionAction,
			  routeStatus.lastInteractionKind,
			  routeStatus.lastInteractionEntity,
			  routeStatus.lastInteractionDistanceSq,
			  routeStatus.lastInteractionTravelType,
			  routeStatus.lastInteractionMoveState,
			  routeStatus.lastInteractionFramesRemaining,
			  botFrameCommandStatus.movementStateWaterJumpCommands,
			  botFrameCommandStatus.naturalMovementStateCommands,
			  botFrameCommandStatus.naturalMovementStateCrouchCommands,
			  botFrameCommandStatus.naturalMovementStateSwimCommands,
			  botFrameCommandStatus.naturalMovementStateWaterJumpCommands,
			  botFrameCommandStatus.interactionWaitCommandUses,
			  botFrameCommandStatus.interactionUseCommandUses,
			  botFrameCommandStatus.lastInteractionCommandAction,
			  botFrameCommandStatus.lastInteractionCommandEntity)
			.data());

	base_import.Com_Print(
		G_Fmt("q3a_bot_nav_natural_support_status "
			  "natural_movement_support_aas_loaded={} "
			  "natural_movement_support_checks={} "
			  "natural_movement_supported={} natural_movement_unsupported={} "
			  "natural_movement_unsupported_mask={} "
			  "natural_crouch_supported={} natural_crouch_unsupported={} "
			  "natural_crouch_reason={} natural_crouch_area={} "
			  "natural_crouch_goal_area={} "
			  "natural_crouch_origin_x={} natural_crouch_origin_y={} "
			  "natural_crouch_origin_z={} "
			  "natural_swim_supported={} natural_swim_unsupported={} "
			  "natural_swim_reason={} natural_swim_area={} "
			  "natural_swim_goal_area={} "
			  "natural_swim_origin_x={} natural_swim_origin_y={} "
			  "natural_swim_origin_z={} "
			  "natural_waterjump_supported={} natural_waterjump_unsupported={} "
			  "natural_waterjump_reason={} natural_waterjump_area={} "
			  "natural_waterjump_goal_area={} "
			  "natural_waterjump_origin_x={} natural_waterjump_origin_y={} "
			  "natural_waterjump_origin_z={}\n",
			  routeStatus.naturalMovementSupportAasLoaded,
			  routeStatus.naturalMovementSupportChecks,
			  routeStatus.naturalMovementSupported,
			  routeStatus.naturalMovementUnsupported,
			  routeStatus.naturalMovementUnsupportedMask,
			  routeStatus.naturalMovementCrouchSupported,
			  routeStatus.naturalMovementCrouchUnsupported,
			  routeStatus.naturalMovementCrouchReason,
			  routeStatus.naturalMovementCrouchArea,
			  routeStatus.naturalMovementCrouchGoalArea,
			  routeStatus.naturalMovementCrouchOriginX,
			  routeStatus.naturalMovementCrouchOriginY,
			  routeStatus.naturalMovementCrouchOriginZ,
			  routeStatus.naturalMovementSwimSupported,
			  routeStatus.naturalMovementSwimUnsupported,
			  routeStatus.naturalMovementSwimReason,
			  routeStatus.naturalMovementSwimArea,
			  routeStatus.naturalMovementSwimGoalArea,
			  routeStatus.naturalMovementSwimOriginX,
			  routeStatus.naturalMovementSwimOriginY,
			  routeStatus.naturalMovementSwimOriginZ,
			  routeStatus.naturalMovementWaterJumpSupported,
			  routeStatus.naturalMovementWaterJumpUnsupported,
			  routeStatus.naturalMovementWaterJumpReason,
			  routeStatus.naturalMovementWaterJumpArea,
			  routeStatus.naturalMovementWaterJumpGoalArea,
			  routeStatus.naturalMovementWaterJumpOriginX,
			  routeStatus.naturalMovementWaterJumpOriginY,
			  routeStatus.naturalMovementWaterJumpOriginZ)
			.data());

	base_import.Com_Print(
		G_Fmt("q3a_bot_nav_interaction_context_status "
			  "interaction_world_entities={} interaction_world_doors={} "
			  "interaction_world_buttons={} interaction_world_platforms={} "
			  "interaction_world_trains={} interaction_world_waters={} "
			  "interaction_world_triggers={} interaction_world_movers={} "
			  "interaction_world_use_entities={} "
			  "interaction_world_touch_entities={} "
			  "last_nav_interaction_entity={} "
			  "last_nav_interaction_spawn_count={} "
			  "last_nav_interaction_origin_x={} "
			  "last_nav_interaction_origin_y={} "
			  "last_nav_interaction_origin_z={} "
			  "last_nav_interaction_bounds_min_x={} "
			  "last_nav_interaction_bounds_min_y={} "
			  "last_nav_interaction_bounds_min_z={} "
			  "last_nav_interaction_bounds_max_x={} "
			  "last_nav_interaction_bounds_max_y={} "
			  "last_nav_interaction_bounds_max_z={} "
			  "last_nav_interaction_use={} last_nav_interaction_touch={} "
			  "last_nav_interaction_solid={} "
			  "last_nav_interaction_movetype={}\n",
			  routeStatus.interactionWorldEntities,
			  routeStatus.interactionWorldDoors,
			  routeStatus.interactionWorldButtons,
			  routeStatus.interactionWorldPlatforms,
			  routeStatus.interactionWorldTrains,
			  routeStatus.interactionWorldWaters,
			  routeStatus.interactionWorldTriggers,
			  routeStatus.interactionWorldMovers,
			  routeStatus.interactionWorldUseEntities,
			  routeStatus.interactionWorldTouchEntities,
			  routeStatus.lastInteractionEntity,
			  routeStatus.lastInteractionSpawnCount,
			  routeStatus.lastInteractionOriginX,
			  routeStatus.lastInteractionOriginY,
			  routeStatus.lastInteractionOriginZ,
			  routeStatus.lastInteractionBoundsMinX,
			  routeStatus.lastInteractionBoundsMinY,
			  routeStatus.lastInteractionBoundsMinZ,
			  routeStatus.lastInteractionBoundsMaxX,
			  routeStatus.lastInteractionBoundsMaxY,
			  routeStatus.lastInteractionBoundsMaxZ,
			  routeStatus.lastInteractionUse,
			  routeStatus.lastInteractionTouch,
			  routeStatus.lastInteractionSolid,
			  routeStatus.lastInteractionMoveType)
			.data());

	base_import.Com_Print(
		G_Fmt("q3a_bot_action_status "
			  "action_evaluations={} action_invalid_contexts={} "
			  "action_dead_contexts={} action_item_evaluations={} "
			  "action_combat_evaluations={} action_move_to_item_decisions={} "
			  "action_weapon_switch_decisions={} action_attack_decisions={} "
			  "action_use_world_decisions={} action_use_inventory_decisions={} "
			  "action_noop_decisions={} action_applied_cmds={} "
			  "action_applied_attack_buttons={} action_applied_use_buttons={} "
			  "action_pending_weapon_switches={} action_pending_inventory_uses={} "
			  "action_last_client={} action_last_intent={} "
			  "action_last_priority={} action_last_item={} "
			  "action_last_entity={} action_last_weapon_item={} "
			  "action_last_intent_name={} "
			  "item_evaluations={} item_invalid_candidates={} "
			  "item_reserved_deferrals={} item_seek_decisions={} "
			  "item_low_health_boosts={} item_low_armor_boosts={} "
			  "item_last_item={} item_last_entity={} item_last_priority={} "
			  "combat_evaluations={} combat_no_enemy={} "
			  "combat_blocked_sight={} combat_weapon_switch_decisions={} "
			  "combat_fire_decisions={} combat_withheld_fire={} "
			  "combat_last_weapon_item={} combat_last_priority={} "
			  "combat_last_enemy_distance_sq={}\n",
			  actionStatus.evaluations,
			  actionStatus.invalidContexts,
			  actionStatus.deadContexts,
			  actionStatus.itemEvaluations,
			  actionStatus.combatEvaluations,
			  actionStatus.moveToItemDecisions,
			  actionStatus.weaponSwitchDecisions,
			  actionStatus.attackDecisions,
			  actionStatus.useWorldDecisions,
			  actionStatus.useInventoryDecisions,
			  actionStatus.noopDecisions,
			  actionStatus.appliedCommands,
			  actionStatus.appliedAttackButtons,
			  actionStatus.appliedUseButtons,
			  actionStatus.pendingWeaponSwitches,
			  actionStatus.pendingInventoryUses,
			  actionStatus.lastClientIndex,
			  static_cast<int>(actionStatus.lastIntent),
			  actionStatus.lastPriority,
			  actionStatus.lastItem,
			  actionStatus.lastEntity,
			  actionStatus.lastWeaponItem,
			  BotActions_IntentName(actionStatus.lastIntent),
			  itemStatus.evaluations,
			  itemStatus.invalidCandidates,
			  itemStatus.reservedDeferrals,
			  itemStatus.seekDecisions,
			  itemStatus.lowHealthBoosts,
			  itemStatus.lowArmorBoosts,
			  itemStatus.lastItem,
			  itemStatus.lastEntity,
			  itemStatus.lastPriority,
			  combatStatus.evaluations,
			  combatStatus.noEnemy,
			  combatStatus.blockedSight,
			  combatStatus.weaponSwitchDecisions,
			  combatStatus.fireDecisions,
			  combatStatus.withheldFire,
			  combatStatus.lastWeaponItem,
			  combatStatus.lastPriority,
			  combatStatus.lastEnemyDistanceSquared)
			.data());
}
