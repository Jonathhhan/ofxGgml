#pragma once

#include "ofMain.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct ofxGgmlInferenceSettings {
int maxTokens = 256;
float temperature = 0.7f;
float topP = 0.9f;
float repeatPenalty = 1.1f;
int contextSize = 2048;
int batchSize = 512;
int gpuLayers = 0;
int threads = 0;
int seed = -1;
bool simpleIo = true;
bool promptCacheAll = true;
std::string promptCachePath;
std::string jsonSchema;
std::string grammarPath;
};

struct ofxGgmlInferenceResult {
bool success = false;
float elapsedMs = 0.0f;
std::string text;
std::string error;
};

struct ofxGgmlEmbeddingSettings {
bool normalize = true;
std::string pooling = "mean";
};

struct ofxGgmlEmbeddingResult {
bool success = false;
std::vector<float> embedding;
std::string error;
};

struct ofxGgmlSimilarityHit {
std::string id;
std::string text;
float score = 0.0f;
size_t index = 0;
};

/// CLI-backed inference helper for llama.cpp tools.
///
/// This provides:
/// - text generation with optional prompt-cache reuse (KV/session cache)
/// - structured output flags (JSON schema / grammar file)
/// - embedding extraction through llama-embedding
/// - utility tokenize/sample/detokenize helpers for app-side postprocessing
class ofxGgmlInference {
public:
ofxGgmlInference();

void setCompletionExecutable(const std::string & path);
void setEmbeddingExecutable(const std::string & path);
const std::string & getCompletionExecutable() const;
const std::string & getEmbeddingExecutable() const;

ofxGgmlInferenceResult generate(
const std::string & modelPath,
const std::string & prompt,
const ofxGgmlInferenceSettings & settings = {}) const;

ofxGgmlEmbeddingResult embed(
const std::string & modelPath,
const std::string & text,
const ofxGgmlEmbeddingSettings & settings = {}) const;

static std::vector<std::string> tokenize(const std::string & text);
static std::string detokenize(const std::vector<std::string> & tokens);
static int sampleFromLogits(
const std::vector<float> & logits,
float temperature = 1.0f,
float topP = 1.0f,
uint32_t seed = 0);

private:
std::string m_completionExe;
std::string m_embeddingExe;
};

/// Lightweight in-memory similarity index for RAG-style retrieval.
class ofxGgmlEmbeddingIndex {
public:
void clear();
void add(const std::string & id, const std::string & text, const std::vector<float> & embedding);
std::vector<ofxGgmlSimilarityHit> search(const std::vector<float> & queryEmbedding, size_t topK = 3) const;

static float cosineSimilarity(const std::vector<float> & a, const std::vector<float> & b);

private:
struct Entry {
std::string id;
std::string text;
std::vector<float> embedding;
};
std::vector<Entry> m_entries;
};
