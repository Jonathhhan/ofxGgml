#include "catch2.hpp"
#include "../src/ofxGgml.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace {

std::filesystem::path makeAssistantEvalDir(const std::string & name) {
	const auto base =
		std::filesystem::temp_directory_path() / "ofxggml_assistant_eval_tests";
	std::filesystem::create_directories(base);
	const auto dir = base / (name + "_" + std::to_string(std::rand()));
	std::filesystem::create_directories(dir);
	return dir;
}

} // namespace

TEST_CASE("Assistant eval: symbol retrieval prioritizes relevant definitions", "[eval]") {
	const auto sourceDir = makeAssistantEvalDir("symbols");
	{
		std::ofstream out(sourceDir / "renderer.cpp");
		out << "class Renderer { public: void renderFrame(); };\n";
		out << "void Renderer::renderFrame() {}\n";
	}
	{
		std::ofstream out(sourceDir / "audio.cpp");
		out << "void mixAudio() {}\n";
	}
	{
		std::ofstream out(sourceDir / "app.cpp");
		out << "int main() { Renderer renderer; renderer.renderFrame(); }\n";
	}

	ofxGgmlScriptSource scriptSource;
	REQUIRE(scriptSource.setLocalFolder(sourceDir.string()));

	ofxGgmlCodeAssistant assistant;
	ofxGgmlCodeAssistantContext context;
	context.scriptSource = &scriptSource;
	context.maxSymbols = 3;

	const auto symbols = assistant.retrieveSymbols("render frame timing", context);
	REQUIRE_FALSE(symbols.empty());
	REQUIRE(symbols.front().name.find("render") != std::string::npos);

	ofxGgmlCodeAssistantSymbolQuery query;
	query.query = "renderFrame callers";
	query.targetSymbols = {"renderFrame"};
	query.includeCallers = true;
	query.maxDefinitions = 2;
	query.maxReferences = 4;
	const auto symbolContext = assistant.buildSymbolContext(query, context);
	REQUIRE_FALSE(symbolContext.definitions.empty());
	REQUIRE_FALSE(symbolContext.relatedReferences.empty());
	const std::string referenceKind =
		symbolContext.relatedReferences.front().kind;
	REQUIRE((referenceKind == "caller" || referenceKind == "reference"));
}

TEST_CASE("Assistant eval: structured workspace plans are dry-run safe", "[eval]") {
	const auto root = makeAssistantEvalDir("dryrun");
	std::filesystem::create_directories(root / "src");

	ofxGgmlWorkspaceAssistant assistant;
	std::vector<ofxGgmlCodeAssistantPatchOperation> operations;

	ofxGgmlCodeAssistantPatchOperation writeOp;
	writeOp.kind = ofxGgmlCodeAssistantPatchKind::WriteFile;
	writeOp.filePath = "src/example.txt";
	writeOp.summary = "Create example file";
	writeOp.content = "draft";
	operations.push_back(writeOp);

	const auto applyResult = assistant.applyPatchOperations(
		operations,
		root.string(),
		{},
		true);
	REQUIRE(applyResult.success);
	REQUIRE_FALSE(std::filesystem::exists(root / "src" / "example.txt"));
	REQUIRE(applyResult.messages.front().find("[dry-run]") != std::string::npos);
	REQUIRE(applyResult.unifiedDiffPreview.find("+++ b/src/example.txt") != std::string::npos);
}
