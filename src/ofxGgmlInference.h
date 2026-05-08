#pragma once

/// Clean inference layer for the rebuilt ofxGgml API.
///
/// This header builds on ofxGgmlFoundation.h and adds model/session concepts
/// without pulling in the legacy assistant, modality, or workflow surfaces.

#include "ofxGgmlFoundation.h"
#include "foundation/ofxGgmlConversation.h"
#include "foundation/ofxGgmlLlamaServerBackend.h"
#include "foundation/ofxGgmlModelCatalog.h"
#include "foundation/ofxGgmlTextTasks.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct ofxGgmlSessionSettings {
	ofxGgmlRuntimeSettings runtime;
	ofxGgmlModelCatalog models;
	std::string defaultTextModelId;
};

class ofxGgmlSession {
public:
	bool setup(const ofxGgmlSessionSettings & settings = {}) {
		settings_ = settings;
		models_ = settings.models;
		return app_.setup(settings.runtime);
	}

	void close() {
		app_.close();
		models_.clear();
		tasks_ = ofxGgmlTextTaskQueue();
	}

	bool isReady() const {
		return app_.isReady();
	}

	bool useTextBackend(std::shared_ptr<ofxGgmlTextBackend> backend) {
		return app_.text().setup(std::move(backend));
	}

	bool useEchoBackend() {
		return useTextBackend(std::make_shared<ofxGgmlEchoTextBackend>());
	}

	bool useLlamaServer(
		const ofxGgmlLlamaServerSettings & settings,
		std::shared_ptr<ofxGgmlHttpTransport> transport = std::make_shared<ofxGgmlUnavailableHttpTransport>()) {
		return useTextBackend(std::make_shared<ofxGgmlLlamaServerBackend>(settings, std::move(transport)));
	}

	void addModel(const ofxGgmlModelSpec & model) {
		models_.add(model);
	}

	std::optional<ofxGgmlModelSpec> findModel(const std::string & id) const {
		return models_.find(id);
	}

	const ofxGgmlModelCatalog & models() const {
		return models_;
	}

	ofxGgmlTextResponse generate(const std::string & prompt) {
		return app_.generate(prompt);
	}

	ofxGgmlTextResponse generate(const ofxGgmlTextRequest & request) {
		return app_.text().generate(request);
	}

	ofxGgmlTextResponse chat(const std::vector<ofxGgmlTextMessage> & messages) {
		return app_.text().chat(messages);
	}

	ofxGgmlTextResponse chat(ofxGgmlConversation & conversation, const std::string & userMessage) {
		conversation.addUserMessage(userMessage);
		auto response = chat(conversation.messages());
		if (response.ok) {
			conversation.addAssistantMessage(response.text);
		}
		return response;
	}

	uint64_t submit(const std::string & prompt) {
		return tasks_.submit(prompt);
	}

	uint64_t submit(const ofxGgmlTextRequest & request) {
		return tasks_.submit(request);
	}

	bool update(std::size_t maxTasks = 1) {
		return tasks_.update(app_.text(), maxTasks);
	}

	std::optional<ofxGgmlTextTaskResult> popCompleted() {
		return tasks_.popCompleted();
	}

	const ofxGgmlTextTaskQueue & tasks() const {
		return tasks_;
	}

	ofxGgmlApp & app() {
		return app_;
	}

	const ofxGgmlApp & app() const {
		return app_;
	}

private:
	ofxGgmlSessionSettings settings_;
	ofxGgmlModelCatalog models_;
	ofxGgmlApp app_;
	ofxGgmlTextTaskQueue tasks_;
};
