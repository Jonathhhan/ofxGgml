# Subtitle Functionality

The `ofxGgml` subtitle system provides comprehensive support for creating, validating, and manipulating subtitle files in SRT and WebVTT formats. The system includes advanced features for quality analysis, timing adjustments, and professional subtitle workflows.

## Overview

Key subtitle capabilities:
- **Parsing**: SRT file parsing with UTF-8/UTF-16 support
- **Generation**: SRT and WebVTT export with optional styling
- **Validation**: Comprehensive error and warning detection
- **Quality Metrics**: Reading speed, duration analysis, gap detection
- **Timing Utilities**: Offset, scaling, merging, splitting
- **Styling**: WebVTT cue settings for positioning and formatting

## Basic Usage

### Parsing SRT Files

```cpp
#include "ofxGgml.h"

// Parse SRT file
std::vector<ofxGgmlSimpleSrtCue> cues;
std::string error;

if (ofxGgmlSimpleSrtSubtitleParser::parseFile("subtitles.srt", cues, error)) {
    for (const auto& cue : cues) {
        ofLogNotice() << cue.text
                      << " (" << cue.startMs << "ms - " << cue.endMs << "ms)";
    }
} else {
    ofLogError() << "Parse failed: " << error;
}
```

### Generating Subtitles

```cpp
// Create subtitle track
ofxGgmlMontageSubtitleTrack track;
track.title = "My Subtitles";

// Add cues
ofxGgmlMontageSubtitleCue cue;
cue.startSeconds = 0.0;
cue.endSeconds = 2.5;
cue.text = "Hello world!";
track.cues.push_back(cue);

// Export as SRT
std::string srt = ofxGgmlMontagePlanner::buildSrt(track);
ofFile("output.srt", ofFile::WriteOnly) << srt;

// Export as VTT
std::string vtt = ofxGgmlMontagePlanner::buildVtt(track);
ofFile("output.vtt", ofFile::WriteOnly) << vtt;
```

## WebVTT Styling

### Cue Settings

WebVTT supports advanced positioning and styling through cue settings:

```cpp
ofxGgmlMontageSubtitleCue cue;
cue.startSeconds = 0.0;
cue.endSeconds = 2.5;
cue.text = "Centered subtitle";

// Configure VTT styling
cue.vttSettings.position = "50%";     // Horizontal center
cue.vttSettings.line = "85%";         // Near bottom
cue.vttSettings.size = "80%";         // 80% width
cue.vttSettings.align = "center";     // Center alignment

track.cues.push_back(cue);
```

#### Available VTT Settings

- **position**: Horizontal position (e.g., "50%" for center)
- **line**: Vertical position (e.g., "85%" for near bottom, "-1" for top)
- **size**: Width of cue box (e.g., "80%")
- **align**: Text alignment ("start", "center", "end", "left", "right")
- **vertical**: Text direction ("lr" or "rl" for vertical text)
- **region**: Named region identifier

### Example: Multiple Speakers

```cpp
// Speaker 1 - top of screen
ofxGgmlMontageSubtitleCue speaker1;
speaker1.startSeconds = 0.0;
speaker1.endSeconds = 2.0;
speaker1.text = "Speaker 1: Hello!";
speaker1.vttSettings.line = "10%";
speaker1.vttSettings.align = "left";

// Speaker 2 - bottom of screen
ofxGgmlMontageSubtitleCue speaker2;
speaker2.startSeconds = 2.5;
speaker2.endSeconds = 4.5;
speaker2.text = "Speaker 2: Hi there!";
speaker2.vttSettings.line = "85%";
speaker2.vttSettings.align = "right";
```

## Subtitle Validation

### Basic Validation

```cpp
#include "support/ofxGgmlSubtitleHelpers.h"

auto validation = ofxGgmlMontagePlanner::validateSubtitleTrack(track);

if (validation.valid) {
    ofLogNotice() << "Subtitles are valid!";
} else {
    ofLogError() << "Validation failed with " << validation.errors.size() << " errors";

    for (const auto& error : validation.errors) {
        ofLogError() << "  - " << error;
    }
}

// Check warnings even if valid
if (!validation.warnings.empty()) {
    ofLogWarning() << validation.warnings.size() << " warnings found:";
    for (const auto& warning : validation.warnings) {
        ofLogWarning() << "  - " << warning;
    }
}
```

### Validation Checks

The validator detects:

**Errors** (makes subtitles invalid):
- End time before start time
- Negative time values
- Overlapping cues
- Invalid timing sequences

**Warnings** (quality issues):
- Very short cues (< 0.3 seconds)
- Very long cues (> 7 seconds)
- Fast reading speed (> 200 WPM)
- Empty text
- Large gaps between cues

## Quality Metrics

### Calculate Metrics

```cpp
auto metrics = ofxGgmlMontagePlanner::calculateSubtitleMetrics(track);

ofLogNotice() << "Total cues: " << metrics.totalCues;
ofLogNotice() << "Total duration: " << metrics.totalDurationSeconds << "s";
ofLogNotice() << "Average cue duration: " << metrics.averageCueDurationSeconds << "s";
ofLogNotice() << "Average reading speed: " << metrics.averageWordsPerMinute << " WPM";

// Check for issues
if (metrics.overlapCount > 0) {
    ofLogWarning() << metrics.overlapCount << " overlapping cues detected";
}

if (metrics.tooFastCount > 0) {
    ofLogWarning() << metrics.tooFastCount << " cues with fast reading speed";
}

// Format complete summary
std::string summary = ofxGgmlSubtitleHelpers::formatMetricsSummary(metrics);
ofLogNotice() << summary;
```

### Metrics Available

- `totalCues`: Number of subtitle cues
- `totalDurationSeconds`: Total subtitle timeline duration
- `averageCueDurationSeconds`: Average display time per cue
- `minCueDurationSeconds`: Shortest cue duration
- `maxCueDurationSeconds`: Longest cue duration
- `totalWords`: Total word count
- `averageWordsPerMinute`: Overall reading speed
- `overlapCount`: Number of overlapping cues
- `gapCount`: Number of significant gaps
- `totalGapDurationSeconds`: Total gap time
- `tooShortCount`: Cues under 0.3 seconds
- `tooLongCount`: Cues over 7 seconds
- `tooFastCount`: Cues requiring > 200 WPM reading speed

## Timing Utilities

### Offset Timing

Shift all subtitles forward or backward:

```cpp
// Delay all subtitles by 2 seconds
ofxGgmlSubtitleHelpers::offsetTiming(track.cues, 2.0);

// Advance all subtitles by 1 second (negative offset)
ofxGgmlSubtitleHelpers::offsetTiming(track.cues, -1.0);
```

### Scale Timing

Speed up or slow down subtitle timing:

```cpp
// Slow down by 50% (1.5x duration)
ofxGgmlSubtitleHelpers::scaleTiming(track.cues, 1.5);

// Speed up by 50% (0.5x duration)
ofxGgmlSubtitleHelpers::scaleTiming(track.cues, 0.5);
```

### Synchronization Example

```cpp
// Synchronize subtitles that drift over time
void synchronizeSubtitles(
    std::vector<ofxGgmlMontageSubtitleCue>& cues,
    double initialOffsetSeconds,
    double scaleFactor) {

    // First apply scaling to fix drift
    ofxGgmlSubtitleHelpers::scaleTiming(cues, scaleFactor);

    // Then apply offset to align start
    ofxGgmlSubtitleHelpers::offsetTiming(cues, initialOffsetSeconds);
}
```

## Merging and Splitting

### Merge Close Cues

Combine cues that are very close together:

```cpp
// Merge cues with gaps of 0.5 seconds or less
auto merged = ofxGgmlSubtitleHelpers::mergeCues(track.cues, 0.5);

ofLogNotice() << "Merged " << track.cues.size()
              << " cues into " << merged.size();

// Use merged cues
track.cues = merged;
```

### Split Long Cues

Break long cues into smaller segments:

```cpp
// Split cues longer than 7 seconds
auto split = ofxGgmlSubtitleHelpers::splitLongCues(track.cues, 7.0);

ofLogNotice() << "Split " << track.cues.size()
              << " cues into " << split.size();

// Use split cues
track.cues = split;
```

### Optimization Workflow

```cpp
void optimizeSubtitles(ofxGgmlMontageSubtitleTrack& track) {
    // 1. Split overly long cues
    track.cues = ofxGgmlSubtitleHelpers::splitLongCues(track.cues, 7.0);

    // 2. Merge very short segments
    track.cues = ofxGgmlSubtitleHelpers::mergeCues(track.cues, 0.3);

    // 3. Validate results
    auto validation = ofxGgmlMontagePlanner::validateSubtitleTrack(track);

    if (!validation.valid) {
        ofLogError() << "Optimization produced invalid subtitles";
        for (const auto& error : validation.errors) {
            ofLogError() << "  " << error;
        }
    }

    // 4. Check quality
    auto metrics = ofxGgmlMontagePlanner::calculateSubtitleMetrics(track);
    ofLogNotice() << ofxGgmlSubtitleHelpers::formatMetricsSummary(metrics);
}
```

## Reading Speed Analysis

### Check Reading Speed

```cpp
// Calculate reading speed for individual cue
double duration = cue.endSeconds - cue.startSeconds;
double wpm = ofxGgmlSubtitleHelpers::calculateReadingSpeed(cue.text, duration);

if (wpm > 200.0) {
    ofLogWarning() << "Too fast: " << wpm << " WPM";
} else if (wpm < 120.0 && wpm > 0.0) {
    ofLogNotice() << "Comfortable: " << wpm << " WPM";
}
```

### Reading Speed Guidelines

- **< 120 WPM**: Very comfortable, possibly too slow
- **120-180 WPM**: Ideal reading speed for most content
- **180-200 WPM**: Fast but manageable
- **> 200 WPM**: Too fast for comfortable reading

## Integration with Montage Workflow

### From Speech to Subtitles

```cpp
// 1. Transcribe audio with Whisper
ofxGgmlSpeechInferenceRequest speechRequest;
speechRequest.task = ofxGgmlSpeechTask::Transcribe;
speechRequest.audioPath = "interview.wav";

auto speechResult = speechInference.infer(speechRequest);

// 2. Convert to montage segments
auto segments = ofxGgmlMontagePlanner::segmentsFromSpeechSegments(
    speechResult.segments, "INTERVIEW");

// 3. Plan montage
ofxGgmlMontagePlannerRequest montageRequest;
montageRequest.goal = "Best interview moments";
montageRequest.segments = segments;
montageRequest.maxClips = 10;

auto montageResult = ofxGgmlMontagePlanner::plan(montageRequest);

// For headless automation, ofxGgmlEasy::planMontageFromSrt(...) applies the
// same montage defaults (12s target, 1.5s spacing, 0.25s/0.35s handles) and
// lets you override spacing/handles, target duration, drop-frame EDL output,
// or a source-file path without building the request manually.

// 4. Generate subtitles
auto subtitleTrack = ofxGgmlMontagePlanner::buildSubtitleTrack(
    montageResult.plan, "INTERVIEW_HIGHLIGHTS");

// 5. Validate and optimize
auto validation = ofxGgmlMontagePlanner::validateSubtitleTrack(subtitleTrack);
if (validation.valid) {
    subtitleTrack.cues = ofxGgmlSubtitleHelpers::splitLongCues(
        subtitleTrack.cues, 7.0);
}

// 6. Export
std::string srt = ofxGgmlMontagePlanner::buildSrt(subtitleTrack);
ofFile("highlights.srt", ofFile::WriteOnly) << srt;
```

## Advanced Features

### Custom Validation Rules

```cpp
bool validateCustomRules(const ofxGgmlMontageSubtitleTrack& track) {
    bool valid = true;

    for (size_t i = 0; i < track.cues.size(); ++i) {
        const auto& cue = track.cues[i];

        // Custom rule: No cue longer than 5 seconds
        if (cue.endSeconds - cue.startSeconds > 5.0) {
            ofLogWarning() << "Cue " << i << " exceeds 5 second limit";
            valid = false;
        }

        // Custom rule: Minimum gap of 0.1 seconds
        if (i + 1 < track.cues.size()) {
            double gap = track.cues[i + 1].startSeconds - cue.endSeconds;
            if (gap < 0.1 && gap > 0.0) {
                ofLogWarning() << "Gap too small between cue " << i
                               << " and " << (i + 1);
                valid = false;
            }
        }
    }

    return valid;
}
```

### Batch Processing

```cpp
void processSubtitleDirectory(const std::string& directory) {
    ofDirectory dir(directory);
    dir.allowExt("srt");
    dir.listDir();

    for (size_t i = 0; i < dir.size(); ++i) {
        std::string path = dir.getPath(i);

        // Load and parse
        std::vector<ofxGgmlSimpleSrtCue> cues;
        std::string error;

        if (!ofxGgmlSimpleSrtSubtitleParser::parseFile(path, cues, error)) {
            ofLogError() << "Failed to parse " << path << ": " << error;
            continue;
        }

        // Convert to montage format
        ofxGgmlMontageSubtitleTrack track;
        for (const auto& cue : cues) {
            ofxGgmlMontageSubtitleCue montageCue;
            montageCue.startSeconds = cue.startMs / 1000.0;
            montageCue.endSeconds = cue.endMs / 1000.0;
            montageCue.text = cue.text;
            track.cues.push_back(montageCue);
        }

        // Validate and report
        auto validation = ofxGgmlMontagePlanner::validateSubtitleTrack(track);
        auto metrics = ofxGgmlMontagePlanner::calculateSubtitleMetrics(track);

        ofLogNotice() << "\n" << path;
        ofLogNotice() << "  " << validation.summary();
        ofLogNotice() << "  Avg WPM: " << metrics.averageWordsPerMinute;
    }
}
```

## Best Practices

### 1. Always Validate

```cpp
// Before exporting, always validate
auto validation = ofxGgmlMontagePlanner::validateSubtitleTrack(track);

if (!validation.valid) {
    // Fix errors before proceeding
    ofLogError() << "Cannot export: " << validation.errors.size() << " errors";
    return;
}
```

### 2. Check Reading Speed

```cpp
// Monitor reading speed across the entire track
auto metrics = ofxGgmlMontagePlanner::calculateSubtitleMetrics(track);

if (metrics.averageWordsPerMinute > 180.0) {
    ofLogWarning() << "Consider extending cue durations or splitting text";
}
```

### 3. Use Appropriate Durations

```cpp
// Ensure minimum display time
for (auto& cue : track.cues) {
    double duration = cue.endSeconds - cue.startSeconds;
    if (duration < 0.8) {
        cue.endSeconds = cue.startSeconds + 0.8;
    }
}
```

### 4. Test in Target Player

Different subtitle players may have varying support for:
- WebVTT cue settings
- Multi-line text
- Special characters
- UTF-8 encoding

Always test exported subtitles in the target player.

## Troubleshooting

### Common Issues

**Issue**: Subtitles appear too fast
```cpp
// Solution: Scale timing to slow down
ofxGgmlSubtitleHelpers::scaleTiming(track.cues, 1.2);  // 20% slower
```

**Issue**: Overlapping subtitles
```cpp
// Solution: Validate and fix overlaps
auto validation = ofxGgmlMontagePlanner::validateSubtitleTrack(track);
for (const auto& error : validation.errors) {
    if (error.find("overlap") != std::string::npos) {
        // Manually adjust timing or split cues
    }
}
```

**Issue**: Long cues are hard to read
```cpp
// Solution: Automatically split long cues
track.cues = ofxGgmlSubtitleHelpers::splitLongCues(track.cues, 5.0);
```

## See Also

- `ofxGgmlMontagePlanner.h` - Montage planning and subtitle generation
- `ofxGgmlSubtitleHelpers.h` - Subtitle utilities and helpers
- `ofxGgmlSimpleSrtSubtitleParser.h` - SRT parsing
- `docs/EDL_EXPORT.md` - EDL export documentation
- `tests/test_subtitle_helpers.cpp` - Comprehensive test examples
