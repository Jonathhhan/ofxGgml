#include "ofApp.h"

#include "utils/ImGuiHelpers.h"
#include "utils/PathHelpers.h"

#include <cctype>
#include <unordered_set>

namespace {

std::string trimCopyLocal(const std::string & text) {
	size_t start = 0;
	while (start < text.size() &&
		std::isspace(static_cast<unsigned char>(text[start]))) {
		++start;
	}
	size_t end = text.size();
	while (end > start &&
		std::isspace(static_cast<unsigned char>(text[end - 1]))) {
		--end;
	}
	return text.substr(start, end - start);
}

std::string parseUrlHostLocal(const std::string & rawUrl) {
	const std::string trimmed = trimCopyLocal(rawUrl);
	const size_t schemePos = trimmed.find("://");
	if (schemePos == std::string::npos) {
		return {};
	}
	const size_t hostStart = schemePos + 3;
	size_t hostEnd = trimmed.find_first_of("/?#", hostStart);
	if (hostEnd == std::string::npos) {
		hostEnd = trimmed.size();
	}
	std::string hostPort = trimmed.substr(hostStart, hostEnd - hostStart);
	const size_t atPos = hostPort.rfind('@');
	if (atPos != std::string::npos) {
		hostPort = hostPort.substr(atPos + 1);
	}
	const size_t colonPos = hostPort.find(':');
	if (colonPos != std::string::npos) {
		hostPort = hostPort.substr(0, colonPos);
	}
	return trimCopyLocal(hostPort);
}

struct CitationSourceRow {
	int sourceIndex = -1;
	std::string sourceLabel;
	std::string sourceUri;
};

std::vector<CitationSourceRow> collectCitationSources(
	const std::vector<ofxGgmlCitationItem> & items) {
	std::vector<CitationSourceRow> rows;
	rows.reserve(items.size());
	std::unordered_set<std::string> seen;
	for (const auto & item : items) {
		const std::string key =
			std::to_string(item.sourceIndex) + "|" + item.sourceUri + "|" + item.sourceLabel;
		if (!seen.insert(key).second) {
			continue;
		}
		rows.push_back({
			item.sourceIndex,
			item.sourceLabel,
			item.sourceUri
		});
	}
	return rows;
}

} // namespace

void ofApp::drawCitationSearchSection(
	const char * useInputButtonLabel,
	const std::string & suggestedTopic) {
	ImGui::Separator();
	ImGui::Text("Citation Research");
	ImGui::TextDisabled(
		"Extract source-backed quotes about a topic from loaded URLs, a crawler URL, or topic-only web search.");

	if (hasDeferredCitationTopic) {
		copyStringToBuffer(
			citationTopic,
			sizeof(citationTopic),
			deferredCitationTopic);
		hasDeferredCitationTopic = false;
		deferredCitationTopic.clear();
	}
	if (hasDeferredCitationSeedUrl) {
		copyStringToBuffer(
			citationSeedUrl,
			sizeof(citationSeedUrl),
			deferredCitationSeedUrl);
		hasDeferredCitationSeedUrl = false;
		deferredCitationSeedUrl.clear();
	}

	ImGui::InputText("Citation topic", citationTopic, sizeof(citationTopic));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}

	ImGui::InputText("Crawler URL", citationSeedUrl, sizeof(citationSeedUrl));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	if (ImGui::Checkbox("Use crawler URL", &citationUseCrawler)) {
		autoSaveSession();
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(120);
	if (ImGui::SliderInt("Max quotes", &citationMaxResults, 1, 12)) {
		autoSaveSession();
	}

	ImGui::BeginDisabled(trim(sourceUrlsInput).empty());
	if (ImGui::SmallButton("Use Loaded URLs")) {
		if (trim(citationSeedUrl).empty()) {
			const auto urls = splitStoredScriptSourceUrls(sourceUrlsInput);
			if (!urls.empty()) {
				deferredCitationSeedUrl = urls.front();
				hasDeferredCitationSeedUrl = true;
			}
		}
		if (trim(citationTopic).empty() && !trim(suggestedTopic).empty()) {
			deferredCitationTopic = suggestedTopic;
			hasDeferredCitationTopic = true;
		}
		citationUseCrawler = false;
		autoSaveSession();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(trim(suggestedTopic).empty());
	if (ImGui::SmallButton(useInputButtonLabel)) {
		deferredCitationTopic = suggestedTopic;
		hasDeferredCitationTopic = true;
		autoSaveSession();
	}
	ImGui::EndDisabled();

	const bool hasTopic = std::strlen(citationTopic) > 0;
	const bool hasCrawlerUrl = std::strlen(citationSeedUrl) > 0;
	const bool hasLoadedUrls = !trim(sourceUrlsInput).empty();
	const bool canRun =
		!generating.load() &&
		hasTopic &&
		(citationUseCrawler ? hasCrawlerUrl : true);

	ImGui::BeginDisabled(!canRun);
	if (ImGui::Button("Scrape Citations", ImVec2(160, 0))) {
		runCitationSearch();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (!citationOutput.empty()) {
		if (ImGui::SmallButton("Copy Citations")) {
			copyToClipboard(citationOutput);
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Send to Write##Citations")) {
			copyStringToBuffer(writeInput, sizeof(writeInput), citationOutput);
			activeMode = AiMode::Write;
		}
	}

	if (!citationBackendName.empty()) {
		ImGui::TextDisabled(
			"Backend: %s  %.1f ms  Quotes: %d",
			citationBackendName.c_str(),
			citationElapsedMs,
			static_cast<int>(citationResults.size()));
	}

	if (!citationResults.empty()) {
		ImGui::BeginChild("##CitationResults", ImVec2(0, 170), true);
		for (size_t i = 0; i < citationResults.size(); ++i) {
			const auto & item = citationResults[i];
			ImGui::PushID(static_cast<int>(i));
			ImGui::TextDisabled(
				"[Source %d] %s",
				item.sourceIndex,
				item.sourceLabel.empty() ? "unknown" : item.sourceLabel.c_str());
			ImGui::TextWrapped("\"%s\"", item.quote.c_str());
			if (!item.note.empty()) {
				ImGui::TextWrapped("Note: %s", item.note.c_str());
			}
			if (!item.sourceUri.empty()) {
				const std::string host = parseUrlHostLocal(item.sourceUri);
				if (!host.empty()) {
					ImGui::TextDisabled("Domain: %s", host.c_str());
				}
				if (ImGui::SmallButton("Open Source")) {
					ofLaunchBrowser(item.sourceUri);
				}
				ImGui::SameLine();
				ImGui::TextDisabled("%s", item.sourceUri.c_str());
			}
			ImGui::Spacing();
			ImGui::PopID();
		}
		ImGui::EndChild();

		const auto sourceRows = collectCitationSources(citationResults);
		if (!sourceRows.empty()) {
			ImGui::Spacing();
			ImGui::Text("Sources");
			ImGui::BeginChild("##CitationSources", ImVec2(0, 120), true);
			for (size_t i = 0; i < sourceRows.size(); ++i) {
				const auto & row = sourceRows[i];
				ImGui::PushID(static_cast<int>(i));
				if (!trim(row.sourceUri).empty()) {
					if (ImGui::SmallButton("Open")) {
						ofLaunchBrowser(row.sourceUri);
					}
					ImGui::SameLine();
				}
				std::string label =
					row.sourceLabel.empty()
						? ("Source " + ofToString(row.sourceIndex))
						: row.sourceLabel;
				if (row.sourceIndex > 0) {
					label = "[" + ofToString(row.sourceIndex) + "] " + label;
				}
				ImGui::TextWrapped("%s", label.c_str());
				if (!trim(row.sourceUri).empty()) {
					const std::string host = parseUrlHostLocal(row.sourceUri);
					if (!host.empty()) {
						ImGui::TextDisabled("Domain: %s", host.c_str());
					}
					ImGui::TextDisabled("%s", row.sourceUri.c_str());
				}
				ImGui::Spacing();
				ImGui::PopID();
			}
			ImGui::EndChild();
		}
	} else if (!citationOutput.empty()) {
		ImGui::BeginChild("##CitationOutput", ImVec2(0, 140), true);
		ImGui::TextWrapped("%s", citationOutput.c_str());
		ImGui::EndChild();
	}
}

void ofApp::runCitationSearch() {
	if (generating.load()) {
		return;
	}

	const std::string topic = trim(citationTopic);
	if (topic.empty()) {
		return;
	}

	const bool useCrawler = citationUseCrawler;
	const std::string crawlUrl = trim(citationSeedUrl);
	const auto loadedUrls = splitStoredScriptSourceUrls(sourceUrlsInput);
	if (useCrawler && crawlUrl.empty()) {
		return;
	}

	const AiMode requestMode = activeMode;

	generating.store(true);
	cancelRequested.store(false);
	activeGenerationMode = requestMode;
	generatingStatus = useCrawler
		? "Crawling website and extracting citations..."
		: (loadedUrls.empty()
			? "Searching topic sources and extracting citations..."
			: "Extracting citations from loaded URLs...");
	generationStartTime = ofGetElapsedTimef();

	{
		std::lock_guard<std::mutex> lock(streamMutex);
		streamingOutput = useCrawler
			? "Crawling sources and extracting citation candidates..."
			: (loadedUrls.empty()
				? "Finding web sources for the topic and extracting citation candidates..."
				: "Reading loaded URLs and extracting citation candidates...");
	}

	if (workerThread.joinable()) {
		workerThread.join();
	}

	const std::string modelPath = getSelectedModelPath();
	const int maxCitations = std::clamp(citationMaxResults, 1, 12);
	const auto inferenceSettings = buildCurrentTextInferenceSettings(requestMode);
	workerThread = std::thread([this, topic, useCrawler, crawlUrl, loadedUrls, modelPath, maxCitations, inferenceSettings]() {
		try {
			ofxGgmlCitationSearchRequest request;
			request.modelPath = modelPath;
			request.topic = topic;
			request.maxCitations = static_cast<size_t>(maxCitations);
			request.useCrawler = useCrawler;
			request.inferenceSettings = inferenceSettings;
			request.sourceSettings.requestCitations = true;
			request.sourceSettings.maxSources = 8;
			request.sourceSettings.maxCharsPerSource = 2500;
			request.sourceSettings.maxTotalChars = 12000;
			request.sourceUrls = loadedUrls;
			if (useCrawler) {
				request.crawlerRequest.startUrl = crawlUrl;
				request.crawlerRequest.maxDepth = 1;
				request.crawlerRequest.renderJavaScript = false;
				request.crawlerRequest.keepOutputFiles = true;
			}

			const ofxGgmlCitationSearchResult result = citationSearch.search(request);

			std::ostringstream text;
			if (result.success) {
				if (!result.summary.empty() && !result.citations.empty()) {
					text << result.summary << "\n\n";
				}
				for (size_t i = 0; i < result.citations.size(); ++i) {
					const auto & item = result.citations[i];
					text << i + 1 << ". \"" << item.quote << "\"\n";
					text << "   Source: ";
					if (!item.sourceLabel.empty()) {
						text << item.sourceLabel;
					} else {
						text << "Source " << item.sourceIndex;
					}
					if (!item.sourceUri.empty()) {
						text << " (" << item.sourceUri << ")";
					}
					text << "\n";
					if (!item.note.empty()) {
						text << "   Note: " << item.note << "\n";
					}
					text << "\n";
				}
				if (result.citations.empty()) {
					text << "No strong citations were found in the current sources.";
				}
			} else {
				text << "[Error] " << result.error;
			}

			{
				std::lock_guard<std::mutex> lock(outputMutex);
				pendingCitationDirty = true;
				pendingCitationBackendName = result.backendName;
				pendingCitationElapsedMs = result.elapsedMs;
				pendingCitationResults = result.citations;
				pendingCitationOutput = text.str();
			}
		} catch (const std::exception & e) {
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingCitationDirty = true;
			pendingCitationBackendName = "CitationSearch";
			pendingCitationElapsedMs = 0.0f;
			pendingCitationResults.clear();
			pendingCitationOutput = std::string("[Error] Internal exception: ") + e.what();
		} catch (...) {
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingCitationDirty = true;
			pendingCitationBackendName = "CitationSearch";
			pendingCitationElapsedMs = 0.0f;
			pendingCitationResults.clear();
			pendingCitationOutput = "[Error] Unknown citation-search exception.";
		}

		generating.store(false);
	});
}
