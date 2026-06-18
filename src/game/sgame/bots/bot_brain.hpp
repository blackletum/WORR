// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#pragma once

struct gentity_t;
struct usercmd_t;

void BotBrain_BeginFrame( gentity_t * bot );
void BotBrain_EndFrame( gentity_t * bot );
bool BotBrain_BuildFrameCommand( gentity_t * bot, usercmd_t * cmd );
void BotBrain_PrintFrameCommandStatus( int expectedMinFrames, int expectedMinCommands );
