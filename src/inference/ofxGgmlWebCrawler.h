#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct ofxGgmlCrawledDocument {
	std::string title;
	std::string sourceUrl;
	std::string localPath;
	std::string markdown;
	int crawlDepth = -1;
	size_t byteSize = 0;
};

struct ofxGgmlWebCrawlerRequest {
	std::string startUrl;
	std::string outputDir;
	int maxDepth = 2;
	bool renderJavaScript = false;
	bool keepOutputFiles = true;
	std::vector<std::string> allowedDomains;
	std::vector<std::string> extraArgs;
	std::string executablePath;
};

struct ofxGgmlWebCrawlerResult {
	bool success = false;
	float elapsedMs = 0.0f;
	std::string backendName;
	std::string startUrl;
	std::string outputDir;
	std::string normalizedCommand;
	std::string commandOutput;
	std::string error;
	int exitCode = -1;
	std::vector<std::string> savedFiles;
	std::vector<ofxGgmlCrawledDocument> documents;
};

class ofxGgmlWebCrawlerBackend {
public:
	virtual ~ofxGgmlWebCrawlerBackend() = default;
	virtual std::string backendName() const = 0;
	virtual ofxGgmlWebCrawlerResult crawl(
		const ofxGgmlWebCrawlerRequest & request) const = 0;
};

class ofxGgmlWebCrawlerBridgeBackend : public ofxGgmlWebCrawlerBackend {
public:
	using CrawlCallback =
		std::function<ofxGgmlWebCrawlerResult(
			const ofxGgmlWebCrawlerRequest &)>;

	explicit ofxGgmlWebCrawlerBridgeBackend(CrawlCallback callback);

	std::string backendName() const override;
	ofxGgmlWebCrawlerResult crawl(
		const ofxGgmlWebCrawlerRequest & request) const override;

private:
	CrawlCallback m_callback;
};

class ofxGgmlMojoWebCrawlerBackend : public ofxGgmlWebCrawlerBackend {
public:
	explicit ofxGgmlMojoWebCrawlerBackend(
		const std::string & executablePath = "");

	void setExecutablePath(const std::string & path);
	const std::string & getExecutablePath() const;

	std::string backendName() const override;
	ofxGgmlWebCrawlerResult crawl(
		const ofxGgmlWebCrawlerRequest & request) const override;

private:
	std::string m_executablePath;
};

std::shared_ptr<ofxGgmlWebCrawlerBackend>
createWebCrawlerBridgeBackend(
	ofxGgmlWebCrawlerBridgeBackend::CrawlCallback callback);

class ofxGgmlWebCrawler {
public:
	ofxGgmlWebCrawler();

	void setBackend(std::shared_ptr<ofxGgmlWebCrawlerBackend> backend);
	std::shared_ptr<ofxGgmlWebCrawlerBackend> getBackend() const;
	ofxGgmlWebCrawlerResult crawl(
		const ofxGgmlWebCrawlerRequest & request) const;

private:
	std::shared_ptr<ofxGgmlWebCrawlerBackend> m_backend;
};
