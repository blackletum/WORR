/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#pragma once

#define BOT_MYMAP_STATUS_API_V1 "BOT_MYMAP_STATUS_API_V1"

typedef struct {
    int api_version;
    void (*PrintStatus)(int expected_bots, int expected_play_queue,
                        int expected_mymap_queue,
                        int expected_last_queue_success,
                        int expected_last_consume_success);
    int (*TryQueueFirstBotMyMap)(const char *map_name);
    int (*ConsumeQueuedMap)(void);
    void (*ResetStatus)(void);
    void (*ClearQueues)(void);
} bot_mymap_status_api_v1_t;
