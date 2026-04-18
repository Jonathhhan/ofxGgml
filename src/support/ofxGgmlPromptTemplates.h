#pragma once

#include "ofMain.h"
#include <functional>
#include <map>
#include <regex>
#include <string>
#include <vector>

/// Prompt template with variable substitution and reusable task patterns.
///
/// Provides a library of common prompt templates for different AI tasks
/// with variable substitution support.
///
/// Example usage:
/// ```cpp
/// auto& templates = ofxGgmlPromptTemplates::getInstance();
///
/// // Use built-in template
/// std::string prompt = templates.fill("summarize", {
///     {"text", articleContent},
///     {"max_length", "3 sentences"}
/// });
///
/// // Register custom template
/// templates.registerTemplate("custom_task",
///     "Analyze the following {{data_type}}:\n{{content}}\n\nFocus on {{aspect}}.");
///
/// std::string customPrompt = templates.fill("custom_task", {
///     {"data_type", "code snippet"},
///     {"content", codeText},
///     {"aspect", "performance"}
/// });
/// ```
class ofxGgmlPromptTemplates {
public:
	using VariableMap = std::map<std::string, std::string>;

	/// Get singleton instance.
	static ofxGgmlPromptTemplates& getInstance() {
		static ofxGgmlPromptTemplates instance;
		return instance;
	}

	/// Register a custom template.
	void registerTemplate(const std::string& name, const std::string& templateText) {
		m_templates[name] = templateText;
	}

	/// Remove a template.
	void unregisterTemplate(const std::string& name) {
		m_templates.erase(name);
	}

	/// Check if a template exists.
	bool hasTemplate(const std::string& name) const {
		return m_templates.find(name) != m_templates.end();
	}

	/// Get raw template text.
	std::string getTemplate(const std::string& name) const {
		auto it = m_templates.find(name);
		return (it != m_templates.end()) ? it->second : "";
	}

	/// Fill template with variables. Variables use {{variable_name}} syntax.
	std::string fill(const std::string& templateName, const VariableMap& variables) const {
		auto it = m_templates.find(templateName);
		if (it == m_templates.end()) {
			return "";
		}
		return substituteVariables(it->second, variables);
	}

	/// Fill template text directly with variables.
	static std::string fillText(const std::string& templateText, const VariableMap& variables) {
		return substituteVariables(templateText, variables);
	}

	/// List all registered template names.
	std::vector<std::string> listTemplates() const {
		std::vector<std::string> names;
		for (const auto& [name, _] : m_templates) {
			names.push_back(name);
		}
		return names;
	}

	/// Reset to default templates only.
	void resetToDefaults() {
		m_templates.clear();
		initializeDefaults();
	}

private:
	ofxGgmlPromptTemplates() {
		initializeDefaults();
	}

	void initializeDefaults() {
		// Text processing templates
		m_templates["summarize"] =
			"Summarize the following text in {{max_length|3 sentences}}:\n\n{{text}}\n\nSummary:";

		m_templates["key_points"] =
			"Extract the key points from the following text:\n\n{{text}}\n\nKey points:";

		m_templates["expand"] =
			"Expand on the following text with more detail:\n\n{{text}}\n\nExpanded version:";

		m_templates["rewrite"] =
			"Rewrite the following text in a {{style|professional}} style:\n\n{{text}}\n\nRewritten:";

		m_templates["translate"] =
			"Translate the following text to {{target_language}}:\n\n{{text}}\n\nTranslation:";

		// Question answering templates
		m_templates["qa"] =
			"Answer the following question based on the context:\n\nContext: {{context}}\n\nQuestion: {{question}}\n\nAnswer:";

		m_templates["qa_with_sources"] =
			"Answer the following question using the provided sources. Cite sources as [Source N].\n\n{{sources}}\n\nQuestion: {{question}}\n\nAnswer:";

		// Code-related templates
		m_templates["explain_code"] =
			"Explain what the following {{language|code}} does:\n\n```{{language|code}}\n{{code}}\n```\n\nExplanation:";

		m_templates["review_code"] =
			"Review the following {{language|code}} for potential issues, improvements, and best practices:\n\n```{{language|code}}\n{{code}}\n```\n\nCode Review:";

		m_templates["generate_code"] =
			"Generate {{language}} code that {{task}}.\n\n{{requirements|Requirements:\n{{requirements}}\n\n}}Code:";

		m_templates["debug_code"] =
			"Debug the following {{language|code}} which has this error:\n\nError: {{error}}\n\n```{{language|code}}\n{{code}}\n```\n\nFix:";

		m_templates["document_code"] =
			"Generate documentation for the following {{language|code}}:\n\n```{{language|code}}\n{{code}}\n```\n\nDocumentation:";

		// Creative writing templates
		m_templates["story"] =
			"Write a {{genre|short}} story about {{topic}}. {{constraints|}}";

		m_templates["dialogue"] =
			"Write a dialogue between {{characters}} about {{topic}}. {{style|}}";

		m_templates["poem"] =
			"Write a {{style|}} poem about {{topic}}. {{constraints|}}";

		// Business/professional templates
		m_templates["email"] =
			"Write a {{tone|professional}} email {{purpose}}.\n\n{{context|Context:\n{{context}}\n\n}}Email:";

		m_templates["meeting_notes"] =
			"Create structured meeting notes from the following transcript:\n\n{{transcript}}\n\nMeeting Notes:";

		m_templates["action_items"] =
			"Extract action items and tasks from the following text:\n\n{{text}}\n\nAction Items:";

		m_templates["executive_summary"] =
			"Create an executive summary of the following:\n\n{{text}}\n\nExecutive Summary:";

		// Analysis templates
		m_templates["sentiment"] =
			"Analyze the sentiment of the following text (positive, negative, neutral):\n\n{{text}}\n\nSentiment:";

		m_templates["classify"] =
			"Classify the following text into one of these categories: {{categories}}\n\n{{text}}\n\nCategory:";

		m_templates["extract_entities"] =
			"Extract named entities (people, places, organizations, dates) from:\n\n{{text}}\n\nEntities:";

		// Chat/conversation templates
		m_templates["chat_system"] =
			"You are {{role|a helpful assistant}}. {{instructions|}}{{personality|}}";

		m_templates["chat_context"] =
			"{{system_prompt|You are a helpful assistant.}}\n\nConversation history:\n{{history}}\n\nUser: {{message}}\nAssistant:";

		// Multimodal templates
		m_templates["image_caption"] =
			"Describe what you see in this image. {{focus|}}";

		m_templates["image_qa"] =
			"Look at this image and answer the question: {{question}}";

		m_templates["ocr"] =
			"Extract all text from this image, maintaining the layout and structure as much as possible.";

		// RAG (Retrieval-Augmented Generation) templates
		m_templates["rag_simple"] =
			"Based on the following context, answer the question.\n\nContext:\n{{context}}\n\nQuestion: {{question}}\n\nAnswer:";

		m_templates["rag_with_citations"] =
			"Based on the following sources, answer the question. Include citations as [Source N].\n\n{{sources}}\n\nQuestion: {{question}}\n\nAnswer:";

		// System/meta templates
		m_templates["json_response"] =
			"{{task}}\n\nRespond with valid JSON matching this schema:\n{{schema}}\n\nJSON Response:";

		m_templates["structured_output"] =
			"{{task}}\n\nProvide your response in the following format:\n{{format}}\n\nResponse:";
	}

	static std::string substituteVariables(const std::string& templateText, const VariableMap& variables) {
		std::string result = templateText;

		// Match {{variable}} or {{variable|default}}
		std::regex varRegex(R"(\{\{([^}|]+)(?:\|([^}]*))?\}\})");
		std::smatch match;

		std::string::const_iterator searchStart(result.cbegin());
		std::vector<std::pair<size_t, size_t>> replacements; // Store positions to replace
		std::vector<std::string> replacementValues;

		while (std::regex_search(searchStart, result.cend(), match, varRegex)) {
			std::string varName = match[1].str();
			std::string defaultValue = match[2].str();

			// Trim whitespace from variable name
			varName = trim(varName);

			std::string replacement;
			auto it = variables.find(varName);
			if (it != variables.end()) {
				replacement = it->second;
			} else {
				replacement = defaultValue;
			}

			size_t startPos = match.position(0) + std::distance(result.cbegin(), searchStart);
			size_t endPos = startPos + match.length(0);

			replacements.push_back({startPos, endPos});
			replacementValues.push_back(replacement);

			searchStart = match.suffix().first;
		}

		// Apply replacements in reverse order to maintain positions
		for (int i = static_cast<int>(replacements.size()) - 1; i >= 0; --i) {
			result.replace(replacements[i].first,
			              replacements[i].second - replacements[i].first,
			              replacementValues[i]);
		}

		return result;
	}

	static std::string trim(const std::string& str) {
		size_t start = str.find_first_not_of(" \t\n\r");
		if (start == std::string::npos) return "";
		size_t end = str.find_last_not_of(" \t\n\r");
		return str.substr(start, end - start + 1);
	}

	std::map<std::string, std::string> m_templates;
};
