#include "test_harness.h"
#include "../src/ofxGgml.h"

#include <string>

OFXGGML_TEST(document_index_searches_only_explicitly_loaded_text) {
	ofxGgml::DocumentIndex index;
	OFXGGML_REQUIRE(index.addText(
		"architecture.md",
		"The process boundary keeps llama-server and CUDA outside the addon. "));
	OFXGGML_REQUIRE(index.addText(
		"usage.md",
		"ChatSession keeps successful conversation history."));
	OFXGGML_REQUIRE(!index.addText("architecture.md", "duplicate"));

	const auto hits = index.search("process boundary CUDA");
	OFXGGML_REQUIRE(index.documentCount() == 2);
	OFXGGML_REQUIRE(index.chunkCount() == 2);
	OFXGGML_REQUIRE(hits.size() == 1);
	OFXGGML_REQUIRE(hits[0].source == "architecture.md");
	OFXGGML_REQUIRE(hits[0].citation == "[architecture.md#chunk-1]");
}

OFXGGML_TEST(document_index_chunks_large_documents_and_limits_results) {
	ofxGgml::DocumentIndex index;
	std::string text;
	for (int i = 0; i < 150; ++i) text += "Tools search grounded documents. ";
	OFXGGML_REQUIRE(index.addText("long.txt", text));
	OFXGGML_REQUIRE(index.chunkCount() > 1);
	OFXGGML_REQUIRE(index.search("grounded documents", 1).size() == 1);
	OFXGGML_REQUIRE(index.search("", 5).empty());
}
