# Workflows Guide

Learn how to use ofxGgml's specialized creative and research workflows for video, montage, music, and citation-based research.

## Overview

Include `ofxGgmlWorkflows.h` to get:
- All modalities (speech, vision, TTS, diffusion, CLIP)
- Video planning and beat generation
- Montage editing from subtitles
- Citation search and source-grounded research
- Video essay pipeline (topic → script → narration)
- Music generation and ABC notation
- MilkDrop visualization presets
- Web crawling for RAG pipelines

## Video Planning

### Single-Scene Beat Planning

Generate prompt beats for one video clip:

```cpp
#include "ofxGgmlWorkflows.h"

ofxGgmlVideoPlanner planner;
planner.setInference(&inference);

ofxGgmlVideoPlanRequest request;
request.description = "City timelapse footage";
request.goal = "Create a dynamic social media short";
request.beatCount = 5;
request.duration = 15;  // seconds

auto plan = planner.planBeats(request);

for (auto& beat : plan.beats) {
    cout << beat.timing << ": " << beat.prompt << endl;
}
```

### Multi-Scene Generation Plan

Plan a sequence with consistent entities:

```cpp
request.sceneCount = 3;
request.description = "Sci-fi corridor exploration";
request.entities = "Robot character, neon lights, alien architecture";

auto multiScene = planner.planMultiScene(request);

for (size_t i = 0; i < multiScene.scenes.size(); i++) {
    cout << "Scene " << (i+1) << ": " << multiScene.scenes[i].description << endl;
    cout << "Prompt: " << multiScene.scenes[i].prompt << endl;
}
```

### AI-Assisted Edit Plan

Get structured editing instructions:

```cpp
auto editPlan = planner.planEdit(
    "Berlin travel footage",
    "Create a trailer-style edit",
    "Opening: skyline. Middle: street scenes. End: landmark closeup"
);

for (auto& action : editPlan.actions) {
    cout << action.timecode << " - " << action.type << ": "
         << action.description << endl;
}
```

## Montage Planning

### From Subtitle File

Create a montage from transcript cues:

```cpp
ofxGgmlMontagePlanner montage;
montage.setInference(&inference);

ofxGgmlMontagePlannerRequest request;
request.subtitlePath = "interview.srt";
request.goal = "Extract key emotional moments";
request.maxClips = 8;
request.targetDuration = 60;  // seconds

auto plan = montage.plan(request);

// Ranked clips
for (auto& clip : plan.clips) {
    cout << clip.startTime << " → " << clip.endTime << endl;
    cout << "  Text: " << clip.text << endl;
    cout << "  Score: " << clip.score << endl;
}
```

### Export EDL for NLE

```cpp
// Export CMX EDL for Premiere/Resolve/Avid
string edl = montage.buildEdl(
    plan.clips,
    "My Montage",
    30.0,  // frame rate
    false  // not drop-frame
);

ofSaveBuffer("montage.edl", edl);
```

### With Audio Track

```cpp
string edlWithAudio = montage.buildEdlWithAudio(
    plan.clips,
    "Montage with Audio",
    30.0,
    "source-video.mp4",  // source file
    "A",  // audio track (A, A2, A3...)
    false
);
```

## Citation Search

### Search for Cited Sources

```cpp
ofxGgmlCitationSearch citations;
citations.setInference(&inference);
citations.setWebCrawler(&crawler);  // Optional

auto results = citations.search(
    "Berlin airport winter disruption 2024",
    {},  // no pre-loaded URLs
    "https://example.com/weather",  // seed URL to crawl
    5  // max citations
);

for (auto& citation : results.citations) {
    cout << "[" << citation.index << "] " << citation.sourceUrl << endl;
    cout << "Quote: " << citation.quote << endl;
    cout << "Context: " << citation.context << endl;
}
```

### From Loaded Sources

```cpp
vector<string> sourceUrls = {
    "https://news.example.com/story1",
    "https://blog.example.com/post2"
};

auto results = citations.search(
    "Climate policy changes",
    sourceUrls,
    "",  // no crawling
    3
);
```

## Video Essay Workflow

Complete pipeline from topic to script to narration:

```cpp
ofxGgmlVideoEssayWorkflow essay;
essay.setInference(&inference);
essay.setCitationSearch(&citationSearch);
essay.setVideoPlanner(&videoPlanner);

ofxGgmlVideoEssayRequest request;
request.topic = "The history of mechanical keyboards";
request.targetDuration = 180;  // 3 minutes
request.voiceModel = "en_US-lessac-medium";
request.sourceUrls = {"https://wiki.example.com/keyboards"};

auto result = essay.generate(request);

if (result.success) {
    // Cited outline
    cout << "Outline:\n" << result.outline << endl;

    // Narrated script
    cout << "Script:\n" << result.script << endl;

    // SRT subtitle file
    ofSaveBuffer("essay.srt", result.srtContent);

    // Optional: visual concept and scene plan
    if (result.hasVisualPlan) {
        cout << "Visual concept: " << result.visualConcept << endl;
    }
}
```

## Music Generation

### Music Prompt Generation

```cpp
ofxGgmlMusicGenerator music;
music.setInference(&inference);

auto prompt = music.generatePrompt(
    "Dreamy rainy neon city at night",
    "ambient electronica",
    "soft analog synths, sub bass, light vinyl crackle",
    45  // duration seconds
);

if (prompt.success) {
    cout << "Music prompt: " << prompt.prompt << endl;
}
```

### ABC Notation Sketch

Local-first music notation:

```cpp
auto abc = music.generateABCNotation(
    "Playful hand-drawn city chase",
    "quirky chamber pop",
    "pizzicato strings and clarinet",
    16  // bars
);

if (abc.success) {
    ofSaveBuffer("chase.abc", abc.notationText);
}
```

### Image → Music

```cpp
ofxGgmlMediaPromptGenerator media;
media.setInference(&inference);

auto musicFromImage = media.generateImageToMusicPrompt(
    "Orange dusk over a harbor with slow boats",
    "gentle movement, reflective mood",
    "cinematic ambient",
    "warm piano and textured pads"
);
```

### With AceStep Server (Rendered Audio)

```cpp
ofxGgmlAceStepBridge aceStep;
aceStep.configure("http://127.0.0.1:8090");

ofxGgmlAceStepRequest request;
request.prompt = musicPrompt.prompt;
request.duration = 30;

auto audio = aceStep.generate(request);

if (audio.success) {
    ofSaveBuffer("output.wav", audio.audioData);
}
```

## MilkDrop Preset Generation

### Generate Visualization Preset

```cpp
ofxGgmlMilkDropGenerator milkdrop;
milkdrop.setInference(&inference);

auto preset = milkdrop.generate(
    "Neon kaleidoscope tunnel with bass-reactive zoom pulses",
    "Geometric",  // style hint
    0.65  // complexity 0-1
);

if (preset.success) {
    milkdrop.savePreset(preset.presetText, "neon-tunnel.milk");
}
```

### Generate Variants

```cpp
auto variants = milkdrop.generateVariants(
    "Liquid neon lattice with soft beat pulses",
    "Liquid",
    0.6,
    3  // number of variants
);

for (size_t i = 0; i < variants.presets.size(); i++) {
    milkdrop.savePreset(variants.presets[i].presetText,
        "variant-" + ofToString(i) + ".milk");
}
```

### Validate and Repair

```cpp
auto validation = milkdrop.validate(preset.presetText);

if (!validation.valid) {
    cout << "Errors: " << validation.errorCount << endl;

    // Attempt repair
    auto repaired = milkdrop.repair(preset.presetText);
    if (repaired.success) {
        preset.presetText = repaired.presetText;
    }
}
```

## Web Crawling

### Crawl Website for RAG

```cpp
ofxGgmlWebCrawler crawler;

ofxGgmlWebCrawlerSettings settings;
settings.executablePath = "mojo";  // Auto-discovered
settings.startUrl = "https://docs.example.com";
settings.maxDepth = 2;
settings.renderJavaScript = false;

auto result = crawler.crawl(settings);

if (result.success) {
    cout << "Crawled " << result.documents.size() << " pages\n";

    for (auto& doc : result.documents) {
        cout << doc.url << " (" << doc.content.length() << " chars)\n";
    }
}
```

### Use with Citation Search

```cpp
ofxGgmlCitationSearch citations;
citations.setWebCrawler(&crawler);

// This will crawl the seed URL first
auto cited = citations.search(
    "API authentication best practices",
    {},
    "https://api-docs.example.com",
    4
);
```

## RAG Pipeline

### Build Retrieval Pipeline

```cpp
ofxGgmlRAGPipeline rag;
rag.setInference(&inference);
rag.setWebCrawler(&crawler);

// Index documents
rag.indexUrl("https://docs.example.com");
rag.indexFile("local-doc.md");

// Query with context
auto answer = rag.query(
    "How do I authenticate API requests?",
    5  // top-k results
);

if (answer.success) {
    cout << "Answer: " << answer.text << endl;
    cout << "Sources: " << answer.sourceCount << endl;
}
```

## Performance Tips

### Video Planning
- Use smaller models for beat planning (1.5B-3B)
- Larger models (7B+) for detailed scene descriptions
- Cache entity descriptions for multi-scene consistency

### Montage
- Pre-load subtitle files once, plan multiple montages
- Use embeddings for semantic similarity scoring
- EDL export is fast, doesn't need GPU

### Citation Search
- Crawl once, search multiple times
- Use domain restrictions to limit scope
- Cache citation results for reuse

### Music Generation
- Prompt generation is fast (local text)
- ABC notation is instant
- Rendered audio requires external server (slow)

## Integration Examples

### Video Essay → Montage

```cpp
// 1. Generate essay with SRT
auto essay = videoEssay.generate(request);

// 2. Use SRT for montage planning
montagePlanner.planFromSrt(essay.srtContent, "Extract key quotes");

// 3. Export to editor
string edl = montagePlanner.buildEdl(plan.clips, "Essay Montage", 30.0);
```

### Citation → Video Essay

```cpp
// 1. Search for sources
auto citations = citationSearch.search(topic, {}, seedUrl, 5);

// 2. Build essay from citations
essayRequest.topic = topic;
essayRequest.sourceUrls = /* extract from citations */;
auto essay = videoEssay.generate(essayRequest);
```

### Music → Image → Video

```cpp
// 1. Generate music prompt
auto musicPrompt = musicGen.generatePrompt(...);

// 2. Convert to image prompt
auto imagePrompt = mediaGen.generateMusicToImagePrompt(musicPrompt.prompt);

// 3. Generate images
diffusion.generate(imagePrompt.prompt, settings);

// 4. Plan music video
videoPlanner.planMusicVideo(musicPrompt.prompt, beatCount);
```

## Next Steps

- **Code assistants**: [ASSISTANTS.md](ASSISTANTS.md)
- **Performance tuning**: [../PERFORMANCE.md](../PERFORMANCE.md)
- **Integration guide**: [../OFXGGML_STABLEDIFFUSION_INTEGRATION.md](../OFXGGML_STABLEDIFFUSION_INTEGRATION.md)

## Examples

- `ofxGgmlGuiExample` - All workflows in GUI
  - Video mode: Planning and editing
  - Vision mode: Music video workflow
  - Summarize mode: Citation research
  - MilkDrop mode: Preset generation
