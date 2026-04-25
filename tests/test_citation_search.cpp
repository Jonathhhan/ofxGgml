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
