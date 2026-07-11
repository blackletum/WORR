/*Copyright (c) 2024 The DarkMatter Project
Licensed under the GNU General Public License 2.0.

g_clients.cpp implementation.*/

#include "g_clients.hpp"

#include <algorithm>
#include <cstddef>

namespace {
	int ClampMaxClients(int maxClients) {
		const int upperLimit = static_cast<int>(MAX_CLIENTS_KEX);
		return std::clamp(maxClients, 0, upperLimit);
	}

	void* TagMallocChecked(std::size_t size) {
		if (size == 0)
			return nullptr;

		if (!gi.TagMalloc)
			gi.Com_Error("TagMalloc not initialized");

		return gi.TagMalloc(size, TAG_GAME);
	}

	void TagFreeChecked(void* ptr) {
		if (!ptr)
			return;

		if (gi.TagFree)
			gi.TagFree(ptr);
	}
}

void AllocateClientArray(int maxClients) {
	if (game.clients)
		FreeClientArray();

	game.maxClients = ClampMaxClients(maxClients);

	if (game.maxClients == 0) {
		game.clients = nullptr;
		globals.numEntities = 1;
		return;
	}

	game.clients = static_cast<gclient_t*>(TagMallocChecked(sizeof(gclient_t) * game.maxClients));
	ConstructClients(game.clients, game.maxClients);

	globals.numEntities = game.maxClients + 1;

	// [KEX]: Ensure client pointers are linked immediately to prevent engine crashes
	// if SV_CalcPings runs before a client is fully connected.
	if (g_entities) {
		for (int i = 0; i < static_cast<int>(game.maxClients); i++) {
			g_entities[i + 1].client = &game.clients[i];
		}
	}
}

void FreeClientArray() {
	static gclient_t dummyClient;
	dummyClient = gclient_t{};

	// [KEX]: Unlink client pointers
	if (g_entities && game.clients) {
		for (int i = 0; i < static_cast<int>(game.maxClients); i++) {
			g_entities[i + 1].client = &dummyClient;
		}
	}

	if (game.clients)
		DestroyClients(game.clients, game.maxClients);

	TagFreeChecked(game.clients);

	game.clients = nullptr;
	game.maxClients = 0;
	globals.numEntities = 1;
}

void ReplaceClientArray(int maxClients) {
	FreeClientArray();
	AllocateClientArray(maxClients);
}
