#pragma once

#include "ofMain.h"
#include "ofxGgmlWorkflows.h"

#include <future>
#include <string>
#include <vector>

class ofApp : public ofBaseApp {
public:
	void setup() override;
	void update() override;
	void draw() override;
	void keyPressed(int key) override;
	void exit() override;

private:
	void startCrawl();
	void finishCrawl(ofxGgmlWebCrawlerResult result);
	void drawControls(float x, float y) const;
	void drawDocumentsPanel(float x, float y, float width, float height) const;
	void drawPreviewPanel(float x, float y, float width, float height) const;
	std::string wrapText(const std::string & text, size_t maxColumns) const;
	std::string buildDocumentListLabel(const ofxGgmlCrawledDocument & document, size_t index) const;
	std::string buildPreviewText() const;
	std::string buildRunOutputDir() const;

	ofxGgmlEasy ai;
	std::future<ofxGgmlWebCrawlerResult> crawlFuture;
	bool crawlInFlight = false;
	bool renderJavaScript = false;
	int maxDepth = 1;
	int selectedDocumentIndex = 0;
	std::string urlInput = "https://example.com";
	std::string statusMessage = "Ready. Press Enter to crawl the current URL.";
	ofxGgmlWebCrawlerResult lastResult;
	std::vector<ofxGgmlCrawledDocument> documents;
};
