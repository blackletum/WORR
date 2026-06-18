/*Copyright (c) 2024 ZeniMax Media Inc.
Licensed under the GNU General Public License 2.0.

bot_think.cpp implementation.*/

#include "../g_local.hpp"
#include "bot_brain.hpp"
#include "bot_think.hpp"

/*
================
Bot_BeginFrame
================
*/
void Bot_BeginFrame( gentity_t * bot ) {
	BotBrain_BeginFrame( bot );
}

/*
================
Bot_EndFrame
================
*/
void Bot_EndFrame( gentity_t * bot ) {
	BotBrain_EndFrame( bot );
}

/*
================
Bot_BuildFrameCommand
================
*/
bool Bot_BuildFrameCommand( gentity_t * bot, usercmd_t * cmd ) {
	return BotBrain_BuildFrameCommand( bot, cmd );
}

/*
================
Bot_FrameCommandPrintStatus
================
*/
void Bot_FrameCommandPrintStatus( int expectedMinFrames, int expectedMinCommands ) {
	BotBrain_PrintFrameCommandStatus( expectedMinFrames, expectedMinCommands );
}
