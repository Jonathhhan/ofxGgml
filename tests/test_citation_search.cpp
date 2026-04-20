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
