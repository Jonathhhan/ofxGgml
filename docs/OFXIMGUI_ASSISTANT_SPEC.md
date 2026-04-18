# ofxImGui Assistant Spec

This document defines a specialized assistant/skill for `ofxImGui` and openFrameworks GUI architecture.

The goal is not to create a separate generic coding assistant, but to add a focused layer that is better at:

- building and refactoring `ofxImGui` panels
- preserving existing openFrameworks project style
- avoiding common immediate-mode UI bugs
- wiring image/video/FBO previews correctly
- splitting large GUI shells into maintainable mode files

## Why a Specialized Assistant

`ofxImGui` work is not just "write Dear ImGui code".

In this addon stack, good GUI changes usually need to respect:

- openFrameworks application lifecycle (`setup`, `update`, `draw`, `exit`)
- `ofApp`-style mode routing and split translation units
- `std::string` / `char` buffer handoff for `ImGui::InputText`
- deferred state updates when buttons mutate already-rendered inputs
- `ofImage`, `ofTexture`, `ofFbo`, video, and `ofxImGui::AddImage(...)` preview rules
- session persistence and cross-mode handoff state
- OF naming/style conventions instead of raw upstream Dear ImGui examples

That means a general coding assistant often produces technically valid ImGui code that still feels wrong in this codebase.

## Primary Use Cases

The assistant should be optimized for:

1. Building a new panel or mode
2. Refactoring a large panel out of `ofApp.cpp`
3. Fixing broken `InputText` / button / preview state behavior
4. Improving layout and usability of an existing panel
5. Adding session persistence for GUI state
6. Wiring preview media correctly for images, video, subtitles, or generated assets
7. Reviewing `ofxImGui` code for common immediate-mode mistakes

## Recommended Integration Path

There are two valid implementation paths:

### Option A: Dedicated `ofxGgml` Assistant Mode

Add a UI-specialized mode on top of the existing assistant stack.

Suggested action enum entry:

```cpp
// example future addition
UIOfxImGui
```

This mode would live closest to `ofxGgmlCodeAssistant`, but with a dedicated prompt builder and retrieval policy.

### Option B: Specialized Skill / Prompt Profile

Keep the current assistant classes intact and add a specialized skill/prompt preset for:

- `ofxImGui`
- openFrameworks GUI architecture
- current repo split style

This is the best short-term path because it is low risk and can reuse the existing coding/review loop directly.

## Recommendation

Implement this in phases:

1. **Phase 1**
   Add a specialized prompt/profile on top of `ofxGgmlCodeAssistant`
2. **Phase 2**
   Add UI-specific retrieval and heuristics
3. **Phase 3**
   Add a dedicated assistant mode if the behavior proves stable enough

## Assistant Contract

The assistant should behave like a UI-focused teammate, not a generic code generator.

It should:

- inspect existing GUI files before proposing edits
- identify the owning mode/panel file before changing code
- preserve OF-style code organization
- prefer extending existing helpers over adding ad-hoc local logic
- explain what UI state it is changing and where that state lives
- verify `Begin` / `End` balance and preview resource lifecycle

It should avoid:

- introducing raw upstream Dear ImGui patterns that ignore openFrameworks conventions
- mutating already-rendered input buffers in the same frame without deferred state handling
- adding one-off helper state when a shared utility already exists
- mixing unrelated panel logic into `ofApp.cpp` when a split file is the better home

## Retrieval Strategy

The assistant should prioritize a narrow, UI-aware context pack.

### Primary Retrieval Inputs

- current panel file
- `ofApp.h`
- shared GUI helpers in `src/utils/`
- session persistence file if state is persisted
- preview/media helpers if image/video/subtitle content is involved

### Ranking Rules

Rank files higher when they contain:

- `draw...Panel()` ownership
- `ImGui::` calls matching the user task
- `ofxImGui::AddImage(...)`
- `InputText`, `Combo`, `BeginChild`, `TreeNode`, `TabBar`, `Selectable`
- panel/session helpers already used by neighboring modes
- recent edits for the same mode

### Context Expansion Rules

Pull more context only when needed:

- preview resources: image/video/FBO utilities
- deferred UI updates: shared ImGui helper functions
- persistence: session save/load helpers
- cross-mode handoff: source/target panel files

## UI-Specific Heuristics

The assistant should explicitly check for these patterns.

### 1. Input Buffer Safety

When a button copies text into a field already rendered this frame:

- do not overwrite the live ImGui-owned visible state directly
- use deferred buffer updates or a shared helper

Common bug pattern:

- button appears to do nothing
- backing string changes, but visible input does not update until later

### 2. Preview Rendering Rules

When rendering OF media into ImGui:

- prefer existing `ofxImGui::AddImage(...)` patterns
- be careful with `GL_TEXTURE_RECTANGLE` vs `GL_TEXTURE_2D`
- respect `ofDisableArbTex()` decisions already made by the app
- use shared preview helpers instead of hand-rolled texture logic

### 3. Lifecycle Ownership

When adding preview or playback state:

- initialize in `setup()` or lazily in helpers
- update in `update()`
- release in `exit()` if needed
- avoid leaking player/texture/FBO state into unrelated modes

### 4. Session Persistence

When adding user-facing GUI state:

- decide whether it is transient or persistent
- if persistent, wire it through session save/load consistently
- reset derived workflow state when the source plan/config changes

### 5. Panel Ownership

When a panel becomes large:

- prefer moving it into a mode-specific translation unit
- keep `ofApp.cpp` as coordinator where possible
- preserve naming already used by the example split

## Output Shape

The assistant should return structured UI-oriented results.

Recommended sections:

- `Plan`
- `Patch intent`
- `Touched files`
- `Verification`
- `Risks`

For code edits, it should explicitly name:

- owning panel function
- backing state fields
- whether state is deferred, persistent, or preview-only
- which helpers are reused vs newly introduced

## Review Checklist

When used in review mode, the assistant should check:

- unmatched `Begin` / `End`
- unstable ImGui IDs
- widget state stored in local temporaries instead of owned state
- input buffer mismatch bugs
- hidden one-frame state changes
- incorrect preview texture usage
- missing cleanup for video/player resources
- session persistence omissions
- repeated panel logic that should move into helpers
- `ofApp.cpp` concentration that should move into split files

## Example Prompt Style

Recommended system framing:

> You are an `ofxImGui` and openFrameworks GUI specialist. Favor existing project helpers, preserve OF-style panel architecture, identify owning panel/state before editing, and avoid immediate-mode state bugs such as mutating already-rendered input buffers without deferred updates.

Recommended task framing:

- identify the owning panel/function first
- inspect related helpers and persistence points
- propose the smallest structurally correct edit
- verify UI state flow, preview flow, and session behavior

## Suggested Future API Additions

If this becomes a first-class addon feature, these additions would fit:

### New assistant action

```cpp
enum class ofxGgmlCodeAssistantAction {
    // ...
    UIOfxImGui
};
```

### Optional context flags

```cpp
struct ofxGgmlCodeAssistantContext {
    // ...
    bool preferUiHelpers = false;
    bool includeSessionPersistence = false;
    bool includePreviewResources = false;
    std::string uiFrameworkHint = "ofxImGui";
};
```

### Optional structured result extension

```cpp
struct ofxGgmlCodeAssistantStructuredResult {
    // ...
    std::string owningPanel;
    std::vector<std::string> uiStateFields;
    std::vector<std::string> reusedHelpers;
    std::vector<std::string> persistenceFields;
};
```

## Immediate Next Steps

The best practical implementation order is:

1. Add an `ofxImGui` prompt/profile to `ofxGgmlCodeAssistant`
2. Add UI-aware retrieval ranking for panel/helper/session files
3. Add a small review checklist preset for GUI bugs
4. Expose it in Script mode as a dedicated quick action such as:
   - `UI Review`
   - `Build Panel`
   - `Refactor Panel`
   - `Fix ImGui State`

## Success Criteria

The assistant is successful if it consistently does these better than the generic coding path:

- chooses the right panel file
- preserves OF/openFrameworks style
- uses shared helpers instead of duplicating UI glue
- avoids `InputText` state bugs
- handles image/video preview paths correctly
- keeps `ofApp.cpp` from growing unnecessarily
- produces changes that feel native to this addon instead of imported from generic ImGui samples
