#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

enum class ofxGgmlImageGenerationTask {
	TextToImage = 0,
	ImageToImage,
	InstructImage,
	Variation,
	Restyle,
	Inpaint,
	Upscale
};

enum class ofxGgmlImageSelectionMode {
	KeepOrder = 0,
	Rerank,
	BestOnly
};

enum class ofxGgmlImageGenerationErrorType {
	None = 0,
	ConfigurationError,
	ModelLoadError,
	ValidationError,
	GenerationError,
	ResourceError,
	TimeoutError,
	BackendError
};

struct ofxGgmlImageGenerationModelProfile {
	std::string name;
	std::string architecture;
	std::string modelRepoHint;
	std::string modelFileHint;
	std::string modelPath;
	std::string vaePath;
	std::string clipLPath;
	std::string clipGPath;
	bool supportsImageToImage = true;
	bool supportsInstructImage = true;
	bool supportsVariation = true;
	bool supportsRestyle = true;
	bool supportsInpaint = false;
	bool supportsUpscale = false;
};

struct ofxGgmlGeneratedImage {
	std::string path;
	int width = 0;
	int height = 0;
	int seed = -1;
	int index = 0;
	int sourceIndex = 0;
	bool selected = false;
	float score = 0.0f;
	std::string scorer;
	std::string scoreSummary;
};

struct ofxGgmlImageGenerationProgress {
	float progress = 0.0f;
	int currentStep = 0;
	int totalSteps = 0;
	int currentBatch = 0;
	int totalBatches = 0;
	std::string currentPhase;
	float elapsedMs = 0.0f;
	bool cancelled = false;
};

using ofxGgmlImageGenerationProgressCallback =
	std::function<bool(const ofxGgmlImageGenerationProgress &)>;

struct ofxGgmlImageGenerationCapabilities {
	bool supportsTextToImage = false;
	bool supportsImageToImage = false;
	bool supportsInstructImage = false;
	bool supportsVariation = false;
	bool supportsRestyle = false;
	bool supportsInpaint = false;
	bool supportsUpscale = false;
	bool supportsControlNet = false;
	bool supportsLoRA = false;
	bool supportsProgressCallbacks = false;
	bool supportsBatchGeneration = false;
	int maxBatchSize = 1;
	std::vector<std::string> supportedSamplers;
	std::string modelArchitecture;
	std::string backendVersion;
};

struct ofxGgmlImageGenerationRequest {
	ofxGgmlImageGenerationTask task = ofxGgmlImageGenerationTask::TextToImage;
	ofxGgmlImageSelectionMode selectionMode = ofxGgmlImageSelectionMode::KeepOrder;
	std::string prompt;
	std::string instruction;
	std::string negativePrompt;
	std::string rankingPrompt;
	std::string modelPath;
	std::string vaePath;
	std::string clipLPath;
	std::string clipGPath;
	std::string t5xxlPath;
	std::string initImagePath;
	std::string maskImagePath;
	std::string controlImagePath;
	std::string outputDir;
	std::string outputPrefix;
	std::string sampler;
	int width = 1024;
	int height = 1024;
	int steps = 20;
	int batchCount = 1;
	int seed = -1;
	float cfgScale = 7.0f;
	float strength = 1.0f;
	float controlStrength = 1.0f;
	bool normalizeClipEmbeddings = true;
	bool saveMetadata = true;
	ofxGgmlImageGenerationProgressCallback progressCallback;
};

struct ofxGgmlImageGenerationDiagnostics {
	float modelLoadTimeMs = 0.0f;
	float generationTimeMs = 0.0f;
	float postProcessTimeMs = 0.0f;
	size_t peakMemoryMB = 0;
	size_t contextReloads = 0;
	std::string modelArchitecture;
	std::string backendVersion;
	std::vector<std::pair<std::string, std::string>> timingBreakdown;
};

struct ofxGgmlImageGenerationResult {
	bool success = false;
	float elapsedMs = 0.0f;
	std::string error;
	ofxGgmlImageGenerationErrorType errorType = ofxGgmlImageGenerationErrorType::None;
	std::string backendName;
	std::string rawOutput;
	std::vector<ofxGgmlGeneratedImage> images;
	std::vector<std::pair<std::string, std::string>> metadata;
	ofxGgmlImageGenerationDiagnostics diagnostics;
};

class ofxGgmlImageGenerationBackend {
public:
	virtual ~ofxGgmlImageGenerationBackend() = default;
	virtual std::string backendName() const = 0;
	virtual ofxGgmlImageGenerationResult generate(
		const ofxGgmlImageGenerationRequest & request) const = 0;
	virtual ofxGgmlImageGenerationCapabilities getCapabilities() const {
		return {};
	}
};

class ofxGgmlStableDiffusionBridgeBackend : public ofxGgmlImageGenerationBackend {
public:
	using GenerateFunction = std::function<ofxGgmlImageGenerationResult(
		const ofxGgmlImageGenerationRequest &)>;
	using GetCapabilitiesFunction = std::function<ofxGgmlImageGenerationCapabilities()>;

	explicit ofxGgmlStableDiffusionBridgeBackend(
		GenerateFunction generateFunction = {},
		std::string displayName = "ofxStableDiffusion");

	void setGenerateFunction(GenerateFunction generateFunction);
	void setGetCapabilitiesFunction(GetCapabilitiesFunction getCapabilitiesFunction);
	bool isConfigured() const;

	std::string backendName() const override;
	ofxGgmlImageGenerationResult generate(
		const ofxGgmlImageGenerationRequest & request) const override;
	ofxGgmlImageGenerationCapabilities getCapabilities() const override;

private:
	GenerateFunction m_generateFunction;
	GetCapabilitiesFunction m_getCapabilitiesFunction;
	std::string m_displayName;
};

class ofxGgmlDiffusionInference {
public:
	ofxGgmlDiffusionInference();

	static std::vector<ofxGgmlImageGenerationModelProfile> defaultProfiles();
	static const char * taskLabel(ofxGgmlImageGenerationTask task);
	static const char * selectionModeLabel(ofxGgmlImageSelectionMode mode);
	static std::shared_ptr<ofxGgmlImageGenerationBackend>
		createStableDiffusionBridgeBackend(
			ofxGgmlStableDiffusionBridgeBackend::GenerateFunction generateFunction = {},
			const std::string & displayName = "ofxStableDiffusion");

	void setBackend(std::shared_ptr<ofxGgmlImageGenerationBackend> backend);
	std::shared_ptr<ofxGgmlImageGenerationBackend> getBackend() const;

	ofxGgmlImageGenerationResult generate(
		const ofxGgmlImageGenerationRequest & request) const;

	ofxGgmlImageGenerationCapabilities getCapabilities() const;

	static const char * errorTypeLabel(ofxGgmlImageGenerationErrorType errorType);

private:
	std::shared_ptr<ofxGgmlImageGenerationBackend> m_backend;
};
