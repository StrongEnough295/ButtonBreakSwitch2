// Copyright (c) 2012- PPSSPP Project.
// Licensed under GPLv2 or later. See LICENSE.TXT.
#pragma once

#include <atomic>
#include <thread>

class GameShareAdhocServer {
public:
	~GameShareAdhocServer();
	bool Start();
	void Stop();
	bool Running() const { return running_; }

private:
	std::atomic<bool> running_{false};
	std::thread relay_;
	std::thread control_;
};
