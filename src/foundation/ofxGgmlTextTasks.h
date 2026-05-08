#pragma once

#include "ofxGgmlTextClient.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>

struct ofxGgmlTextTask {
	uint64_t id = 0;
	ofxGgmlTextRequest request;
};

struct ofxGgmlTextTaskResult {
	uint64_t id = 0;
	ofxGgmlTextResponse response;
};

class ofxGgmlTextTaskQueue {
public:
	uint64_t submit(const std::string & prompt) {
		ofxGgmlTextRequest request;
		request.prompt = prompt;
		return submit(request);
	}

	uint64_t submit(const ofxGgmlTextRequest & request) {
		const auto id = nextId_++;
		pending_.push_back({id, request});
		return id;
	}

	bool empty() const {
		return pending_.empty() && completed_.empty();
	}

	std::size_t pendingCount() const {
		return pending_.size();
	}

	std::size_t completedCount() const {
		return completed_.size();
	}

	bool update(ofxGgmlTextClient & client, std::size_t maxTasks = 1) {
		bool didWork = false;
		for (std::size_t i = 0; i < maxTasks && !pending_.empty(); ++i) {
			auto task = pending_.front();
			pending_.pop_front();
			completed_.push_back({task.id, client.generate(task.request)});
			didWork = true;
		}
		return didWork;
	}

	std::optional<ofxGgmlTextTaskResult> popCompleted() {
		if (completed_.empty()) {
			return std::nullopt;
		}
		auto result = completed_.front();
		completed_.pop_front();
		return result;
	}

	void clear() {
		pending_.clear();
		completed_.clear();
	}

private:
	uint64_t nextId_ = 1;
	std::deque<ofxGgmlTextTask> pending_;
	std::deque<ofxGgmlTextTaskResult> completed_;
};
