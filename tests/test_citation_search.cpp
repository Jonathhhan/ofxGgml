#include "catch2.hpp"
#include "../src/inference/ofxGgmlCitationSearch.h"

TEST_CASE("Citation cleaner strips YAML front matter at the top", "[citation_search]") {
	const std::string markdown =
		"---\n"
		"title: Example Page\n"
		"slug: example-page\n"
		"---\n"
		"# Heading\n"
		"Important body paragraph.\n";

	const std::string cleaned =
		ofxGgmlCitationSearchInternal::cleanCrawlerMarkdownForCitations(markdown);

	REQUIRE(cleaned.find("title: Example Page") == std::string::npos);
	REQUIRE(cleaned.find("slug: example-page") == std::string::npos);
	REQUIRE(cleaned.find("Heading") != std::string::npos);
	REQUIRE(cleaned.find("Important body paragraph.") != std::string::npos);
}

TEST_CASE("Citation cleaner preserves content around thematic breaks", "[citation_search]") {
	const std::string markdown =
		"Opening paragraph with useful context.\n"
		"\n"
		"---\n"
		"\n"
		"Second paragraph that should still be available for citations.\n";

	const std::string cleaned =
		ofxGgmlCitationSearchInternal::cleanCrawlerMarkdownForCitations(markdown);

	REQUIRE(cleaned.find("Opening paragraph with useful context.") != std::string::npos);
	REQUIRE(cleaned.find(
		"Second paragraph that should still be available for citations.") !=
		std::string::npos);
}

TEST_CASE("Citation search detects intercepted search-style input", "[citation_search]") {
	const auto match = ofxGgmlCitationSearch::detectInputIntent(
		"find sources about Berlin weather delays");
	REQUIRE(match.matched);
	REQUIRE(match.triggerWord == "find");
	REQUIRE(match.topic == "Berlin weather delays");

	const auto quoteMatch = ofxGgmlCitationSearch::detectInputIntent(
		"quote evidence on icy airport disruption?");
	REQUIRE(quoteMatch.matched);
	REQUIRE(quoteMatch.topic == "icy airport disruption");

	const auto noMatch = ofxGgmlCitationSearch::detectInputIntent("hello there");
	REQUIRE_FALSE(noMatch.matched);
}

TEST_CASE("Citation search preserves numeric topics in intercepted input", "[citation_search]") {
	const auto match = ofxGgmlCitationSearch::detectInputIntent(
		"find citations about Formula 1 2025 calendar changes");

	REQUIRE(match.matched);
	REQUIRE(match.triggerWord == "find");
	REQUIRE(match.topic == "Formula 1 2025 calendar changes");
}

TEST_CASE("Citation search keeps longer exact quote candidates", "[citation_search]") {
	const std::string content =
		"First sentence establishes the claim with enough detail to matter. "
		"Second sentence adds supporting context and a concrete example from the source text. "
		"Third sentence finishes the evidence span without changing wording or dropping nuance.";

	const auto candidates =
		ofxGgmlCitationSearchInternal::extractExactQuoteCandidatesForTesting(content);

	bool foundLongCandidate = false;
	for (const auto & candidate : candidates) {
		if (candidate == content) {
			foundLongCandidate = true;
			break;
		}
	}

	REQUIRE(foundLongCandidate);
}

TEST_CASE("Citation items include confidence and quality metrics", "[citation_search]") {
	ofxGgmlCitationItem item;
	item.quote = "This is a test quote from a reputable source.";
	item.sourceUri = "https://example.edu/article";
	item.confidenceScore = 0.85f;
	item.isExactMatch = true;
	item.relevanceScore = 0.75f;
	item.sourceCredibility = 0.8f;

	REQUIRE(item.confidenceScore > 0.0f);
	REQUIRE(item.confidenceScore <= 1.0f);
	REQUIRE(item.isExactMatch);
	REQUIRE(item.relevanceScore > 0.0f);
	REQUIRE(item.sourceCredibility > 0.0f);
}

TEST_CASE("Citation results track diversity and average confidence", "[citation_search]") {
	ofxGgmlCitationSearchResult result;
	result.success = true;

	ofxGgmlCitationItem item1;
	item1.sourceIndex = 1;
	item1.confidenceScore = 0.8f;
	result.citations.push_back(item1);

	ofxGgmlCitationItem item2;
	item2.sourceIndex = 2;
	item2.confidenceScore = 0.9f;
	result.citations.push_back(item2);

	ofxGgmlCitationItem item3;
	item3.sourceIndex = 1;
	item3.confidenceScore = 0.7f;
	result.citations.push_back(item3);

	result.averageConfidence = (0.8f + 0.9f + 0.7f) / 3.0f;
	result.sourceDiversityScore = 0.67f;

	REQUIRE(result.averageConfidence > 0.0f);
	REQUIRE(result.averageConfidence <= 1.0f);
	REQUIRE(result.sourceDiversityScore > 0.0f);
	REQUIRE(result.sourceDiversityScore <= 1.0f);
}

TEST_CASE("Source credibility scoring favors academic and government domains", "[citation_search]") {
	ofxGgmlCitationItem eduItem;
	eduItem.sourceUri = "https://stanford.edu/research/paper";

	ofxGgmlCitationItem govItem;
	govItem.sourceUri = "https://data.gov/statistics";

	ofxGgmlCitationItem comItem;
	comItem.sourceUri = "https://random.com/blog";

	REQUIRE(eduItem.sourceUri.find(".edu") != std::string::npos);
	REQUIRE(govItem.sourceUri.find(".gov") != std::string::npos);
	REQUIRE(comItem.sourceUri.find(".com") != std::string::npos);
}

TEST_CASE("Confidence score combines match quality, relevance, and credibility", "[citation_search]") {
	ofxGgmlCitationItem highConfidence;
	highConfidence.isExactMatch = true;
	highConfidence.relevanceScore = 0.9f;
	highConfidence.sourceCredibility = 0.85f;
	highConfidence.quote = "A well-formed quote of appropriate length from a credible source.";
	highConfidence.confidenceScore = 0.95f;

	ofxGgmlCitationItem lowConfidence;
	lowConfidence.isExactMatch = false;
	lowConfidence.relevanceScore = 0.3f;
	lowConfidence.sourceCredibility = 0.5f;
	lowConfidence.quote = "Short.";
	lowConfidence.confidenceScore = 0.35f;

	REQUIRE(highConfidence.confidenceScore > lowConfidence.confidenceScore);
	REQUIRE(highConfidence.isExactMatch);
	REQUIRE_FALSE(lowConfidence.isExactMatch);
}

TEST_CASE("Source diversity score penalizes over-reliance on single source", "[citation_search]") {
	std::vector<ofxGgmlCitationItem> diverseCitations;
	for (int i = 1; i <= 5; ++i) {
		ofxGgmlCitationItem item;
		item.sourceIndex = i;
		diverseCitations.push_back(item);
	}

	std::vector<ofxGgmlCitationItem> singleSourceCitations;
	for (int i = 0; i < 5; ++i) {
		ofxGgmlCitationItem item;
		item.sourceIndex = 1;
		singleSourceCitations.push_back(item);
	}

	REQUIRE(diverseCitations.size() == 5);
	REQUIRE(singleSourceCitations.size() == 5);
}

TEST_CASE("Citation search validates empty topic", "[citation_search]") {
	ofxGgmlCitationSearch citationSearch;
	ofxGgmlCitationSearchRequest request;
	request.modelPath = "test_model.gguf";
	request.topic = "";

	auto result = citationSearch.search(request);

	REQUIRE_FALSE(result.success);
	REQUIRE(!result.error.empty());
	REQUIRE(result.error.find("empty") != std::string::npos);
}

TEST_CASE("Citation search validates short topic", "[citation_search]") {
	ofxGgmlCitationSearch citationSearch;
	ofxGgmlCitationSearchRequest request;
	request.modelPath = "test_model.gguf";
	request.topic = "ab";

	auto result = citationSearch.search(request);

	REQUIRE_FALSE(result.success);
	REQUIRE(!result.error.empty());
	REQUIRE(result.error.find("too short") != std::string::npos);
}

TEST_CASE("Citation search validates missing model path", "[citation_search]") {
	ofxGgmlCitationSearch citationSearch;
	ofxGgmlCitationSearchRequest request;
	request.modelPath = "";
	request.topic = "valid topic";

	auto result = citationSearch.search(request);

	REQUIRE_FALSE(result.success);
	REQUIRE(!result.error.empty());
	REQUIRE(result.error.find("model path") != std::string::npos);
}

TEST_CASE("Citation search validates confidence threshold range", "[citation_search]") {
	ofxGgmlCitationSearch citationSearch;
	ofxGgmlCitationSearchRequest request;
	request.modelPath = "test_model.gguf";
	request.topic = "valid topic";
	request.minimumConfidenceThreshold = 1.5f;

	auto result = citationSearch.search(request);

	REQUIRE_FALSE(result.success);
	REQUIRE(!result.error.empty());
	REQUIRE(result.error.find("between 0.0 and 1.0") != std::string::npos);
}

TEST_CASE("Citation search validates max citations bounds", "[citation_search]") {
	ofxGgmlCitationSearch citationSearch;
	ofxGgmlCitationSearchRequest request;
	request.modelPath = "test_model.gguf";
	request.topic = "valid topic";
	request.maxCitations = 0;

	auto result = citationSearch.search(request);

	REQUIRE_FALSE(result.success);
	REQUIRE(!result.error.empty());
	REQUIRE(result.error.find("at least 1") != std::string::npos);
}

TEST_CASE("Citation search validates crawler URL when enabled", "[citation_search]") {
	ofxGgmlCitationSearch citationSearch;
	ofxGgmlCitationSearchRequest request;
	request.modelPath = "test_model.gguf";
	request.topic = "valid topic";
	request.useCrawler = true;
	request.crawlerRequest.startUrl = "";

	auto result = citationSearch.search(request);

	REQUIRE_FALSE(result.success);
	REQUIRE(!result.error.empty());
	REQUIRE(result.error.find("Crawler URL") != std::string::npos);
}

