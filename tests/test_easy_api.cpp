#include "catch2.hpp"
#include "../src/ofxGgml.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace {

std::filesystem::path makeEasyApiTestDir(const std::string & name) {
	const auto base = std::filesystem::temp_directory_path() / "ofxggml_easy_api_tests";
	std::filesystem::create_directories(base);
	const auto dir = base / (name + "_" + std::to_string(std::rand()));
	std::filesystem::create_directories(dir);
	return dir;
}

std::string createEasyApiDummyModel() {
	const auto dir = makeEasyApiTestDir("model");
	const auto model = dir / "dummy.gguf";
	std::ofstream out(model);
	out << "dummy-model";
	return model.string();
}

std::string createEasyApiExecutable(const std::string & outputLine) {
	const auto dir = makeEasyApiTestDir("exec");
#ifdef _WIN32
	const auto exe = dir / "fake_easy_api.bat";
	const auto outputFile = dir / "fake_easy_api_output.txt";
	{
		std::ofstream output(outputFile, std::ios::binary);
		output << outputLine;
	}
	std::ofstream out(exe);
	out
		<< "@echo off\r\n"
		<< "type \"%~dp0fake_easy_api_output.txt\"\r\n";
#else
	const auto exe = dir / "fake_easy_api.sh";
	const auto outputFile = dir / "fake_easy_api_output.txt";
	{
		std::ofstream output(outputFile, std::ios::binary);
		output << outputLine;
	}
	std::ofstream out(exe);
	out << "#!/usr/bin/env bash\ncat \"$(dirname \"$0\")/fake_easy_api_output.txt\"\n";
	out.close();
	chmod(exe.c_str(), 0755);
#endif
	return exe.string();
}

std::string createFakeMojoCrawlerExecutable() {
	const auto dir = makeEasyApiTestDir("mojo");
#ifdef _WIN32
	const auto exe = dir / "fake_mojo.bat";
	std::ofstream out(exe);
	out
		<< "@echo off\r\n"
		<< "setlocal\r\n"
		<< "set OUTPUT=%4\r\n"
		<< "if not exist \"%OUTPUT%\" mkdir \"%OUTPUT%\"\r\n"
		<< "(\r\n"
		<< "echo ---\r\n"
		<< "echo title: Allowed Doc\r\n"
		<< "echo source_url: https://allowed.example/article\r\n"
		<< "echo ---\r\n"
		<< "echo # Allowed Doc\r\n"
		<< "echo Kept content.\r\n"
		<< ") > \"%OUTPUT%\\allowed.md\"\r\n"
		<< "(\r\n"
		<< "echo ---\r\n"
		<< "echo title: Blocked Doc\r\n"
		<< "echo source_url: https://blocked.example/post\r\n"
		<< "echo ---\r\n"
		<< "echo # Blocked Doc\r\n"
		<< "echo Removed content.\r\n"
		<< ") > \"%OUTPUT%\\blocked.md\"\r\n";
#else
	const auto exe = dir / "fake_mojo.sh";
	std::ofstream out(exe);
	out
		<< "#!/usr/bin/env bash\n"
		<< "set -euo pipefail\n"
		<< "OUTPUT=\"$4\"\n"
		<< "mkdir -p \"$OUTPUT\"\n"
		<< "cat > \"$OUTPUT/allowed.md\" <<'EOF'\n"
		<< "---\n"
		<< "title: Allowed Doc\n"
		<< "source_url: https://allowed.example/article\n"
		<< "---\n"
		<< "# Allowed Doc\n"
		<< "Kept content.\n"
		<< "EOF\n"
		<< "cat > \"$OUTPUT/blocked.md\" <<'EOF'\n"
		<< "---\n"
		<< "title: Blocked Doc\n"
		<< "source_url: https://blocked.example/post\n"
		<< "---\n"
		<< "# Blocked Doc\n"
		<< "Removed content.\n"
		<< "EOF\n";
	out.close();
	chmod(exe.c_str(), 0755);
#endif
	return exe.string();
}

std::string encodeFileUrlComponent(const std::string & text) {
	std::ostringstream encoded;
	for (const unsigned char ch : text) {
		if (std::isalnum(ch) || ch == '/' || ch == '-' || ch == '_' || ch == '.' || ch == '~' || ch == ':') {
			encoded << static_cast<char>(ch);
		} else {
			encoded << '%' << std::uppercase << std::hex
				<< std::setw(2) << std::setfill('0') << static_cast<int>(ch)
				<< std::nouppercase << std::dec;
		}
	}
	return encoded.str();
}

std::string makeFileUrl(const std::filesystem::path & path) {
	const std::string generic = path.lexically_normal().generic_string();
#ifdef _WIN32
	return "file:///" + encodeFileUrlComponent(generic);
#else
	return "file://" + encodeFileUrlComponent(generic);
#endif
}

class FakeEasySpeechBackend final : public ofxGgmlSpeechBackend {
public:
	std::string backendName() const override {
		return "FakeEasySpeech";
	}

	ofxGgmlSpeechResult transcribe(const ofxGgmlSpeechRequest & request) const override {
		ofxGgmlSpeechResult result;
		result.success = true;
		result.backendName = backendName();
		result.text = request.task == ofxGgmlSpeechTask::Translate
			? "translated speech"
			: "transcribed speech";
		return result;
	}
};

class FakeEasyWebCrawlerBackend final : public ofxGgmlWebCrawlerBackend {
public:
	std::string backendName() const override {
		return "FakeEasyCrawler";
	}

	ofxGgmlWebCrawlerResult crawl(const ofxGgmlWebCrawlerRequest & request) const override {
		ofxGgmlWebCrawlerResult result;
		result.success = true;
		result.backendName = backendName();
		result.startUrl = request.startUrl;
		result.outputDir = request.outputDir;
		result.documents.push_back({
			"Crawled Source",
			request.startUrl,
			{},
			"Example markdown about Berlin weather and icy conditions. "
			"Icy weather halted flights.",
			0,
			52
		});
		return result;
	}
};

} // namespace

TEST_CASE("Easy API reports missing configuration cleanly", "[easy_api]") {
	ofxGgmlEasy easy;

	const auto textResult = easy.complete("Hello");
	REQUIRE_FALSE(textResult.success);
	REQUIRE(textResult.error.find("configureText") != std::string::npos);

	const auto visionResult = easy.describeImage("image.png");
	REQUIRE_FALSE(visionResult.success);
	REQUIRE(visionResult.error.find("configureVision") != std::string::npos);

	const auto speechResult = easy.transcribeAudio("clip.wav");
	REQUIRE_FALSE(speechResult.success);
	REQUIRE(speechResult.error.find("configureSpeech") != std::string::npos);
}

TEST_CASE("Easy API wraps common text workflows", "[easy_api]") {
	const std::string modelPath = createEasyApiDummyModel();
	const std::string exePath = createEasyApiExecutable("easy-api-ok");

	ofxGgmlEasy easy;
	ofxGgmlEasyTextConfig textConfig;
	textConfig.modelPath = modelPath;
	textConfig.completionExecutable = exePath;
	easy.configureText(textConfig);

	const auto completion = easy.complete("Say hello.");
	REQUIRE(completion.success);
	REQUIRE_FALSE(completion.text.empty());

	const auto summary = easy.summarize("This is a longer paragraph that needs a summary.");
	REQUIRE(summary.inference.success);
	REQUIRE(summary.prepared.label == "Summarize text.");

	const auto translation = easy.translate("Guten Morgen", "English", "German");
	REQUIRE(translation.inference.success);
	REQUIRE(translation.prepared.prompt.find("Translate the following text from German to English.") != std::string::npos);

	const auto chat = easy.chat("How are you?", "German");
	REQUIRE(chat.inference.success);
	REQUIRE(chat.prepared.prompt.find("How are you?") != std::string::npos);

	const std::string musicPromptExePath = createEasyApiExecutable(
		"Music prompt: cinematic synthwave soundtrack, neon pulses, reflective pacing, instrumental");
	textConfig.completionExecutable = musicPromptExePath;
	easy.configureText(textConfig);
	const auto imageToMusic = easy.generateImageToMusicPrompt(
		"Rain-soaked neon alley with a lone figure.",
		"cinematic synthwave soundtrack",
		"analog synth bass, glassy pads",
		24,
		true);
	REQUIRE(imageToMusic.success);
	REQUIRE(imageToMusic.musicPrompt.find("neon pulses") != std::string::npos);

	const std::string abcExePath = createEasyApiExecutable(
		"X:1\nT:Night Theme\nM:4/4\nL:1/8\nQ:1/4=92\nK:Cm\n|: C2 G2 A2 G2 | E2 D2 C4 :|");
	textConfig.completionExecutable = abcExePath;
	easy.configureText(textConfig);
	const auto notation = easy.generateMusicNotation(
		"nocturnal city montage",
		"Night Theme",
		"cinematic synth soundtrack",
		16,
		"Cm");
	REQUIRE(notation.success);
	REQUIRE(notation.validation.valid);
	REQUIRE(notation.abcNotation.find("T:Night Theme") != std::string::npos);

	const std::string milkExePath = createEasyApiExecutable("[preset00]\nzoom=1.02\nfRating=3.0");
	textConfig.completionExecutable = milkExePath;
	easy.configureText(textConfig);
	const auto preset = easy.generateMilkDropPreset(
		"Neon tunnel with pulsing geometry.",
		"Geometric",
		0.5f);
	REQUIRE(preset.success);
	REQUIRE(preset.presetText.find("[preset00]") == 0);
	REQUIRE(preset.validation.valid);

	const auto variants = easy.generateMilkDropVariants(
		"Neon tunnel with pulsing geometry.",
		"Geometric",
		0.5f,
		3);
	REQUIRE(variants.success);
	REQUIRE(variants.variants.size() == 3);
	REQUIRE(variants.variants.front().validation.valid);

	const auto validation = easy.validateMilkDropPreset("[preset00]\nzoom=1.02\nfRating=3.0\n");
	REQUIRE(validation.valid);

	const auto repaired = easy.repairMilkDropPreset(
		"[preset00]\nzoom=(1.0\n",
		"General",
		0.25f,
		"Repair the broken parentheses and keep it conservative.");
	REQUIRE(repaired.success);
	REQUIRE(repaired.validation.valid);

	const auto saveDir = makeEasyApiTestDir("milkdrop_save");
	const auto savedPath = easy.saveMilkDropPreset(
		preset.presetText,
		(saveDir / "preset").string());
	REQUIRE_FALSE(savedPath.empty());
	REQUIRE(std::filesystem::exists(savedPath));
}

TEST_CASE("Easy API can reuse a custom speech backend", "[easy_api]") {
	ofxGgmlEasy easy;
	ofxGgmlEasySpeechConfig speechConfig;
	speechConfig.modelPath = "dummy.bin";
	easy.configureSpeech(speechConfig);
	easy.getSpeechInference().setBackend(std::make_shared<FakeEasySpeechBackend>());

	const auto transcript = easy.transcribeAudio("clip.wav");
	REQUIRE(transcript.success);
	REQUIRE(transcript.backendName == "FakeEasySpeech");
	REQUIRE(transcript.text == "transcribed speech");

	const auto translation = easy.translateAudio("clip.wav");
	REQUIRE(translation.success);
	REQUIRE(translation.text == "translated speech");
}

TEST_CASE("Easy API wraps crawler and montage helpers", "[easy_api]") {
	ofxGgmlEasy easy;
	ofxGgmlEasyCrawlerConfig crawlerConfig;
	crawlerConfig.outputDir = makeEasyApiTestDir("crawl").string();
	easy.configureWebCrawler(crawlerConfig);
	easy.getWebCrawler().setBackend(std::make_shared<FakeEasyWebCrawlerBackend>());

	const auto crawlResult = easy.crawlWebsite("https://example.com/docs", 1);
	REQUIRE(crawlResult.success);
	REQUIRE(crawlResult.backendName == "FakeEasyCrawler");
	REQUIRE(crawlResult.documents.size() == 1);

	const auto srtDir = makeEasyApiTestDir("srt");
	const auto srtPath = srtDir / "sample.srt";
	{
		std::ofstream out(srtPath);
		out
			<< "1\n00:00:00,000 --> 00:00:02,000\nBerlin wakes up.\n\n"
			<< "2\n00:00:02,000 --> 00:00:04,000\nTrains move through the city.\n";
	}

	const auto montageResult = easy.planMontageFromSrt(
		srtPath.string(),
		"Build a concise city montage.",
		4,
		0.0,
		true,
		"AX",
		"BERLIN_MONTAGE",
		25);
	REQUIRE(montageResult.success);
	REQUIRE(montageResult.planning.success);
	REQUIRE_FALSE(montageResult.edlText.empty());
	REQUIRE_FALSE(montageResult.srtText.empty());
	REQUIRE(montageResult.previewBundle.montageTrack.cues.size() == montageResult.montageTrack.cues.size());
}

TEST_CASE("Easy API exposes citation and video edit helpers", "[easy_api]") {
	const std::string modelPath = createEasyApiDummyModel();
	const std::string editExePath = createEasyApiExecutable(
		"{\"originalGoal\":\"Make it punchy\",\"sourceSummary\":\"Two city beats.\",\"overallDirection\":\"Energetic recap\",\"pacingStrategy\":\"Fast start\",\"visualStyle\":\"Clean contrast\",\"audioStrategy\":\"Rhythmic\",\"targetDurationSeconds\":12,\"globalNotes\":[\"Keep the opening short.\"],\"assetSuggestions\":[\"Title card\"],\"clips\":[{\"index\":1,\"startSeconds\":0,\"endSeconds\":3,\"purpose\":\"Hook\",\"sourceDescription\":\"Opening shot\",\"treatment\":\"Punch in\",\"transition\":\"Cut\",\"textOverlay\":\"Berlin\"}],\"actions\":[{\"index\":1,\"type\":\"trim\",\"startSeconds\":0,\"endSeconds\":3,\"instruction\":\"Trim to the best opening beat.\",\"rationale\":\"Start strong.\",\"assetHint\":\"\"}]}");
	const std::string citationExePath = createEasyApiExecutable(
		"{\"summary\":\"Cited weather notes.\",\"citations\":[{\"quote\":\"Icy weather halted flights.\",\"sourceIndex\":1,\"note\":\"Airport disruption\"}]}");

	ofxGgmlEasy easy;
	ofxGgmlEasyTextConfig textConfig;
	textConfig.modelPath = modelPath;
	textConfig.completionExecutable = editExePath;
	easy.configureText(textConfig);

	const auto editResult = easy.planVideoEdit(
		"Berlin city footage",
		"Turn this into a short recap.",
		"Opening skyline, then transit, then crowd reaction.",
		12.0,
		4,
		true);
	if (editResult.success) {
		REQUIRE(editResult.planning.success);
		REQUIRE_FALSE(editResult.workflow.steps.empty());
		REQUIRE_FALSE(editResult.editorBrief.empty());
	} else {
		const bool hasExpectedHeadlessError =
			editResult.error.find("JSON parsing is unavailable") != std::string::npos ||
			editResult.error.find("non-JSON output") != std::string::npos ||
			editResult.error.find("did not contain a JSON object") != std::string::npos;
		REQUIRE(hasExpectedHeadlessError);
	}

	textConfig.completionExecutable = citationExePath;
	easy.configureText(textConfig);
	easy.getWebCrawler().setBackend(std::make_shared<FakeEasyWebCrawlerBackend>());
	const auto citationResult = easy.findCitations(
		"Berlin weather",
		{},
		"https://example.com/weather",
		3);
	if (citationResult.success) {
		REQUIRE(citationResult.citations.size() == 1);
		REQUIRE(citationResult.citations.front().quote == "Icy weather halted flights.");
	} else {
		const bool hasExpectedCitationError =
			citationResult.error.find("non-JSON output") != std::string::npos ||
			citationResult.error.find("Inference failed while extracting citations.") != std::string::npos ||
			citationResult.error.find("Crawler did not return any usable markdown documents.") != std::string::npos;
		REQUIRE(hasExpectedCitationError);
	}

	const auto interceptedCitationResult = easy.findCitationsFromInput(
		"find sources about Berlin weather",
		{},
		"https://example.com/weather",
		3);
	if (interceptedCitationResult.success) {
		REQUIRE(interceptedCitationResult.requestedTopic == "Berlin weather");
		REQUIRE(interceptedCitationResult.inputTriggerWord == "find");
		REQUIRE(interceptedCitationResult.queryRewrite.originalTopic == "Berlin weather");
		REQUIRE_FALSE(interceptedCitationResult.queryRewrite.queriesUsed.empty());
		REQUIRE(interceptedCitationResult.citations.size() == 1);
		REQUIRE(interceptedCitationResult.citations.front().quote ==
			"Icy weather halted flights.");
	} else {
		const bool hasExpectedCitationError =
			interceptedCitationResult.error.find("non-JSON output") != std::string::npos ||
			interceptedCitationResult.error.find("Inference failed while extracting citations.") != std::string::npos ||
			interceptedCitationResult.error.find("Crawler did not return any usable markdown documents.") != std::string::npos;
		REQUIRE(hasExpectedCitationError);
	}
}

TEST_CASE("Easy API exposes the coding agent wrapper", "[easy_api][coding_agent]") {
	const std::string modelPath = createEasyApiDummyModel();
	const std::string exePath = createEasyApiExecutable("Planned coding steps.");

	ofxGgmlEasy easy;
	ofxGgmlEasyTextConfig textConfig;
	textConfig.modelPath = modelPath;
	textConfig.completionExecutable = exePath;
	easy.configureText(textConfig);

	ofxGgmlCodingAgentRequest request;
	request.taskLabel = "Plan Script panel improvement.";
	request.assistantRequest.action = ofxGgmlCodeAssistantAction::Ask;
	request.assistantRequest.userInput =
		"Plan how to improve a Script panel without applying code changes.";

	ofxGgmlCodingAgentSettings settings;
	settings.mode = ofxGgmlCodingAgentMode::Plan;
	settings.autoApply = false;
	settings.autoVerify = false;

	const auto result = easy.runCodingAgent(request, {}, settings);
	REQUIRE(result.success);
	REQUIRE(result.readOnly);
	REQUIRE(result.assistantResult.inference.success);
	REQUIRE_FALSE(result.summary.empty());
}

TEST_CASE("Mojo crawler keeps canonical source URLs and filters allowed domains", "[easy_api][crawler]") {
	const std::string fakeMojo = createFakeMojoCrawlerExecutable();

	ofxGgmlWebCrawler crawler;
	crawler.setBackend(std::make_shared<ofxGgmlMojoWebCrawlerBackend>(fakeMojo));

	ofxGgmlWebCrawlerRequest request;
	request.startUrl = "https://allowed.example/root";
	request.keepOutputFiles = false;
	request.allowedDomains = {"allowed.example"};

	const auto result = crawler.crawl(request);
	REQUIRE(result.success);
	REQUIRE(result.documents.size() == 1);
	REQUIRE(result.documents.front().sourceUrl == "https://allowed.example/article");
	REQUIRE(result.documents.front().localPath.empty());

	ofxGgmlWebCrawlerRequest blockedRequest = request;
	blockedRequest.startUrl = "https://blocked.example/root";
	const auto blockedResult = crawler.crawl(blockedRequest);
	REQUIRE_FALSE(blockedResult.success);
	const bool hasAllowedDomainError =
		blockedResult.error.find("allowedDomains") != std::string::npos ||
		blockedResult.error.find("allowed domains") != std::string::npos;
	REQUIRE(hasAllowedDomainError);
}

TEST_CASE("Default crawler uses native HTML parsing for static pages", "[easy_api][crawler]") {
	const auto dir = makeEasyApiTestDir("native_html");
	const auto indexPath = dir / "index.html";
	const auto secondPath = dir / "page2.html";
	{
		std::ofstream out(indexPath);
		out
			<< "<html><head><title>Native Root</title></head><body>"
			<< "<main><h1>Native Root</h1>"
			<< "<p>Berlin weather research starts on the first page.</p>"
			<< "<a href=\"page2.html\">Next</a></main></body></html>";
	}
	{
		std::ofstream out(secondPath);
		out
			<< "<html><head><title>Native Follow Up</title></head><body>"
			<< "<article><p>Second page adds a supporting exact sentence.</p></article>"
			<< "</body></html>";
	}

	ofxGgmlWebCrawler crawler;
	ofxGgmlWebCrawlerRequest request;
	request.startUrl = makeFileUrl(indexPath);
	request.maxDepth = 1;
	request.keepOutputFiles = false;

	const auto result = crawler.crawl(request);
	REQUIRE(result.success);
	REQUIRE(result.backendName == "NativeHtml");
	REQUIRE(
		result.commandOutput.find("Parsed native page") != std::string::npos);
	REQUIRE(
		result.commandOutput.find("libxml2 xmllint HTML") != std::string::npos);
	REQUIRE(result.documents.size() >= 2);
	REQUIRE(result.documents[0].title == "Native Root");
	REQUIRE(
		result.documents[0].markdown.find(
			"Berlin weather research starts on the first page.") != std::string::npos);

	bool foundSecondPage = false;
	for (const auto & document : result.documents) {
		if (document.title == "Native Follow Up") {
			foundSecondPage = true;
			REQUIRE(
				document.markdown.find(
					"Second page adds a supporting exact sentence.") != std::string::npos);
			break;
		}
	}
	REQUIRE(foundSecondPage);
}
