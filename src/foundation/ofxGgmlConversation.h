#pragma once

#include "ofxGgmlTextClient.h"

#include <cstddef>
#include <string>
#include <vector>

class ofxGgmlConversation {
public:
	void clear() {
		messages_.clear();
	}

	bool empty() const {
		return messages_.empty();
	}

	std::size_t size() const {
		return messages_.size();
	}

	void setSystemPrompt(const std::string & prompt) {
		if (!messages_.empty() && messages_.front().role == "system") {
			messages_.front().content = prompt;
			return;
		}
		messages_.insert(messages_.begin(), {"system", prompt});
	}

	void addUserMessage(const std::string & content) {
		messages_.push_back({"user", content});
	}

	void addAssistantMessage(const std::string & content) {
		messages_.push_back({"assistant", content});
	}

	const std::vector<ofxGgmlTextMessage> & messages() const {
		return messages_;
	}

private:
	std::vector<ofxGgmlTextMessage> messages_;
};
