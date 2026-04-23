#include "ofxGgmlDiffusionInference.h"
#include "ofxGgmlVisionInference.h"
#include "ofxGgmlInference.h"

#include <chrono>
#include <utility>

ofxGgmlStableDiffusionBridgeBackend::ofxGgmlStableDiffusionBridgeBackend(
	GenerateFunction generateFunction,
	std::string displayName)
	: m_generateFunction(std::move(generateFunction))
	, m_displayName(std::move(displayName)) {
}

void ofxGgmlStableDiffusionBridgeBackend::setGenerateFunction(
	GenerateFunction generateFunction) {
	m_generateFunction = std::move(generateFunction);
}

void ofxGgmlStableDiffusionBridgeBackend::setGetCapabilitiesFunction(
	GetCapabilitiesFunction getCapabilitiesFunction) {
	m_getCapabilitiesFunction = std::move(getCapabilitiesFunction);
}

bool ofxGgmlStableDiffusionBridgeBackend::isConfigured() const {
	return static_cast<bool>(m_generateFunction);
}

std::string ofxGgmlStableDiffusionBridgeBackend::backendName() const {
	return m_displayName.empty() ? "ofxStableDiffusion" : m_displayName;
}

ofxGgmlImageGenerationResult ofxGgmlStableDiffusionBridgeBackend::generate(
	const ofxGgmlImageGenerationRequest & request) const {
	ofxGgmlImageGenerationResult result;
	result.backendName = backendName();
	if (!m_generateFunction) {
		result.error =
			"stable diffusion bridge backend is not configured yet. "
			"Attach an ofxStableDiffusion adapter callback before calling generate().";
		result.errorType = ofxGgmlImageGenerationErrorType::ConfigurationError;
		return result;
	}

	const auto started = std::chrono::steady_clock::now();
	result = m_generateFunction(request);
	if (result.backendName.empty()) {
		result.backendName = backendName();
	}
	if (result.elapsedMs <= 0.0f) {
		result.elapsedMs = std::chrono::duration<float, std::milli>(
			std::chrono::steady_clock::now() - started).count();
	}
	return result;
}

ofxGgmlImageGenerationCapabilities
ofxGgmlStableDiffusionBridgeBackend::getCapabilities() const {
	if (m_getCapabilitiesFunction) {
		return m_getCapabilitiesFunction();
	}
	return {};
}

ofxGgmlDiffusionInference::ofxGgmlDiffusionInference()
	: m_backend(createStableDiffusionBridgeBackend()) {
}

std::vector<ofxGgmlImageGenerationModelProfile>
ofxGgmlDiffusionInference::defaultProfiles() {
	return {
		{
			"Stable Diffusion 1.5",
			"SD 1.x",
			"runwayml/stable-diffusion-v1-5",
			"v1-5-pruned-emaonly.safetensors",
			"",
			"",
			"",
			"",
			true,
			true,
			true,
			true,
			true,
			true
		},
		{
			"Stable Diffusion XL",
			"SDXL",
			"stabilityai/stable-diffusion-xl-base-1.0",
			"sd_xl_base_1.0.safetensors",
			"",
			"",
			"",
			"",
			true,
			true,
			true,
			true,
			true,
			true
		},
		{
			"FLUX.1 Schnell",
			"FLUX.1",
			"black-forest-labs/FLUX.1-schnell",
			"flux1-schnell-Q4_0.gguf",
			"",
			"",
			"",
			"",
			true,
			true,
			true,
			true,
			false,
			false
		}
	};
}

const char * ofxGgmlDiffusionInference::taskLabel(
	ofxGgmlImageGenerationTask task) {
	switch (task) {
	case ofxGgmlImageGenerationTask::TextToImage: return "Text to Image";
	case ofxGgmlImageGenerationTask::ImageToImage: return "Image to Image";
	case ofxGgmlImageGenerationTask::InstructImage: return "Instruct Image";
	case ofxGgmlImageGenerationTask::Variation: return "Variation";
	case ofxGgmlImageGenerationTask::Restyle: return "Restyle";
	case ofxGgmlImageGenerationTask::Inpaint: return "Inpaint";
	case ofxGgmlImageGenerationTask::Upscale: return "Upscale";
	}
	return "Text to Image";
}

const char * ofxGgmlDiffusionInference::selectionModeLabel(
	ofxGgmlImageSelectionMode mode) {
	switch (mode) {
	case ofxGgmlImageSelectionMode::Rerank: return "Rerank";
	case ofxGgmlImageSelectionMode::BestOnly: return "Best Only";
	case ofxGgmlImageSelectionMode::KeepOrder:
	default:
		return "Keep Order";
	}
}

std::shared_ptr<ofxGgmlImageGenerationBackend>
ofxGgmlDiffusionInference::createStableDiffusionBridgeBackend(
	ofxGgmlStableDiffusionBridgeBackend::GenerateFunction generateFunction,
	const std::string & displayName) {
	return std::make_shared<ofxGgmlStableDiffusionBridgeBackend>(
		std::move(generateFunction),
		displayName);
}

void ofxGgmlDiffusionInference::setBackend(
	std::shared_ptr<ofxGgmlImageGenerationBackend> backend) {
	m_backend = backend ? std::move(backend) : createStableDiffusionBridgeBackend();
}

std::shared_ptr<ofxGgmlImageGenerationBackend>
ofxGgmlDiffusionInference::getBackend() const {
	return m_backend;
}

ofxGgmlImageGenerationResult ofxGgmlDiffusionInference::generate(
	const ofxGgmlImageGenerationRequest & request) const {
	const auto backend = m_backend ? m_backend : createStableDiffusionBridgeBackend();
	return backend->generate(request);
}

ofxGgmlImageGenerationCapabilities
ofxGgmlDiffusionInference::getCapabilities() const {
	return m_backend ? m_backend->getCapabilities()
		: ofxGgmlImageGenerationCapabilities{};
}

const char * ofxGgmlDiffusionInference::errorTypeLabel(
	ofxGgmlImageGenerationErrorType errorType) {
	switch (errorType) {
	case ofxGgmlImageGenerationErrorType::ConfigurationError:
		return "Configuration Error";
	case ofxGgmlImageGenerationErrorType::ModelLoadError:
		return "Model Load Error";
	case ofxGgmlImageGenerationErrorType::ValidationError:
		return "Validation Error";
	case ofxGgmlImageGenerationErrorType::GenerationError:
		return "Generation Error";
	case ofxGgmlImageGenerationErrorType::ResourceError:
		return "Resource Error";
	case ofxGgmlImageGenerationErrorType::TimeoutError:
		return "Timeout Error";
	case ofxGgmlImageGenerationErrorType::BackendError:
		return "Backend Error";
	case ofxGgmlImageGenerationErrorType::None:
	default:
		return "No Error";
	}
}

ofxGgmlDiffusionInference::ImageValidationResult
ofxGgmlDiffusionInference::validateWithVision(
	const ofxGgmlImageGenerationResult & generationResult,
	const std::string & originalPrompt,
	ofxGgmlVisionInference * visionInference,
	const ofxGgmlVisionModelProfile & visionProfile,
	ofxGgmlInference * textInference) {
	ImageValidationResult result;

	if (!visionInference) {
		result.error = "Vision inference not configured";
		return result;
	}

	if (!generationResult.success || generationResult.images.empty()) {
		result.error = "No generated images to validate";
		return result;
	}

	float totalScore = 0.0f;
	for (const auto & image : generationResult.images) {
		// Use vision to describe the generated image
		ofxGgmlVisionRequest visionRequest;
		visionRequest.task = ofxGgmlVisionTask::Describe;
		visionRequest.prompt = "Describe this image in detail, focusing on the main subjects, composition, and style.";
		visionRequest.images.push_back({image.path, "Generated", ""});
		visionRequest.maxTokens = 256;
		visionRequest.temperature = 0.2f;

		auto visionResult = visionInference->runServerRequest(visionProfile, visionRequest);
		if (!visionResult.success) {
			result.descriptions.push_back({image.index, "Vision analysis failed: " + visionResult.error});
			result.imageScores.push_back({image.index, 0.0f});
			continue;
		}

		result.descriptions.push_back({image.index, visionResult.text});

		// If text inference available, compute alignment score
		if (textInference) {
			auto promptEmbed = textInference->embed(originalPrompt);
			auto descEmbed = textInference->embed(visionResult.text);

			if (promptEmbed.success && descEmbed.success) {
				float score = textInference->cosineSimilarity(
					promptEmbed.embedding,
					descEmbed.embedding);
				result.imageScores.push_back({image.index, score});
				totalScore += score;
			} else {
				result.imageScores.push_back({image.index, 0.5f}); // neutral score
				totalScore += 0.5f;
			}
		} else {
			// Without embeddings, just mark as validated
			result.imageScores.push_back({image.index, 0.5f});
			totalScore += 0.5f;
		}
	}

	result.success = true;
	result.averageScore = generationResult.images.empty() ?
		0.0f : totalScore / generationResult.images.size();
	return result;
}
