#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <vector>

/// Generic validation loop framework for generative→analytical workflows
///
/// This framework provides a reusable pattern for:
/// 1. Generate content (images, video plans, music, etc.)
/// 2. Analyze generated content with appropriate tool (vision, CLIP, audio analysis)
/// 3. Score quality/alignment
/// 4. Optionally refine and regenerate based on validation results
///
/// Usage example:
///   ofxGgmlValidationLoop<ImageResult, VisionAnalysis> loop;
///   loop.setGenerator([&](int attempt) { return diffusion.generate(request); });
///   loop.setValidator([&](const ImageResult& img) { return vision.analyze(img.path); });
///   loop.setScorer([&](const ImageResult& img, const VisionAnalysis& analysis) {
///       return computeAlignment(request.prompt, analysis.description);
///   });
///   auto result = loop.run();

/// Validation loop configuration
struct ofxGgmlValidationLoopConfig {
	int maxAttempts = 3;
	float qualityThreshold = 0.6f;
	bool enableRefinement = true;
	bool collectAllAttempts = false;
	float improvementThreshold = 0.1f; // Minimum score improvement to continue refining
};

/// Result from a single validation attempt
template<typename TGenerated, typename TAnalysis>
struct ofxGgmlValidationAttempt {
	int attemptNumber = 0;
	bool success = false;
	float score = 0.0f;
	float elapsedMs = 0.0f;
	TGenerated generated;
	TAnalysis analysis;
	std::string error;
	std::string feedback;
};

/// Complete validation loop result
template<typename TGenerated, typename TAnalysis>
struct ofxGgmlValidationLoopResult {
	bool success = false;
	int totalAttempts = 0;
	float totalElapsedMs = 0.0f;
	float bestScore = 0.0f;
	int bestAttemptIndex = -1;
	std::string error;

	// Best validated result
	TGenerated bestGenerated;
	TAnalysis bestAnalysis;

	// All attempts (if collectAllAttempts enabled)
	std::vector<ofxGgmlValidationAttempt<TGenerated, TAnalysis>> attempts;

	// Aggregated warnings from validation
	std::vector<std::string> warnings;
};

/// Progress callback for validation loops
template<typename TGenerated, typename TAnalysis>
using ofxGgmlValidationProgressCallback = std::function<bool(
	const ofxGgmlValidationAttempt<TGenerated, TAnalysis>&)>;

/// Generic validation loop executor
template<typename TGenerated, typename TAnalysis>
class ofxGgmlValidationLoop {
public:
	using GenerateFunction = std::function<TGenerated(int attemptNumber)>;
	using ValidateFunction = std::function<TAnalysis(const TGenerated&)>;
	using ScoreFunction = std::function<float(const TGenerated&, const TAnalysis&)>;
	using RefineFunction = std::function<void(TGenerated&, const TAnalysis&, float score)>;
	using ProgressCallback = ofxGgmlValidationProgressCallback<TGenerated, TAnalysis>;

	ofxGgmlValidationLoop() = default;
	explicit ofxGgmlValidationLoop(const ofxGgmlValidationLoopConfig& config)
		: m_config(config) {}

	void setConfig(const ofxGgmlValidationLoopConfig& config) { m_config = config; }
	const ofxGgmlValidationLoopConfig& getConfig() const { return m_config; }

	void setGenerator(GenerateFunction func) { m_generate = func; }
	void setValidator(ValidateFunction func) { m_validate = func; }
	void setScorer(ScoreFunction func) { m_score = func; }
	void setRefiner(RefineFunction func) { m_refine = func; }
	void setProgressCallback(ProgressCallback func) { m_progressCallback = func; }

	/// Run the validation loop
	ofxGgmlValidationLoopResult<TGenerated, TAnalysis> run() {
		ofxGgmlValidationLoopResult<TGenerated, TAnalysis> result;

		if (!m_generate || !m_validate || !m_score) {
			result.error = "Validation loop not fully configured. Need generator, validator, and scorer.";
			return result;
		}

		float previousScore = 0.0f;
		bool foundGoodResult = false;

		for (int attempt = 1; attempt <= m_config.maxAttempts && !foundGoodResult; ++attempt) {
			ofxGgmlValidationAttempt<TGenerated, TAnalysis> attemptResult;
			attemptResult.attemptNumber = attempt;

			const auto startTime = std::chrono::steady_clock::now();

			try {
				// 1. Generate
				attemptResult.generated = m_generate(attempt);

				// 2. Validate/Analyze
				attemptResult.analysis = m_validate(attemptResult.generated);

				// 3. Score
				attemptResult.score = m_score(attemptResult.generated, attemptResult.analysis);

				attemptResult.success = true;

				// Check if this is the best so far
				if (attemptResult.score > result.bestScore) {
					result.bestScore = attemptResult.score;
					result.bestGenerated = attemptResult.generated;
					result.bestAnalysis = attemptResult.analysis;
					result.bestAttemptIndex = result.totalAttempts;
				}

				// Check if quality threshold met
				if (attemptResult.score >= m_config.qualityThreshold) {
					foundGoodResult = true;
				}

				// Check if we're improving enough to continue
				if (attempt > 1 && m_config.enableRefinement) {
					float improvement = attemptResult.score - previousScore;
					if (improvement < m_config.improvementThreshold &&
						attemptResult.score < m_config.qualityThreshold) {
						attemptResult.feedback = "Insufficient improvement, stopping refinement";
						foundGoodResult = true; // Stop trying
					}
				}

				previousScore = attemptResult.score;

			} catch (const std::exception& e) {
				attemptResult.success = false;
				attemptResult.error = std::string("Validation attempt failed: ") + e.what();
				result.warnings.push_back(attemptResult.error);
			}

			attemptResult.elapsedMs = std::chrono::duration<float, std::milli>(
				std::chrono::steady_clock::now() - startTime).count();

			result.totalElapsedMs += attemptResult.elapsedMs;

			// Store attempt if collecting all
			if (m_config.collectAllAttempts) {
				result.attempts.push_back(attemptResult);
			}

			// Progress callback
			if (m_progressCallback) {
				bool shouldContinue = m_progressCallback(attemptResult);
				if (!shouldContinue) {
					result.warnings.push_back("Validation loop cancelled by progress callback");
					break;
				}
			}

			// Apply refinement for next attempt
			if (m_config.enableRefinement && m_refine && !foundGoodResult &&
				attempt < m_config.maxAttempts) {
				try {
					m_refine(attemptResult.generated, attemptResult.analysis, attemptResult.score);
				} catch (const std::exception& e) {
					result.warnings.push_back(std::string("Refinement failed: ") + e.what());
				}
			}

			result.totalAttempts++;
		}

		result.success = result.bestScore > 0.0f;
		return result;
	}

private:
	ofxGgmlValidationLoopConfig m_config;
	GenerateFunction m_generate;
	ValidateFunction m_validate;
	ScoreFunction m_score;
	RefineFunction m_refine;
	ProgressCallback m_progressCallback;
};

/// Helper: Create validation loop for diffusion → vision validation
namespace ofxGgmlValidationLoops {

/// Simplified validation loop that just validates once (no refinement)
template<typename TGenerated, typename TAnalysis>
ofxGgmlValidationLoopResult<TGenerated, TAnalysis> validateOnce(
	std::function<TGenerated()> generator,
	std::function<TAnalysis(const TGenerated&)> validator,
	std::function<float(const TGenerated&, const TAnalysis&)> scorer) {

	ofxGgmlValidationLoop<TGenerated, TAnalysis> loop;
	ofxGgmlValidationLoopConfig config;
	config.maxAttempts = 1;
	config.enableRefinement = false;
	loop.setConfig(config);

	loop.setGenerator([generator](int) { return generator(); });
	loop.setValidator(validator);
	loop.setScorer(scorer);

	return loop.run();
}

} // namespace ofxGgmlValidationLoops
