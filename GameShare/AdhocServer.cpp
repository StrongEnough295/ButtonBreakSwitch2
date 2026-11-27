// Copyright (c) 2012- PPSSPP Project.
// Licensed under GPLv2 or later. See LICENSE.TXT.
#include "Switch/GameShare/AdhocServer.h"

#include <cstdio>
#include <memory>
#include <system_error>

#include "Common/Net/SocketCompat.h"
#include "Common/Thread/ThreadUtil.h"
#include "Core/HLE/GameShareAdhoc.h"
#include "Core/HLE/proAdhocServer.h"
#include "ext/aemu_postoffice/server_cpp/server.h"

GameShareAdhocServer::~GameShareAdhocServer() {
	Stop();
}

bool GameShareAdhocServer::Start() {
	Stop();
	aemu_postoffice_server::config config;
	config.ip_addr = "127.0.0.1";
	config.port = GAMESHARE_ADHOC_RELAY_PORT;
	config.strict_mode = false;
	config.enable_enet = false;
	config.num_threads = 0;
	auto server = std::make_unique<aemu_postoffice_server::Server>(config);
	if (server->pump() == aemu_postoffice_server::ServerPumpStatus::LISTEN_SOCK_DEAD) {
		std::fprintf(stderr, "GameShare AdHoc: failed to open loopback relay on port %u\n", config.port);
		return false;
	}

	int listener = OpenGameShareAdhocControl(GAMESHARE_ADHOC_CTL_PORT);
	if (listener < 0) {
		std::fprintf(stderr, "GameShare AdHoc: failed to open loopback discovery on port %u\n", GAMESHARE_ADHOC_CTL_PORT);
		return false;
	}
	running_ = true;
	try {
		relay_ = std::thread([this, server = std::move(server)] {
			SetCurrentThreadName("GameShare AdHoc");
			while (running_) {
				if (server->pump() == aemu_postoffice_server::ServerPumpStatus::LISTEN_SOCK_DEAD) {
					running_ = false;
					break;
				}
				server->wait(10);
			}
		});
		control_ = std::thread([this, listener] {
			RunGameShareAdhocControl(listener, running_);
		});
	} catch (const std::system_error &error) {
		closesocket(listener);
		Stop();
		std::fprintf(stderr, "GameShare AdHoc: could not start server threads: %s\n", error.what());
		return false;
	}

	std::fprintf(stderr, "GameShare AdHoc: discovery and relay listening at 127.0.0.1:%u/%u\n", GAMESHARE_ADHOC_CTL_PORT, GAMESHARE_ADHOC_RELAY_PORT);
	return true;
}

void GameShareAdhocServer::Stop() {
	running_ = false;
	if (control_.joinable())
		control_.join();
	if (relay_.joinable())
		relay_.join();
}
