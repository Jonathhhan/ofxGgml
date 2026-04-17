#include "ofxGgmlDiffusionInference.h"

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
