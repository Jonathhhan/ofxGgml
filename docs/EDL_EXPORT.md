# EDL Export

The `ofxGgmlMontagePlanner` provides CMX-style EDL (Edit Decision List) export functionality for timeline handoff to professional editing applications like Avid Media Composer, Adobe Premiere Pro, DaVinci Resolve, and Final Cut Pro.

## Overview

EDL files are industry-standard text files that describe edit sequences. They specify:
- Source reel names
- In/out timecodes for source clips
- Record timecodes for the timeline
- Transitions and effects
- Audio and video tracks
- Metadata comments

## Basic Usage

### Simple Video-Only EDL

```cpp
#include "ofxGgml.h"

// Create a montage plan from subtitle segments
ofxGgmlMontagePlannerRequest request;
request.goal = "Build an exciting action montage";
request.segments = segments;  // Your subtitle segments
request.maxClips = 8;
request.sourceFilePath = "/path/to/source/video.mp4";

auto result = ofxGgmlMontagePlanner::plan(request);
if (result.success) {
    // Export as EDL
    std::string edl = ofxGgmlMontagePlanner::buildEdl(
        result.plan,
        "ACTION_MONTAGE",  // Title
        25,                // FPS
        false);            // Drop-frame timecode

    // Save to file
    ofFile edlFile("montage.edl", ofFile::WriteOnly);
    edlFile << edl;
}
```

### EDL with Audio Tracks

```cpp
// Export EDL with both video and audio tracks
std::string edlWithAudio = ofxGgmlMontagePlanner::buildEdlWithAudio(
    result.plan,
    "ACTION_MONTAGE",
    25,
    false);

// The output includes separate V (video) and A (audio) track entries
```

## Advanced Features

### Drop-Frame Timecode

For NTSC video at 29.97fps, use drop-frame timecode:

```cpp
request.dropFrameTimecode = true;

std::string edl = ofxGgmlMontagePlanner::buildEdl(
    result.plan,
    "NTSC_MONTAGE",
    30,     // 29.97 fps represented as 30
    true);  // Enable drop-frame
```

Drop-frame timecode uses semicolons (`;`) instead of colons (`:`) as the frame separator and compensates for the NTSC frame rate drift.

### Source File Path References

Include source file paths in the EDL for easier media management:

```cpp
request.sourceFilePath = "/media/project/footage/interview_01.mov";

// EDL will include:
// * SOURCE FILE: /media/project/footage/interview_01.mov
```

### Transition Metadata

Add transition information to clips:

```cpp
// After planning, customize clips
for (auto & clip : result.plan.clips) {
    clip.transitionDurationFrames = 12;  // 12-frame dissolve
}

std::string edl = ofxGgmlMontagePlanner::buildEdl(result.plan, "MONTAGE", 25, false);
// Includes: * TRANSITION DURATION: 12 frames
```

### Custom Audio Track Assignments

Control audio track routing per clip:

```cpp
// After planning, customize audio tracks
result.plan.clips[0].audioTrack = "A";      // Channel 1
result.plan.clips[1].audioTrack = "A2";     // Channel 2
result.plan.clips[2].audioTrack = "NONE";   // Video only, no audio

std::string edl = ofxGgmlMontagePlanner::buildEdlWithAudio(result.plan, "MONTAGE", 25, false);
```

## EDL Format Details

### Structure

A typical EDL entry looks like:

```
001  AX       V     C        00:00:10:00 00:00:15:00 00:00:00:00 00:00:05:00
* FROM CLIP NAME: SEG_1
* SOURCE FILE: /media/footage/clip.mp4
* COMMENT: Opening establishing shot
* THEME: cityscape
* TRANSITION: Open clean on the first strong beat.
* TRANSITION DURATION: 12 frames
```

Breaking this down:
- `001` - Edit number
- `AX` - Reel name (source tape/file identifier)
- `V` - Video track
- `C` - Cut transition (vs D for dissolve, W for wipe)
- `00:00:10:00` - Source in timecode (HH:MM:SS:FF)
- `00:00:15:00` - Source out timecode
- `00:00:00:00` - Record in timecode (timeline position)
- `00:00:05:00` - Record out timecode

### Header

```
TITLE: ACTION_MONTAGE
FCM: NON-DROP FRAME

001  AX       V     C        ...
```

- `TITLE:` - Project/sequence name
- `FCM:` - Frame count mode (DROP FRAME or NON-DROP FRAME)

### Audio Tracks

When using `buildEdlWithAudio()`:

```
001  AX       V     C        00:00:10:00 00:00:15:00 00:00:00:00 00:00:05:00
001  AX       A     C        00:00:10:00 00:00:15:00 00:00:00:00 00:00:05:00
```

Both video (V) and audio (A) tracks share the same edit number and timecodes.

## Frame Rate Support

Common frame rates:
- **24 fps** - Film standard
- **25 fps** - PAL video standard
- **30 fps** - NTSC video (use with drop-frame for 29.97)
- **60 fps** - High frame rate video

```cpp
// PAL project at 25fps
buildEdl(plan, "PAL_EDIT", 25, false);

// NTSC project at 29.97fps (drop-frame)
buildEdl(plan, "NTSC_EDIT", 30, true);

// Film project at 24fps
buildEdl(plan, "FILM_EDIT", 24, false);
```

## GUI Example Integration

The GUI example provides EDL export through the Montage workflow:

1. Load a subtitle file (`.srt`)
2. Set a montage goal
3. Click "Plan Montage"
4. Click "Copy EDL" to copy to clipboard
5. Paste into your NLE timeline import dialog

## Compatibility

EDL files generated by `ofxGgmlMontagePlanner` are compatible with:

- **Avid Media Composer** - Full CMX 3600 support
- **Adobe Premiere Pro** - Import via File → Import → EDL
- **DaVinci Resolve** - Import via File → Import → Timeline → EDL
- **Final Cut Pro** - Import via File → Import → EDL

## Best Practices

### Reel Names

- Keep reel names short (max 8 characters)
- Use alphanumeric characters only
- Consistent naming helps media management

```cpp
request.fallbackReelName = "PROJ01";  // Project reel prefix
```

### Handles

Add pre-roll and post-roll for editorial flexibility:

```cpp
request.preRollSeconds = 1.0;   // 1 second before subtitle
request.postRollSeconds = 1.5;  // 1.5 seconds after
```

Handles give editors room to adjust cuts without running out of media.

### Spacing

Prevent clips from clustering too close together:

```cpp
request.minSpacingSeconds = 2.0;  // At least 2 seconds apart
```

This ensures visual variety in the montage.

## Complete Example

```cpp
#include "ofxGgml.h"

void exportMontageEdl() {
    // Load subtitle segments
    auto segmentsResult = ofxGgmlMontagePlanner::loadSegmentsFromSrt(
        "data/subtitles/interview.srt",
        "INTV01");

    if (!segmentsResult.success) {
        ofLogError() << "Failed to load subtitles: " << segmentsResult.error;
        return;
    }

    // Configure montage request
    ofxGgmlMontagePlannerRequest request;
    request.goal = "Compelling interview highlights";
    request.segments = segmentsResult.value;
    request.maxClips = 10;
    request.minScore = 0.2;
    request.minSpacingSeconds = 3.0;
    request.preRollSeconds = 1.0;
    request.postRollSeconds = 1.5;
    request.targetDurationSeconds = 60.0;
    request.preserveChronology = true;
    request.sourceFilePath = "/media/project/interview_master.mov";
    request.dropFrameTimecode = false;  // PAL 25fps

    // Plan the montage
    auto planResult = ofxGgmlMontagePlanner::plan(request);
    if (!planResult.success) {
        ofLogError() << "Planning failed: " << planResult.error;
        return;
    }

    // Export EDL with audio
    std::string edl = ofxGgmlMontagePlanner::buildEdlWithAudio(
        planResult.plan,
        "INTERVIEW_MONTAGE",
        25,    // 25fps PAL
        false);

    // Save to file
    ofFile edlFile("exports/interview_montage.edl", ofFile::WriteOnly);
    edlFile << edl;

    ofLogNotice() << "EDL exported: "
                  << planResult.plan.clips.size() << " clips, "
                  << ofxGgmlMontagePlanner::computePlanDurationSeconds(planResult.plan)
                  << " seconds";
}
```

## Limitations

- EDL format is text-based and linear; complex nested timelines may lose structure
- Effect parameters beyond basic transitions are stored as comments only
- Multi-camera angles require separate EDL files or extensions
- Color correction and audio mixing are not included

## See Also

- `ofxGgmlMontagePlanner.h` - Core montage planning API
- `ofxGgmlEasy::planMontageFromSrt()` - High-level facade
- `docs/FEATURE_SYNERGIES.md` - Workflow integration examples
- CMX 3600 EDL specification (industry standard)
