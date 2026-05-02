#include "ofApp.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <future>
#include <sstream>

namespace {

constexpr float kMargin = 20.0f;
constexpr float kPanelGap = 16.0f;
constexpr float kHeaderHeight = 164.0f;
constexpr size_t kIntroWrapColumns = 110;
constexpr size_t kShortcutsWrapColumns = 115;
constexpr size_t kStatusWrapColumns = 118;
constexpr size_t kDocumentListWrapColumns = 38;
constexpr size_t kPreviewWrapColumns = 78;
constexpr size_t kPreviewMaxCharacters = 2200;

std::string trimCopy(const std::string & text) {
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

std::string tailLines(const std::string & text, size_t maxLines) {
	if (maxLines == 0 || text.empty()) {
		return {};
	}

	std::vector<std::string> lines;
	std::istringstream stream(text);
	std::string line;
	while (std::getline(stream, line)) {
		lines.push_back(line);
	}

	if (lines.size() <= maxLines) {
		return text;
	}

	std::ostringstream output;
	for (size_t i = lines.size() - maxLines; i < lines.size(); ++i) {
		output << lines[i];
		if (i + 1 < lines.size()) {
			output << '\n';
		}
	}
	return output.str();
}

} // namespace

void ofApp::setup() {
	ofSetWindowTitle("ofxGgml - Web Scraping Example");
	ofSetFrameRate(60);
	ofBackground(18, 22, 28);

	ofxGgmlEasyCrawlerConfig crawlerConfig;
	crawlerConfig.keepOutputFiles = true;
	crawlerConfig.maxDepth = maxDepth;
	crawlerConfig.outputDir = ofToDataPath("crawler-output", true);
	ai.configureWebCrawler(crawlerConfig);

	statusMessage =
		"Ready. Press Enter to crawl the current URL with the built-in HTML crawler.";
}

void ofApp::update() {
	if (!crawlInFlight || !crawlFuture.valid()) {
		return;
	}

	if (crawlFuture.wait_for(std::chrono::milliseconds(0)) !=
		std::future_status::ready) {
		return;
	}

	finishCrawl(crawlFuture.get());
}

void ofApp::draw() {
	ofBackgroundGradient(
		ofColor(18, 22, 28),
		ofColor(8, 10, 14),
		OF_GRADIENT_LINEAR);

	const float contentWidth = static_cast<float>(ofGetWidth()) - (kMargin * 2.0f);
	const float panelY = kMargin + kHeaderHeight;
	const float panelHeight = static_cast<float>(ofGetHeight()) - panelY - kMargin;
	const float leftWidth = std::max(320.0f, contentWidth * 0.3f);
	const float rightWidth = contentWidth - leftWidth - kPanelGap;

	drawControls(kMargin, kMargin);
	drawDocumentsPanel(kMargin, panelY, leftWidth, panelHeight);
	drawPreviewPanel(kMargin + leftWidth + kPanelGap, panelY, rightWidth, panelHeight);
}

void ofApp::keyPressed(int key) {
	if (key == OF_KEY_RETURN) {
		startCrawl();
		return;
	}

	if (key == OF_KEY_BACKSPACE) {
		if (!urlInput.empty()) {
			urlInput.pop_back();
		}
		return;
	}

	if (key == OF_KEY_UP) {
		if (!documents.empty()) {
			selectedDocumentIndex =
				std::max(0, selectedDocumentIndex - 1);
		}
		return;
	}

	if (key == OF_KEY_DOWN) {
		if (!documents.empty()) {
			selectedDocumentIndex =
				std::min(static_cast<int>(documents.size()) - 1, selectedDocumentIndex + 1);
		}
		return;
	}

	if (key == '[') {
		maxDepth = std::max(0, maxDepth - 1);
		return;
	}

	if (key == ']') {
		maxDepth = std::min(4, maxDepth + 1);
		return;
	}

	if (key == 'j' || key == 'J') {
		renderJavaScript = !renderJavaScript;
		return;
	}

	if (key == 'c' || key == 'C') {
		lastResult = {};
		documents.clear();
		selectedDocumentIndex = 0;
		statusMessage = "Cleared results. Press Enter to crawl again.";
		return;
	}

	if (key == 'q' || key == 'Q') {
		ofExit();
		return;
	}

	if (key >= 32 && key <= 126) {
		urlInput.push_back(static_cast<char>(key));
	}
}

void ofApp::exit() {
	if (crawlFuture.valid()) {
		crawlFuture.wait();
	}
}

void ofApp::startCrawl() {
	if (crawlInFlight) {
		statusMessage = "A crawl is already running.";
		return;
	}

	const std::string startUrl = trimCopy(urlInput);
	if (startUrl.empty()) {
		statusMessage = "Enter a URL before starting a crawl.";
		return;
	}

	ofxGgmlWebCrawlerRequest request;
	request.startUrl = startUrl;
	request.maxDepth = maxDepth;
	request.renderJavaScript = renderJavaScript;
	request.keepOutputFiles = true;
	request.outputDir = buildRunOutputDir();
	request.executablePath = ai.getWebCrawlerConfig().executablePath;
	request.extraArgs = ai.getWebCrawlerConfig().extraArgs;
	request.allowedDomains = ai.getWebCrawlerConfig().allowedDomains;

	lastResult = {};
	documents.clear();
	selectedDocumentIndex = 0;
	statusMessage = "Crawling " + startUrl + "...";
	crawlInFlight = true;
	crawlFuture = std::async(
		std::launch::async,
		[this, request]() {
			return ai.getWebCrawler().crawl(request);
		});
}

void ofApp::finishCrawl(ofxGgmlWebCrawlerResult result) {
	crawlInFlight = false;
	lastResult = std::move(result);
	documents = lastResult.documents;
	selectedDocumentIndex = 0;

	if (lastResult.success) {
		statusMessage =
			"Crawl finished with " + lastResult.backendName + ": " +
			ofToString(documents.size()) + " document(s) in " +
			ofToString(lastResult.elapsedMs, 1) + " ms.";
	} else {
		statusMessage = "Crawl failed: " + lastResult.error;
	}
}

void ofApp::drawControls(float x, float y) const {
	ofSetColor(245);
	ofDrawBitmapStringHighlight("ofxGgml Web Scraping Example", x, y + 16.0f);

	ofSetColor(220);
	const std::string intro =
		"Type a URL, press Enter, and inspect the crawled Markdown output. "
		"Use J to toggle JavaScript rendering and [ ] to change crawl depth.";
	ofDrawBitmapString(wrapText(intro, kIntroWrapColumns), x, y + 42.0f);

	ofSetColor(32, 40, 52, 220);
	ofDrawRectangle(x, y + 58.0f, static_cast<float>(ofGetWidth()) - (kMargin * 2.0f), 92.0f);

	ofSetColor(240);
	ofDrawBitmapString("URL", x + 10.0f, y + 82.0f);
	ofSetColor(220);
	ofDrawBitmapString(urlInput + (crawlInFlight ? "" : "_"), x + 54.0f, y + 82.0f);

	std::ostringstream controls;
	controls
		<< "Depth: " << maxDepth
		<< "   Render JS: " << (renderJavaScript ? "on" : "off")
		<< "   Documents: " << documents.size()
		<< "   Backend: " << (lastResult.backendName.empty() ? "n/a" : lastResult.backendName);
	ofDrawBitmapString(controls.str(), x + 10.0f, y + 108.0f);

	const std::string shortcuts =
		"Enter crawl  |  Up/Down select page  |  [ ] depth  |  J toggle JS render  |  C clear  |  Q quit";
	ofDrawBitmapString(wrapText(shortcuts, kShortcutsWrapColumns), x + 10.0f, y + 132.0f);

	ofSetColor(crawlInFlight ? ofColor(255, 220, 120) : ofColor(160, 220, 180));
	ofDrawBitmapString(wrapText(statusMessage, kStatusWrapColumns), x, y + 170.0f);
}

void ofApp::drawDocumentsPanel(float x, float y, float width, float height) const {
	ofSetColor(28, 34, 44, 220);
	ofDrawRectangle(x, y, width, height);

	ofSetColor(245);
	ofDrawBitmapStringHighlight("Crawled documents", x + 10.0f, y + 18.0f);

	float cursorY = y + 42.0f;
	if (documents.empty()) {
		ofSetColor(180);
		ofDrawBitmapString(
			"No pages loaded yet. Try https://example.com or another static site.",
			x + 10.0f,
			cursorY);
		return;
	}

	for (size_t i = 0; i < documents.size(); ++i) {
		const bool selected = static_cast<int>(i) == selectedDocumentIndex;
		if (selected) {
			ofSetColor(68, 94, 140, 220);
			ofDrawRectangle(x + 8.0f, cursorY - 12.0f, width - 16.0f, 42.0f);
		}

		ofSetColor(selected ? ofColor(255) : ofColor(210));
		const std::string label = buildDocumentListLabel(documents[i], i);
		ofDrawBitmapString(wrapText(label, kDocumentListWrapColumns), x + 14.0f, cursorY);
		cursorY += 48.0f;

		if (cursorY > y + height - 16.0f) {
			break;
		}
	}
}

void ofApp::drawPreviewPanel(float x, float y, float width, float height) const {
	ofSetColor(22, 28, 36, 220);
	ofDrawRectangle(x, y, width, height);

	ofSetColor(245);
	ofDrawBitmapStringHighlight("Selected document preview", x + 10.0f, y + 18.0f);

	ofSetColor(220);
	const std::string previewText = buildPreviewText();
	ofDrawBitmapString(
		wrapText(previewText, kPreviewWrapColumns),
		x + 10.0f,
		y + 42.0f);
}

std::string ofApp::wrapText(const std::string & text, size_t maxColumns) const {
	if (text.empty() || maxColumns == 0) {
		return text;
	}

	std::ostringstream wrapped;
	std::istringstream input(text);
	std::string rawLine;
	bool firstParagraph = true;
	while (std::getline(input, rawLine)) {
		const std::string line = trimCopy(rawLine);
		if (!firstParagraph) {
			wrapped << '\n';
		}
		firstParagraph = false;
		if (line.empty()) {
			continue;
		}

		std::istringstream words(line);
		std::string word;
		size_t currentColumns = 0;
		while (words >> word) {
			const size_t nextColumns =
				currentColumns == 0
					? word.size()
					: currentColumns + 1 + word.size();
			if (currentColumns > 0 && nextColumns > maxColumns) {
				wrapped << '\n';
				wrapped << word;
				currentColumns = word.size();
			} else {
				if (currentColumns > 0) {
					wrapped << ' ';
					++currentColumns;
				}
				wrapped << word;
				currentColumns += word.size();
			}
		}
	}

	return wrapped.str();
}

std::string ofApp::buildDocumentListLabel(
	const ofxGgmlCrawledDocument & document,
	size_t index) const {
	std::ostringstream label;
	label
		<< index + 1 << ". "
		<< (document.title.empty() ? document.sourceUrl : document.title)
		<< "\nDepth " << document.crawlDepth
		<< "  |  "
		<< ofToString(document.byteSize) << " bytes";
	return label.str();
}

std::string ofApp::buildPreviewText() const {
	std::ostringstream preview;
	preview
		<< "Backend: "
		<< (lastResult.backendName.empty() ? "n/a" : lastResult.backendName)
		<< "\nElapsed: " << ofToString(lastResult.elapsedMs, 1) << " ms"
		<< "\nSaved files: " << lastResult.savedFiles.size()
		<< "\nOutput dir: "
		<< (lastResult.outputDir.empty() ? "(temporary)" : lastResult.outputDir);

	if (!lastResult.error.empty()) {
		preview << "\n\nError\n" << lastResult.error;
	}

	if (!documents.empty()) {
		const auto & document =
			documents[static_cast<size_t>(
				std::clamp(selectedDocumentIndex, 0, static_cast<int>(documents.size()) - 1))];
		preview
			<< "\n\nTitle\n" << (document.title.empty() ? "(untitled)" : document.title)
			<< "\n\nSource URL\n" << document.sourceUrl
			<< "\n\nLocal path\n" << (document.localPath.empty() ? "(kept in memory)" : document.localPath)
			<< "\n\nMarkdown\n"
			<< document.markdown.substr(
				0,
				std::min<size_t>(document.markdown.size(), kPreviewMaxCharacters));
		if (document.markdown.size() > kPreviewMaxCharacters) {
			preview << "\n... (truncated)";
		}
	} else if (lastResult.success) {
		preview << "\n\nNo documents were returned for this crawl.";
	}

	if (!lastResult.commandOutput.empty()) {
		preview
			<< "\n\nCrawler log (tail)\n"
			<< tailLines(lastResult.commandOutput, 10);
	}

	return preview.str();
}

std::string ofApp::buildRunOutputDir() const {
	return ofToDataPath(
		"crawler-output/" + ofGetTimestampString("%Y%m%d-%H%M%S"),
		true);
}
