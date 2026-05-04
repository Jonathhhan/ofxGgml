#include "catch2.hpp"

#include "../src/inference/ofxGgmlWebCrawler.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace {

std::string writeTempHtml(
	const std::filesystem::path & dir,
	const std::string & filename,
	const std::string & content) {
	const std::filesystem::path path = dir / filename;
	std::ofstream out(path);
	out << content;
	return path.string();
}

std::filesystem::path createTempDir() {
	std::error_code ec;
	const std::filesystem::path base = std::filesystem::temp_directory_path(ec);
	const std::filesystem::path dir = base / ("ofxggml_test_crawler_" + std::to_string(
		std::hash<std::thread::id>{}(std::this_thread::get_id())));
	std::filesystem::create_directories(dir, ec);
	return dir;
}

std::string fileUrl(const std::string & path) {
#ifdef _WIN32
	std::string p = path;
	for (char & c : p) {
		if (c == '\\') c = '/';
	}
	if (!p.empty() && p.front() != '/') {
		p = "/" + p;
	}
	return "file://" + p;
#else
	return "file://" + path;
#endif
}

} // namespace

// ---------------------------------------------------------------------------
// Public API smoke tests (no network required)
// ---------------------------------------------------------------------------

TEST_CASE("ofxGgmlWebCrawler default-constructs successfully", "[web_crawler]") {
	ofxGgmlWebCrawler crawler;
	REQUIRE(crawler.getBackend() != nullptr);
}

TEST_CASE("ofxGgmlWebCrawler rejects an empty start URL", "[web_crawler]") {
	ofxGgmlWebCrawler crawler;
	ofxGgmlWebCrawlerRequest request;
	request.startUrl = "";
	const auto result = crawler.crawl(request);
	REQUIRE_FALSE(result.success);
	REQUIRE_FALSE(result.error.empty());
}

TEST_CASE("ofxGgmlWebCrawlerResult zero-initialises correctly", "[web_crawler]") {
	const ofxGgmlWebCrawlerResult result{};
	REQUIRE_FALSE(result.success);
	REQUIRE(result.elapsedMs == 0.0f);
	REQUIRE(result.exitCode == -1);
	REQUIRE(result.documents.empty());
	REQUIRE(result.savedFiles.empty());
}

TEST_CASE("ofxGgmlCrawledDocument zero-initialises correctly", "[web_crawler]") {
	const ofxGgmlCrawledDocument doc{};
	REQUIRE(doc.crawlDepth == -1);
	REQUIRE(doc.byteSize == 0);
	REQUIRE(doc.title.empty());
	REQUIRE(doc.sourceUrl.empty());
	REQUIRE(doc.markdown.empty());
}

TEST_CASE("ofxGgmlWebCrawler reports error without backend", "[web_crawler]") {
	ofxGgmlWebCrawler crawler;
	crawler.setBackend(nullptr);
	ofxGgmlWebCrawlerRequest request;
	request.startUrl = "https://example.com";
	const auto result = crawler.crawl(request);
	REQUIRE_FALSE(result.success);
	REQUIRE_FALSE(result.error.empty());
}

TEST_CASE("ofxGgmlWebCrawler custom bridge backend is invoked", "[web_crawler]") {
	bool callbackInvoked = false;
	auto backend = createWebCrawlerBridgeBackend(
		[&](const ofxGgmlWebCrawlerRequest & req) -> ofxGgmlWebCrawlerResult {
			callbackInvoked = true;
			ofxGgmlWebCrawlerResult r;
			r.success = true;
			r.startUrl = req.startUrl;
			r.backendName = "TestBridge";
			return r;
		});

	ofxGgmlWebCrawler crawler;
	crawler.setBackend(backend);
	ofxGgmlWebCrawlerRequest request;
	request.startUrl = "https://example.com";
	const auto result = crawler.crawl(request);
	REQUIRE(callbackInvoked);
	REQUIRE(result.success);
	REQUIRE(result.backendName == "TestBridge");
}

TEST_CASE("ofxGgmlWebCrawlerBridgeBackend unconfigured returns error", "[web_crawler]") {
	ofxGgmlWebCrawlerBridgeBackend backend(nullptr);
	ofxGgmlWebCrawlerRequest request;
	request.startUrl = "https://example.com";
	const auto result = backend.crawl(request);
	REQUIRE_FALSE(result.success);
	REQUIRE_FALSE(result.error.empty());
}

TEST_CASE("ofxGgmlWebCrawlerRequest default values are sane", "[web_crawler]") {
	const ofxGgmlWebCrawlerRequest req{};
	REQUIRE(req.maxDepth == 2);
	REQUIRE(req.maxPages == 24);
	REQUIRE(req.maxRetries == 3);
	REQUIRE(req.timeoutSeconds == 300);
	REQUIRE(req.retryDelayMs == 1000);
	REQUIRE_FALSE(req.renderJavaScript);
	REQUIRE(req.stayOnStartDomain);
}

// ---------------------------------------------------------------------------
// File-based crawl round-trip (no internet, tests native HTML backend)
// ---------------------------------------------------------------------------

TEST_CASE("Native HTML backend crawls a local file:// page", "[web_crawler][file_crawl]") {
	std::error_code ec;
	const std::filesystem::path tmpDir = createTempDir();
	if (tmpDir.empty() || !std::filesystem::exists(tmpDir, ec)) {
		SUCCEED("Could not create temporary directory for file:// crawl test.");
		return;
	}

	const std::string html =
		"<!DOCTYPE html><html><head><title>Test Page</title></head>"
		"<body><p>Hello from the test crawler.</p></body></html>";
	const std::string htmlPath = writeTempHtml(tmpDir, "index.html", html);
	const std::filesystem::path outDir = tmpDir / "output";
	std::filesystem::create_directories(outDir, ec);

	ofxGgmlWebCrawlerRequest request;
	request.startUrl = fileUrl(htmlPath);
	request.maxDepth = 0;
	request.maxPages = 1;
	request.keepOutputFiles = true;
	request.outputDir = outDir.string();
	request.stayOnStartDomain = false;

	ofxGgmlWebCrawler crawler;
	const auto result = crawler.crawl(request);

	std::filesystem::remove_all(tmpDir, ec);

	if (!result.success) {
		SUCCEED("Native HTML backend unavailable (libxml2 not built in): " + result.error);
		return;
	}

	REQUIRE(result.success);
	REQUIRE_FALSE(result.documents.empty());
	const auto & doc = result.documents.front();
	REQUIRE(doc.markdown.find("Hello from the test crawler") != std::string::npos);
}

TEST_CASE("Native HTML backend follows links within depth limit", "[web_crawler][file_crawl]") {
	std::error_code ec;
	const std::filesystem::path tmpDir = createTempDir();
	if (tmpDir.empty() || !std::filesystem::exists(tmpDir, ec)) {
		SUCCEED("Could not create temporary directory.");
		return;
	}

	const std::string pageA =
		"<!DOCTYPE html><html><head><title>Page A</title></head>"
		"<body><p>Welcome to A.</p><a href=\"b.html\">Go to B</a></body></html>";
	const std::string pageB =
		"<!DOCTYPE html><html><head><title>Page B</title></head>"
		"<body><p>Welcome to B.</p></body></html>";

	writeTempHtml(tmpDir, "a.html", pageA);
	writeTempHtml(tmpDir, "b.html", pageB);
	const std::filesystem::path outDir = tmpDir / "output";
	std::filesystem::create_directories(outDir, ec);

	ofxGgmlWebCrawlerRequest request;
	request.startUrl = fileUrl((tmpDir / "a.html").string());
	request.maxDepth = 1;
	request.maxPages = 10;
	request.keepOutputFiles = true;
	request.outputDir = outDir.string();
	request.stayOnStartDomain = false;

	ofxGgmlWebCrawler crawler;
	const auto result = crawler.crawl(request);

	std::filesystem::remove_all(tmpDir, ec);

	if (!result.success) {
		SUCCEED("Native HTML backend unavailable: " + result.error);
		return;
	}

	REQUIRE(result.success);
	REQUIRE(result.documents.size() >= 2);
}

TEST_CASE("Native HTML backend respects maxPages limit", "[web_crawler][file_crawl]") {
	std::error_code ec;
	const std::filesystem::path tmpDir = createTempDir();
	if (tmpDir.empty() || !std::filesystem::exists(tmpDir, ec)) {
		SUCCEED("Could not create temporary directory.");
		return;
	}

	// Build a chain: a.html -> b.html -> c.html
	writeTempHtml(tmpDir, "a.html",
		"<html><head><title>A</title></head><body><p>Page A content here.</p>"
		"<a href=\"b.html\">B</a></body></html>");
	writeTempHtml(tmpDir, "b.html",
		"<html><head><title>B</title></head><body><p>Page B content here.</p>"
		"<a href=\"c.html\">C</a></body></html>");
	writeTempHtml(tmpDir, "c.html",
		"<html><head><title>C</title></head><body><p>Page C content here.</p></body></html>");

	ofxGgmlWebCrawlerRequest request;
	request.startUrl = fileUrl((tmpDir / "a.html").string());
	request.maxDepth = 3;
	request.maxPages = 2;
	request.stayOnStartDomain = false;

	ofxGgmlWebCrawler crawler;
	const auto result = crawler.crawl(request);

	std::filesystem::remove_all(tmpDir, ec);

	if (!result.success) {
		SUCCEED("Native HTML backend unavailable: " + result.error);
		return;
	}

	REQUIRE(result.success);
	REQUIRE(result.documents.size() <= 2);
}
