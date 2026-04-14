#include "ofxGgmlInference.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <random>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

namespace {

static std::string trim(const std::string & s) {
size_t b = 0;
while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
size_t e = s.size();
while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
return s.substr(b, e - b);
}

/// Validate that a file path exists and is a regular file.
/// Returns true if the path is valid and safe to use.
static bool isValidFilePath(const std::string & path) {
	if (path.empty()) return false;

	// Check for null bytes (security: path injection)
	if (path.find('\0') != std::string::npos) return false;

	std::error_code ec;
	std::filesystem::path fsPath(path);

	// Check if file exists
	if (!std::filesystem::exists(fsPath, ec)) return false;
	if (ec) return false;

	// Ensure it's a regular file, not a device or special file
	if (!std::filesystem::is_regular_file(fsPath, ec)) return false;
	if (ec) return false;

	return true;
}

/// Validate an executable path for security.
/// Checks that the executable exists and is not a suspicious path.
static bool isValidExecutablePath(const std::string & path) {
	if (path.empty()) return false;

	// Check for null bytes and suspicious characters
	if (path.find('\0') != std::string::npos) return false;
	if (path.find("..") != std::string::npos) return false;  // Path traversal

	std::error_code ec;
	std::filesystem::path fsPath(path);

	// For security, normalize the path to resolve any symlinks
	std::filesystem::path canonical = std::filesystem::weakly_canonical(fsPath, ec);
	if (ec) {
		// If we can't canonicalize, try basic existence check
		return std::filesystem::exists(fsPath, ec) && !ec;
	}

	// Check if the canonical path exists
	return std::filesystem::exists(canonical, ec) && !ec;
}

/// Sanitize a string for safe use in command arguments.
/// Removes or escapes potentially dangerous characters.
static std::string sanitizeArgument(const std::string & arg) {
	std::string result;
	result.reserve(arg.size());

	for (char c : arg) {
		// Remove null bytes and control characters (except common whitespace)
		if (c == '\0' || (std::iscntrl(static_cast<unsigned char>(c)) &&
		    c != '\t' && c != '\n' && c != '\r')) {
			continue;
		}
		result += c;
	}

	return result;
}

#ifdef _WIN32
static std::string quoteArg(const std::string & arg) {
	std::string out;
	out.reserve(arg.size() + 2);
	out += '"';
	for (char c : arg) {
		if (c == '"') out += '\\';
		out += c;
	}
	out += '"';
	return out;
}

static std::string joinCommand(const std::vector<std::string> & args) {
	if (args.empty()) return " 2>&1";
	std::string cmd;
	size_t totalSize = 0;
	for (const auto & arg : args) {
		totalSize += arg.size() + 3; // quotes and space
	}
	cmd.reserve(totalSize + 6);
	for (size_t i = 0; i < args.size(); ++i) {
		if (i > 0) cmd += ' ';
		cmd += quoteArg(args[i]);
	}
	cmd += " 2>&1";
	return cmd;
}
#endif

static bool runCommandCapture(const std::vector<std::string> & args, std::string & output, int & exitCode) {
output.clear();
exitCode = -1;
#ifdef _WIN32
const std::string cmd = joinCommand(args);

FILE * pipe = _popen(cmd.c_str(), "r");
if (!pipe) return false;

std::array<char, 4096> buf{};
while (fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {
output += buf.data();
}

exitCode = _pclose(pipe);
#else
if (args.empty() || args.front().empty()) return false;

	int pipeFds[2] = {-1, -1};
	if (pipe(pipeFds) != 0) {
		return false;
	}

	pid_t pid = fork();
	if (pid < 0) {
		close(pipeFds[0]);
		close(pipeFds[1]);
		return false;
	}

	if (pid == 0) {
		dup2(pipeFds[1], STDOUT_FILENO);
		dup2(pipeFds[1], STDERR_FILENO);
		close(pipeFds[0]);
		close(pipeFds[1]);

		std::vector<char *> argv;
		argv.reserve(args.size() + 1);
		for (const auto & arg : args) {
			argv.push_back(const_cast<char *>(arg.c_str()));
		}
		argv.push_back(nullptr);

		execvp(argv[0], argv.data());
		_exit(127);
	}

	close(pipeFds[1]);
	std::array<char, 4096> buf{};
	ssize_t bytesRead = 0;
	while ((bytesRead = read(pipeFds[0], buf.data(), buf.size())) > 0) {
		output.append(buf.data(), static_cast<size_t>(bytesRead));
	}
	close(pipeFds[0]);

	int status = 0;
	if (waitpid(pid, &status, 0) < 0) {
		return false;
	}
	if (WIFEXITED(status)) {
		exitCode = WEXITSTATUS(status);
	} else if (WIFSIGNALED(status)) {
		exitCode = 128 + WTERMSIG(status);
	}
#endif
return true;
}

static std::string makeTempPath(const char * prefix, const char * ext) {
	std::error_code ec;
	std::filesystem::path base = std::filesystem::temp_directory_path(ec);
	if (ec) base = std::filesystem::current_path();

	// Generate a cryptographically random filename component
	std::random_device rd;
	std::mt19937_64 rng(rd());
	std::uniform_int_distribution<uint64_t> dist;

	// Try up to 1000 times to create a unique file (prevents race conditions)
	for (int attempts = 0; attempts < 1000; ++attempts) {
		const uint64_t random1 = dist(rng);
		const uint64_t random2 = dist(rng);
		std::ostringstream oss;
		oss << prefix << std::hex << random1 << "_" << random2 << ext;

		std::filesystem::path candidate = base / oss.str();

		// Try to create the file exclusively (atomic check-and-create)
		// Use platform-specific approach since std::ios::excl is not standard
#ifdef _WIN32
		// On Windows, use CreateFile with CREATE_NEW for atomic exclusive creation
		HANDLE hFile = CreateFileW(
			candidate.c_str(),
			GENERIC_WRITE,
			0,  // No sharing
			NULL,
			CREATE_NEW,  // Fails if file exists (atomic check-and-create)
			FILE_ATTRIBUTE_NORMAL,
			NULL
		);
		if (hFile != INVALID_HANDLE_VALUE) {
			CloseHandle(hFile);
			return candidate.string();
		}
#else
		// On POSIX systems, std::ios::noreplace is C++23, so use filesystem approach
		std::error_code ec;
		if (!std::filesystem::exists(candidate, ec) && !ec) {
			std::ofstream test(candidate, std::ios::out);
			if (test.is_open()) {
				test.close();
				return candidate.string();
			}
		}
#endif
	}

	// Fallback: use time-based name with random component
	const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
	const uint64_t nonce = dist(rng);
	return (base / (std::string(prefix) + std::to_string(now) + "_" + std::to_string(nonce) + ext)).string();
}

static uint32_t makeRandomSeed() {
	try {
		std::random_device rd;
		return rd();
	} catch (...) {
		const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
		return static_cast<uint32_t>(now ^ (now >> 32));
	}
}

static bool writeTextFile(const std::string & path, const std::string & text) {
std::ofstream out(path, std::ios::binary);
if (!out.is_open()) return false;
out << text;
return out.good();
}

struct ThreadLocalTempFile {
std::string path;
~ThreadLocalTempFile() {
if (path.empty()) return;
std::error_code ec;
std::filesystem::remove(path, ec);
}
};

static bool writeReusableTempTextFile(
ThreadLocalTempFile & slot,
const char * prefix,
const std::string & text,
std::string & outPath) {
if (slot.path.empty()) {
slot.path = makeTempPath(prefix, ".txt");
}
if (!writeTextFile(slot.path, text)) {
slot.path = makeTempPath(prefix, ".txt");
if (!writeTextFile(slot.path, text)) {
return false;
}
}
outPath = slot.path;
return true;
}

static bool extractEmbeddingArray(const ofJson & value, std::vector<float> & out) {
	out.clear();
	if (!value.is_array()) return false;
	for (const auto & item : value) {
		if (!item.is_number()) continue;
		const float v = item.get<float>();
		if (std::isfinite(v)) out.push_back(v);
	}
	return !out.empty();
}

static bool parseEmbeddingJson(const ofJson & json, std::vector<float> & out) {
	if (json.is_array()) {
		return extractEmbeddingArray(json, out);
	}
	if (!json.is_object()) return false;

	if (json.contains("embedding")) {
		if (parseEmbeddingJson(json["embedding"], out)) return true;
	}
	if (json.contains("embeddings")) {
		if (parseEmbeddingJson(json["embeddings"], out)) return true;
	}
	if (json.contains("result")) {
		if (parseEmbeddingJson(json["result"], out)) return true;
	}
	if (json.contains("data") && json["data"].is_array()) {
		for (const auto & item : json["data"]) {
			if (parseEmbeddingJson(item, out)) return true;
		}
	}
	return false;
}

static bool parseEmbeddingVector(const std::string & raw, std::vector<float> & out) {
	out.clear();

	const std::string normalized = trim(raw);
	if (!normalized.empty()) {
		ofJson parsed = ofJson::parse(normalized, nullptr, false);
		if (!parsed.is_discarded() && parseEmbeddingJson(parsed, out)) {
			return true;
		}
	}

	std::istringstream lines(raw);
	std::vector<std::string> candidates;
	std::string line;
	while (std::getline(lines, line)) {
		line = trim(line);
		if (!line.empty()) candidates.push_back(std::move(line));
	}
	for (auto it = candidates.rbegin(); it != candidates.rend(); ++it) {
		ofJson parsed = ofJson::parse(*it, nullptr, false);
		if (!parsed.is_discarded() && parseEmbeddingJson(parsed, out)) {
			return true;
		}
	}

	const size_t begin = raw.find('[');
	const size_t end = raw.find(']', begin == std::string::npos ? 0 : begin + 1);
	if (begin == std::string::npos || end == std::string::npos || end <= begin + 1) return false;
	std::string body = raw.substr(begin + 1, end - begin - 1);
	for (char & c : body) {
		if (c == ',') c = ' ';
	}
	std::istringstream iss(body);
	float v = 0.0f;
	while (iss >> v) {
		if (std::isfinite(v)) out.push_back(v);
	}
	return !out.empty();
}

} // namespace

ofxGgmlInference::ofxGgmlInference()
: m_completionExe("llama-cli")
, m_embeddingExe("llama-embedding") {}

void ofxGgmlInference::setCompletionExecutable(const std::string & path) {
m_completionExe = path;
}

void ofxGgmlInference::setEmbeddingExecutable(const std::string & path) {
m_embeddingExe = path;
}

const std::string & ofxGgmlInference::getCompletionExecutable() const {
return m_completionExe;
}

const std::string & ofxGgmlInference::getEmbeddingExecutable() const {
return m_embeddingExe;
}

ofxGgmlInferenceResult ofxGgmlInference::generate(
const std::string & modelPath,
const std::string & prompt,
const ofxGgmlInferenceSettings & settings) const {
ofxGgmlInferenceResult result;
if (modelPath.empty()) {
result.error = "model path is empty";
return result;
}
if (m_completionExe.empty()) {
result.error = "completion executable path is empty";
return result;
}

// Security: Validate model path
if (!isValidFilePath(modelPath)) {
	result.error = "invalid or inaccessible model path: " + modelPath;
	return result;
}

// Security: Validate executable path
if (!isValidExecutablePath(m_completionExe)) {
	result.error = "invalid or inaccessible completion executable: " + m_completionExe;
	return result;
}

// Security: Sanitize prompt
std::string sanitizedPrompt = sanitizeArgument(prompt);
if (sanitizedPrompt.empty() && !prompt.empty()) {
	result.error = "prompt contains only invalid characters";
	return result;
}

const auto t0 = std::chrono::steady_clock::now();
static thread_local ThreadLocalTempFile promptTempFile;
std::string promptPath;
if (!writeReusableTempTextFile(promptTempFile, "ofxggml_prompt_", sanitizedPrompt, promptPath)) {
result.error = "failed to write temp prompt file";
return result;
}

std::vector<std::string> args = {
m_completionExe,
"-m", modelPath,
"--file", promptPath,
"-n", std::to_string(std::clamp(settings.maxTokens, 1, 8192)),
"-c", std::to_string(std::clamp(settings.contextSize, 128, 131072)),
"-b", std::to_string(std::clamp(settings.batchSize, 1, 8192)),
"-ngl", std::to_string(std::max(0, settings.gpuLayers)),
"--temp", std::to_string(std::clamp(settings.temperature, 0.0f, 3.0f)),
"--top-p", std::to_string(std::clamp(settings.topP, 0.0f, 1.0f)),
"--repeat-penalty", std::to_string(std::clamp(settings.repeatPenalty, 1.0f, 3.0f)),
"--no-display-prompt"
};

if (settings.simpleIo) {
args.push_back("--simple-io");
}
if (settings.threads > 0) {
args.push_back("--threads");
args.push_back(std::to_string(std::clamp(settings.threads, 1, 256)));
}
if (settings.seed >= 0) {
args.push_back("--seed");
args.push_back(std::to_string(settings.seed));
}
if (!settings.promptCachePath.empty()) {
	// Security: Validate cache path if it exists, or allow creation of new file
	std::error_code ec;
	std::filesystem::path cachePath(settings.promptCachePath);
	if (std::filesystem::exists(cachePath, ec)) {
		if (!isValidFilePath(settings.promptCachePath)) {
			result.error = "invalid prompt cache path: " + settings.promptCachePath;
			return result;
		}
	}
	args.push_back("--prompt-cache");
	args.push_back(settings.promptCachePath);
	if (settings.promptCacheAll) {
		args.push_back("--prompt-cache-all");
	}
}
if (!settings.jsonSchema.empty()) {
	// Security: Sanitize JSON schema
	std::string sanitizedSchema = sanitizeArgument(settings.jsonSchema);
	args.push_back("--json-schema");
	args.push_back(sanitizedSchema);
}
if (!settings.grammarPath.empty()) {
	// Security: Validate grammar file path
	if (!isValidFilePath(settings.grammarPath)) {
		result.error = "invalid grammar file path: " + settings.grammarPath;
		return result;
	}
	args.push_back("--grammar-file");
	args.push_back(settings.grammarPath);
}

std::string raw;
int exitCode = -1;
const bool started = runCommandCapture(args, raw, exitCode);

if (!started) {
result.error = "failed to start llama completion process";
return result;
}
if (exitCode != 0) {
result.error = trim(raw);
if (result.error.empty()) {
result.error = "llama completion failed with exit code " + std::to_string(exitCode);
}
return result;
}

result.success = true;
result.text = trim(raw);
const auto t1 = std::chrono::steady_clock::now();
result.elapsedMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
return result;
}

ofxGgmlEmbeddingResult ofxGgmlInference::embed(
const std::string & modelPath,
const std::string & text,
const ofxGgmlEmbeddingSettings & settings) const {
ofxGgmlEmbeddingResult result;
if (modelPath.empty()) {
result.error = "model path is empty";
return result;
}
if (m_embeddingExe.empty()) {
result.error = "embedding executable path is empty";
return result;
}

// Security: Validate model path
if (!isValidFilePath(modelPath)) {
	result.error = "invalid or inaccessible model path: " + modelPath;
	return result;
}

// Security: Validate executable path
if (!isValidExecutablePath(m_embeddingExe)) {
	result.error = "invalid or inaccessible embedding executable: " + m_embeddingExe;
	return result;
}

// Security: Sanitize input text
std::string sanitizedText = sanitizeArgument(text);
if (sanitizedText.empty() && !text.empty()) {
	result.error = "text contains only invalid characters";
	return result;
}

static thread_local ThreadLocalTempFile promptTempFile;
std::string promptPath;
if (!writeReusableTempTextFile(promptTempFile, "ofxggml_embed_", sanitizedText, promptPath)) {
result.error = "failed to write temp embedding prompt file";
return result;
}

std::vector<std::string> args = {
m_embeddingExe,
"-m", modelPath,
"--file", promptPath,
"--embd-output-format", "json",
"--pooling", settings.pooling
};
if (settings.normalize) {
args.push_back("--embd-normalize");
}

std::string raw;
int exitCode = -1;
const bool started = runCommandCapture(args, raw, exitCode);

if (!started) {
result.error = "failed to start llama embedding process";
return result;
}
if (exitCode != 0) {
result.error = trim(raw);
if (result.error.empty()) {
result.error = "llama embedding failed with exit code " + std::to_string(exitCode);
}
return result;
}

if (!parseEmbeddingVector(raw, result.embedding)) {
result.error = "failed to parse embedding output";
return result;
}

result.success = true;
return result;
}

std::vector<std::string> ofxGgmlInference::tokenize(const std::string & text) {
std::vector<std::string> tokens;
std::istringstream iss(text);
std::string tok;
while (iss >> tok) {
tokens.push_back(tok);
}
return tokens;
}

std::string ofxGgmlInference::detokenize(const std::vector<std::string> & tokens) {
std::ostringstream oss;
for (size_t i = 0; i < tokens.size(); ++i) {
if (i > 0) oss << ' ';
oss << tokens[i];
}
return oss.str();
}

int ofxGgmlInference::sampleFromLogits(
const std::vector<float> & logits,
float temperature,
float topP,
uint32_t seed) {
if (logits.empty()) return -1;
if (!std::isfinite(temperature) || temperature <= 0.0f) {
return static_cast<int>(std::distance(logits.begin(), std::max_element(logits.begin(), logits.end())));
}

const float maxLogit = *std::max_element(logits.begin(), logits.end());
std::vector<float> probs(logits.size(), 0.0f);
float sum = 0.0f;
for (size_t i = 0; i < logits.size(); ++i) {
const float z = (logits[i] - maxLogit) / temperature;
const float p = std::exp(z);
probs[i] = std::isfinite(p) ? p : 0.0f;
sum += probs[i];
}
if (sum <= 0.0f) {
return static_cast<int>(std::distance(logits.begin(), std::max_element(logits.begin(), logits.end())));
}
for (float & p : probs) p /= sum;

std::vector<size_t> idx(probs.size());
std::iota(idx.begin(), idx.end(), static_cast<size_t>(0));
std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return probs[a] > probs[b]; });

topP = std::clamp(topP, 0.0f, 1.0f);
if (topP <= 0.0f) {
return static_cast<int>(idx.front());
}

std::vector<double> filtered;
std::vector<size_t> filteredIdx;
filtered.reserve(probs.size());
filteredIdx.reserve(probs.size());
float cum = 0.0f;
for (size_t i : idx) {
filtered.push_back(probs[i]);
filteredIdx.push_back(i);
cum += probs[i];
if (cum >= topP) break;
}

	std::mt19937 rng(seed == 0 ? makeRandomSeed() : seed);
std::discrete_distribution<size_t> dist(filtered.begin(), filtered.end());
return static_cast<int>(filteredIdx[dist(rng)]);
}

void ofxGgmlEmbeddingIndex::clear() {
m_entries.clear();
}

void ofxGgmlEmbeddingIndex::add(const std::string & id, const std::string & text, const std::vector<float> & embedding) {
if (embedding.empty()) return;
m_entries.push_back({ id, text, embedding });
}

std::vector<ofxGgmlSimilarityHit> ofxGgmlEmbeddingIndex::search(const std::vector<float> & queryEmbedding, size_t topK) const {
	std::vector<ofxGgmlSimilarityHit> hits;
	if (queryEmbedding.empty() || m_entries.empty() || topK == 0) return hits;

	hits.reserve(m_entries.size());
	for (size_t i = 0; i < m_entries.size(); ++i) {
		const auto & e = m_entries[i];
		hits.push_back({ e.id, e.text, cosineSimilarity(queryEmbedding, e.embedding), i });
	}

	const size_t limit = std::min(topK, hits.size());
	auto byScoreDesc = [](const ofxGgmlSimilarityHit & a, const ofxGgmlSimilarityHit & b) {
		return a.score > b.score;
	};
	if (limit < hits.size()) {
		std::nth_element(hits.begin(), hits.begin() + limit, hits.end(), byScoreDesc);
		hits.resize(limit);
	}
	std::sort(hits.begin(), hits.end(), byScoreDesc);
	return hits;
}

float ofxGgmlEmbeddingIndex::cosineSimilarity(const std::vector<float> & a, const std::vector<float> & b) {
if (a.empty() || b.empty() || a.size() != b.size()) return 0.0f;
double dot = 0.0;
double na = 0.0;
double nb = 0.0;
for (size_t i = 0; i < a.size(); ++i) {
const double da = a[i];
const double db = b[i];
dot += da * db;
na += da * da;
nb += db * db;
}
if (na <= 0.0 || nb <= 0.0) return 0.0f;
return static_cast<float>(dot / (std::sqrt(na) * std::sqrt(nb)));
}
