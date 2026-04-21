#include "inference/ofxGgmlRAGPipeline.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace {

std::string trimCopy(const std::string & text) {
	const auto begin = std::find_if_not(
		text.begin(),
		text.end(),
		[](unsigned char ch) { return std::isspace(ch) != 0; });
	const auto end = std::find_if_not(
		text.rbegin(),
		text.rend(),
		[](unsigned char ch) { return std::isspace(ch) != 0; }).base();
	if (begin >= end) {
		return {};
	}
	return std::string(begin, end);
}

std::string toLowerCopy(const std::string & text) {
	std::string lowered = text;
	std::transform(
		lowered.begin(),
		lowered.end(),
		lowered.begin(),
		[](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
	return lowered;
}

// Tokenize text into lowercase word tokens, stripping punctuation.
std::vector<std::string> tokenize(const std::string & text) {
	std::vector<std::string> tokens;
	std::string current;
	for (unsigned char ch : text) {
		if (std::isalnum(ch) != 0) {
			current.push_back(static_cast<char>(std::tolower(ch)));
		} else {
			if (!current.empty()) {
				tokens.push_back(std::move(current));
				current.clear();
			}
		}
	}
	if (!current.empty()) {
		tokens.push_back(std::move(current));
	}
	return tokens;
}

// Very short common words to skip when scoring.
const std::unordered_set<std::string> & stopWords() {
	static const std::unordered_set<std::string> kStopWords = {
		"a", "an", "the", "is", "it", "in", "on", "at", "to", "of",
		"and", "or", "for", "with", "as", "by", "be", "are", "was",
		"that", "this", "which", "from", "not", "but", "so", "if"
	};
	return kStopWords;
}

} // namespace

// ---------------------------------------------------------------------------
// ofxGgmlRAGPipeline
// ---------------------------------------------------------------------------

void ofxGgmlRAGPipeline::addDocument(const ofxGgmlRAGDocument & doc) {
	m_documents.push_back(doc);
}

void ofxGgmlRAGPipeline::addTextDocument(
	const std::string & content,
	const std::string & id,
	const std::string & label,
	const std::string & uri) {
	ofxGgmlRAGDocument doc;
	doc.content = content;
	doc.id = id.empty()
		? std::string("doc-") + std::to_string(m_documents.size() + 1)
		: id;
	doc.sourceLabel = label;
	doc.sourceUri = uri;
	m_documents.push_back(std::move(doc));
}

void ofxGgmlRAGPipeline::clearDocuments() {
	m_documents.clear();
}

size_t ofxGgmlRAGPipeline::documentCount() const {
	return m_documents.size();
}

ofxGgmlInference & ofxGgmlRAGPipeline::getInference() {
	return m_inference;
}

const ofxGgmlInference & ofxGgmlRAGPipeline::getInference() const {
	return m_inference;
}

ofxGgmlRAGRetrievalResult ofxGgmlRAGPipeline::retrieve(
	const ofxGgmlRAGQuery & query) const {
	ofxGgmlRAGRetrievalResult result;
	if (trimCopy(query.query).empty()) {
		result.error = "RAG query is empty.";
		return result;
	}
	if (m_documents.empty()) {
		result.error = "No documents have been added to the RAG pipeline.";
		return result;
	}

	const size_t chunkSize = std::max(size_t(64), query.chunkSize);
	const size_t overlap = std::min(query.chunkOverlap, chunkSize / 2);

	std::vector<ofxGgmlRAGChunk> allChunks;
	for (const auto & doc : m_documents) {
		auto docChunks = chunkDocument(doc, chunkSize, overlap);
		allChunks.insert(
			allChunks.end(),
			std::make_move_iterator(docChunks.begin()),
			std::make_move_iterator(docChunks.end()));
	}

	for (auto & chunk : allChunks) {
		chunk.score = scoreChunk(chunk, query.query);
	}

	std::stable_sort(
		allChunks.begin(),
		allChunks.end(),
		[](const ofxGgmlRAGChunk & a, const ofxGgmlRAGChunk & b) {
			return a.score > b.score;
		});

	const size_t k = std::min(query.topK, allChunks.size());
	result.chunks.assign(allChunks.begin(), allChunks.begin() + static_cast<std::ptrdiff_t>(k));
	result.augmentedContext = buildAugmentedContext(result.chunks, query.includeSourceHeaders);
	result.success = true;
	return result;
}

ofxGgmlRAGResult ofxGgmlRAGPipeline::generate(
	const ofxGgmlRAGRequest & request,
	std::function<bool(const std::string &)> onChunk) const {
	const auto start = std::chrono::steady_clock::now();
	ofxGgmlRAGResult result;

	result.retrieval = retrieve(request.query);
	if (!result.retrieval.success) {
		result.error = result.retrieval.error;
		return result;
	}

	result.augmentedPrompt = buildAugmentedPrompt(
		result.retrieval.augmentedContext,
		request.query.query,
		request.promptPrefix);

	result.inference = m_inference.generate(
		request.modelPath,
		result.augmentedPrompt,
		request.inferenceSettings,
		std::move(onChunk));

	const auto end = std::chrono::steady_clock::now();
	result.elapsedMs =
		std::chrono::duration<float, std::milli>(end - start).count();

	result.success = result.inference.success;
	if (result.success) {
		result.answer = result.inference.text;
	} else {
		result.error = result.inference.error;
	}
	return result;
}

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

std::vector<ofxGgmlRAGChunk> ofxGgmlRAGPipeline::chunkDocument(
	const ofxGgmlRAGDocument & doc,
	size_t chunkSize,
	size_t overlap) {
	std::vector<ofxGgmlRAGChunk> chunks;
	const std::string & text = doc.content;
	if (text.empty() || chunkSize == 0) {
		return chunks;
	}

	const size_t step = chunkSize > overlap ? chunkSize - overlap : 1;
	size_t start = 0;
	int idx = 0;
	while (start < text.size()) {
		size_t end = std::min(start + chunkSize, text.size());

		// Extend to the nearest word boundary if possible.
		if (end < text.size()) {
			size_t boundary = end;
			while (boundary < text.size() &&
				std::isspace(static_cast<unsigned char>(text[boundary])) == 0) {
				++boundary;
			}
			// Only extend if the word boundary is reasonably close.
			if (boundary - end <= 40) {
				end = boundary;
			}
		}

		ofxGgmlRAGChunk chunk;
		chunk.docId = doc.id;
		chunk.sourceLabel = doc.sourceLabel;
		chunk.sourceUri = doc.sourceUri;
		chunk.text = trimCopy(text.substr(start, end - start));
		chunk.chunkIndex = idx++;
		if (!chunk.text.empty()) {
			chunks.push_back(std::move(chunk));
		}

		if (end >= text.size()) {
			break;
		}
		start += step;
	}
	return chunks;
}

float ofxGgmlRAGPipeline::scoreChunk(
	const ofxGgmlRAGChunk & chunk,
	const std::string & query) {
	if (query.empty() || chunk.text.empty()) {
		return 0.0f;
	}

	const auto queryTokens = tokenize(query);
	const auto chunkTokens = tokenize(chunk.text);

	if (queryTokens.empty() || chunkTokens.empty()) {
		return 0.0f;
	}

	// Build term frequency map for chunk.
	std::unordered_map<std::string, int> chunkTf;
	for (const auto & t : chunkTokens) {
		if (stopWords().find(t) == stopWords().end()) {
			chunkTf[t]++;
		}
	}

	// Count unique query terms that appear in the chunk (BM25-lite).
	float score = 0.0f;
	std::unordered_set<std::string> seenQueryTerms;
	for (const auto & qt : queryTokens) {
		if (stopWords().find(qt) != stopWords().end()) {
			continue;
		}
		if (seenQueryTerms.count(qt) != 0) {
			continue;
		}
		seenQueryTerms.insert(qt);

		const auto it = chunkTf.find(qt);
		if (it != chunkTf.end()) {
			// Scaled by normalized TF.
			const float tf =
				static_cast<float>(it->second) /
				static_cast<float>(chunkTokens.size());
			score += 1.0f + tf * 2.0f;
		}
	}

	// Normalize by number of distinct query terms scored.
	if (!seenQueryTerms.empty()) {
		score /= static_cast<float>(seenQueryTerms.size());
	}
	return score;
}

std::string ofxGgmlRAGPipeline::buildAugmentedContext(
	const std::vector<ofxGgmlRAGChunk> & chunks,
	bool includeSourceHeaders) {
	if (chunks.empty()) {
		return {};
	}
	std::ostringstream out;
	for (size_t i = 0; i < chunks.size(); ++i) {
		const auto & chunk = chunks[i];
		out << "[Passage " << (i + 1) << "]";
		if (includeSourceHeaders && !chunk.sourceLabel.empty()) {
			out << " " << chunk.sourceLabel;
		}
		if (includeSourceHeaders && !chunk.sourceUri.empty()) {
			out << " (" << chunk.sourceUri << ")";
		}
		out << "\n" << chunk.text << "\n\n";
	}
	return trimCopy(out.str());
}

std::string ofxGgmlRAGPipeline::buildAugmentedPrompt(
	const std::string & augmentedContext,
	const std::string & query,
	const std::string & promptPrefix) {
	std::ostringstream out;
	const std::string prefix = trimCopy(promptPrefix);
	if (!prefix.empty()) {
		out << prefix << "\n\n";
	} else {
		out << "Answer the following question using only the provided passages.\n"
			<< "If the passages do not contain enough information, say so.\n\n";
	}
	if (!augmentedContext.empty()) {
		out << "Passages:\n" << augmentedContext << "\n\n";
	}
	out << "Question: " << trimCopy(query) << "\n"
		<< "Answer:";
	return out.str();
}
