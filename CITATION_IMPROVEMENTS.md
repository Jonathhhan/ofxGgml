# Citation Search Improvements

This document describes the enhancements made to the citation search functionality in ofxGgml.

## Summary

The citation search system has been enhanced with confidence scoring, source diversity metrics, and quality filtering to provide more reliable and trustworthy citations.

## Key Features Added

### 1. Confidence Scoring System

Each citation now includes multiple quality metrics:

- **confidenceScore** (0.0-1.0): Overall confidence in the citation quality
- **isExactMatch** (boolean): Whether the quote was found exactly in the source
- **relevanceScore** (0.0-1.0): How relevant the quote is to the search topic
- **sourceCredibility** (0.0-1.0): Credibility rating of the source domain

#### Confidence Score Calculation

The confidence score is calculated by combining:
- 40% weight for exact match (or 15% for normalized match)
- 35% weight for topic relevance
- 25% weight for source credibility
- Bonus points for optimal quote length (30-500 chars)
- Small bonus for having explanatory notes

### 2. Source Credibility Rating

Sources are automatically rated based on their domain:

**High Credibility Domains:**
- `.edu` (Educational institutions): +0.3
- `.gov` (Government sites): +0.3
- `arxiv.org` (Academic preprints): +0.25
- `ieee.org`, `acm.org` (Professional societies): +0.25

**Medium Credibility Domains:**
- `wikipedia.org`: +0.2
- `scholar.google`: +0.2
- `.org` (Non-profits): +0.15
- `github.com`: +0.15

All sources start with a baseline credibility of 0.5.

### 3. Source Diversity Metrics

The result now includes:

- **sourceDiversityScore** (0.0-1.0): Measures how evenly citations are distributed across sources
  - Higher scores indicate citations from multiple different sources
  - Lower scores indicate over-reliance on a single source
  - Calculated as: 60% diversity + 40% evenness

- **averageConfidence** (0.0-1.0): Mean confidence score across all citations

### 4. Confidence Threshold Filtering

New `minimumConfidenceThreshold` parameter in `ofxGgmlCitationSearchRequest`:
- Set to 0.0 by default (no filtering)
- Citations below this threshold are excluded from results
- Helps ensure only high-quality citations are returned

### 5. Improved Citation Ranking

Citations are now sorted by:
1. Confidence score (highest first)
2. Relevance score (if confidence is equal)
3. Quote length (shorter preferred, if scores are equal)

### 6. Comprehensive Input Validation

Added robust validation with helpful error messages:
- Topic must not be empty and at least 3 characters
- Model path is required
- maxCitations must be between 1 and 1000
- minimumConfidenceThreshold must be between 0.0 and 1.0
- Crawler URL required when useCrawler is enabled

Validation happens early with clear, actionable error messages.

## API Changes

### Updated Structures

```cpp
struct ofxGgmlCitationItem {
    std::string quote;
    std::string note;
    std::string sourceLabel;
    std::string sourceUri;
    int sourceIndex = -1;
    float confidenceScore = 0.0f;        // NEW
    bool isExactMatch = false;           // NEW
    float relevanceScore = 0.0f;         // NEW
    float sourceCredibility = 0.0f;      // NEW
};

struct ofxGgmlCitationSearchRequest {
    // ... existing fields ...
    float minimumConfidenceThreshold = 0.0f;  // NEW
};

struct ofxGgmlCitationSearchResult {
    // ... existing fields ...
    float sourceDiversityScore = 0.0f;   // NEW
    float averageConfidence = 0.0f;      // NEW
};
```

## Usage Examples

### Basic Usage with Default Settings

```cpp
ofxGgmlCitationSearch citationSearch;
ofxGgmlCitationSearchRequest request;
request.modelPath = "path/to/model.gguf";
request.topic = "climate change impacts";
request.maxCitations = 10;

auto result = citationSearch.search(request);

if (result.success) {
    std::cout << "Found " << result.citations.size() << " citations\n";
    std::cout << "Average confidence: " << result.averageConfidence << "\n";
    std::cout << "Source diversity: " << result.sourceDiversityScore << "\n";

    for (const auto& citation : result.citations) {
        std::cout << "Quote: " << citation.quote << "\n";
        std::cout << "Confidence: " << citation.confidenceScore << "\n";
        std::cout << "Source: " << citation.sourceLabel << "\n\n";
    }
}
```

### Using Confidence Threshold

```cpp
ofxGgmlCitationSearchRequest request;
request.modelPath = "path/to/model.gguf";
request.topic = "quantum computing applications";
request.maxCitations = 20;
request.minimumConfidenceThreshold = 0.7f;  // Only high-confidence citations

auto result = citationSearch.search(request);
// Result will only contain citations with confidence >= 0.7
```

### Checking Quality Metrics

```cpp
auto result = citationSearch.search(request);

if (result.success) {
    // Check overall quality
    if (result.averageConfidence < 0.5f) {
        std::cout << "Warning: Low average confidence\n";
    }

    if (result.sourceDiversityScore < 0.3f) {
        std::cout << "Warning: Citations rely heavily on single source\n";
    }

    // Examine individual citations
    for (const auto& citation : result.citations) {
        if (citation.isExactMatch) {
            std::cout << "Exact match found in source\n";
        }

        if (citation.sourceCredibility > 0.8f) {
            std::cout << "High credibility source: "
                      << citation.sourceUri << "\n";
        }
    }
}
```

### Handling Validation Errors

```cpp
ofxGgmlCitationSearchRequest request;
request.topic = "ai";  // Too short!
request.modelPath = "";  // Missing!

auto result = citationSearch.search(request);

if (!result.success) {
    // Clear error message explains the problem
    std::cerr << "Error: " << result.error << "\n";
    // Output: "Citation topic is too short (minimum 3 characters). Current: "ai""
    return;
}
```

### Safe Parameter Configuration

```cpp
ofxGgmlCitationSearchRequest request;
request.modelPath = "path/to/model.gguf";
request.topic = "machine learning";

// Validate ranges
request.maxCitations = std::min(request.maxCitations, size_t(1000));
request.minimumConfidenceThreshold = std::clamp(
    request.minimumConfidenceThreshold, 0.0f, 1.0f);

// Use crawler safely
if (request.useCrawler && request.crawlerRequest.startUrl.empty()) {
    request.crawlerRequest.startUrl = "https://en.wikipedia.org/wiki/Machine_learning";
}

auto result = citationSearch.search(request);
```

## Benefits

1. **Better Citation Quality**: Confidence scoring helps identify the most reliable citations
2. **Source Diversity**: Prevents over-reliance on single sources
3. **Academic Rigor**: Favors authoritative sources (.edu, .gov, academic publishers)
4. **Transparency**: Users can see quality metrics for each citation
5. **Flexible Filtering**: Configurable threshold allows quality control
6. **Robust Validation**: Early parameter validation with helpful error messages prevents invalid requests
7. **Better User Experience**: Clear, actionable error messages guide users to fix issues

## Test Coverage

New comprehensive tests added in `tests/test_citation_search.cpp`:
- Citation items include confidence and quality metrics
- Citation results track diversity and average confidence
- Source credibility scoring favors academic domains
- Confidence score combines match quality, relevance, and credibility
- Source diversity score penalizes over-reliance on single source
- Input validation for empty/short topics
- Input validation for missing model path
- Input validation for confidence threshold range
- Input validation for maxCitations bounds
- Input validation for crawler URL when enabled

## Future Enhancements

Potential improvements for future versions:
- Publication date tracking and recency scoring
- Author information extraction
- Standard citation format export (APA, MLA, Chicago)
- BibTeX/RIS export for academic use
- Citation caching for frequently-requested topics
- Incremental retrieval for faster initial results

## Backward Compatibility

All changes are backward compatible:
- New fields have default values (0.0 or false)
- Existing code will work without modifications
- New features are opt-in via request parameters
