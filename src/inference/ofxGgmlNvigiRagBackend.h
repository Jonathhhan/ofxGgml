#pragma once

#include "ofxGgmlRAGPipeline.h"

#include <chrono>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#ifndef OFXGGML_ENABLE_NVIGI
#define OFXGGML_ENABLE_NVIGI 0
#endif

class ofxGgmlNvigiRagBackend {
public:
	using RetrieveFunction = std::function<ofxGgmlRAGRetrievalResult(
		const ofxGgmlRAGQuery &,
		const std::vector<ofxGgmlRAGDocument> &)>;
	using GenerateFunction = std::function<ofxGgmlRAGResult(
		const ofxGgmlRAGRequest &,
		const std::vector<ofxGgmlRAGDocument> &,
		std::function<bool(const std::string &)>)>;

	struct Options {
		std::string pluginId;
		std::string modelId;
		std::string embeddingModelId;
		std::string retrieverId;
		std::string device;
	};

	ofxGgmlNvigiRagBackend(
		RetrieveFunction retrieveFunction = {},
		GenerateFunction generateFunction = {},
		std::string displayName = "NVIGI RAG")
		: ofxGgmlNvigiRagBackend(
			std::move(retrieveFunction),
			std::move(generateFunction),
			Options{},
			std::move(displayName)) {
	}

	ofxGgmlNvigiRagBackend(
		RetrieveFunction retrieveFunction,
		GenerateFunction generateFunction,
		Options options,
		std::string displayName = "NVIGI RAG")
		: m_retrieveFunction(std::move(retrieveFunction))
		, m_generateFunction(std::move(generateFunction))
		, m_options(std::move(options))
		, m_displayName(std::move(displayName)) {
	}

	void setRetrieveFunction(RetrieveFunction retrieveFunction) {
		m_retrieveFunction = std::move(retrieveFunction);
	}

	void setGenerateFunction(GenerateFunction generateFunction) {
		m_generateFunction = std::move(generateFunction);
	}

	bool isConfigured() const {
		return static_cast<bool>(m_retrieveFunction) ||
			static_cast<bool>(m_generateFunction);
	}

	static bool isSdkEnabled() {
		return OFXGGML_ENABLE_NVIGI != 0;
	}

	const Options & getOptions() const {
		return m_options;
	}

	std::string backendName() const {
		return m_displayName.empty() ? "NVIGI RAG" : m_displayName;
	}

	void addDocument(const ofxGgmlRAGDocument & document) {
		m_documents.push_back(document);
	}

	void clearDocuments() {
		m_documents.clear();
	}

	size_t documentCount() const {
		return m_documents.size();
	}

	const std::vector<ofxGgmlRAGDocument> & getDocuments() const {
		return m_documents;
	}

	ofxGgmlRAGRetrievalResult retrieve(const ofxGgmlRAGQuery & query) const {
		ofxGgmlRAGRetrievalResult result;
		if (!isSdkEnabled()) {
			result.error =
				"NVIGI RAG is disabled. Define OFXGGML_ENABLE_NVIGI=1 and "
				"attach an NVIGI SDK RAG callback before calling retrieve().";
			return result;
		}
		if (!m_retrieveFunction) {
			result.error =
				"NVIGI RAG retrieve callback is not configured yet.";
			return result;
		}
		result = m_retrieveFunction(query, m_documents);
		return result;
	}

	ofxGgmlRAGResult generate(
		const ofxGgmlRAGRequest & request,
		std::function<bool(const std::string &)> onChunk = nullptr) const {
		ofxGgmlRAGResult result;
		if (!isSdkEnabled()) {
			result.error =
				"NVIGI RAG is disabled. Define OFXGGML_ENABLE_NVIGI=1 and "
				"attach an NVIGI SDK RAG generation callback before calling generate().";
			return result;
		}
		if (!m_generateFunction) {
			result.error =
				"NVIGI RAG generate callback is not configured yet.";
			return result;
		}

		const auto started = std::chrono::steady_clock::now();
		result = m_generateFunction(request, m_documents, std::move(onChunk));
		if (result.elapsedMs <= 0.0f) {
			result.elapsedMs = std::chrono::duration<float, std::milli>(
				std::chrono::steady_clock::now() - started).count();
		}
		return result;
	}

private:
	RetrieveFunction m_retrieveFunction;
	GenerateFunction m_generateFunction;
	Options m_options;
	std::string m_displayName;
	std::vector<ofxGgmlRAGDocument> m_documents;
};
