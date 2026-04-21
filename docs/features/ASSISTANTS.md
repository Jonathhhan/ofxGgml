# Assistants Guide

Learn how to use ofxGgml's AI assistants for chat, code, text processing, and workspace management.

## Overview

Include `ofxGgmlAssistants.h` to get:
- All basic text inference features
- Chat assistant (multi-turn conversations)
- Code assistant (semantic search, completions)
- Workspace assistant (patch validation, unified diffs)
- Coding agent (orchestration with approvals)
- Code review (hierarchical analysis)
- Text assistant (translation, summarization)

## Chat Assistant

### Multi-Turn Conversations

```cpp
#include "ofxGgmlAssistants.h"

ofxGgmlChatAssistant chatAssistant;
chatAssistant.setInference(&inference);

// Turn 1
auto response1 = chatAssistant.chat(
    "What's the capital of France?",
    "English",
    "You are a helpful geography tutor."  // System prompt
);

cout << response1.text << endl;

// Turn 2 - remembers context
auto response2 = chatAssistant.chat(
    "What's its population?",
    "English"
    // System prompt optional after first turn
);

// Chat assistant maintains conversation history
```

### Clear History

```cpp
chatAssistant.clearHistory();  // Start fresh conversation
```

### Custom System Prompts

```cpp
chatAssistant.setSystemPrompt(
    "You are a friendly coding mentor specializing in C++ and openFrameworks."
);

auto response = chatAssistant.chat(
    "How do I draw a circle in OF?",
    "English"
);
```

## Text Assistant

### Summarization

```cpp
ofxGgmlTextAssistant textAssistant;
textAssistant.setInference(&inference);

ofxGgmlInferenceSettings settings;
settings.maxTokens = 256;

// Summarize
auto summary = textAssistant.summarize(longArticle, settings);

// Key points
auto keyPoints = textAssistant.extractKeyPoints(document, settings);

// TL;DR
auto tldr = textAssistant.tldr(report, settings);
```

### Translation

```cpp
auto translation = textAssistant.translate(
    "Bonjour le monde",
    "English",  // target language
    "French",   // source language (or "Auto detect")
    settings
);

// Detect language
auto detected = textAssistant.detectLanguage(unknownText, settings);
cout << "Detected: " << detected.detectedLanguage << endl;
```

### Rewriting

```cpp
// Rewrite for clarity
auto rewritten = textAssistant.rewrite(
    roughDraft,
    "Make this more professional and concise",
    settings
);

// Expand
auto expanded = textAssistant.expand(
    bulletPoints,
    "Turn these notes into full paragraphs",
    settings
);
```

## Code Assistant

### Semantic Code Search

```cpp
ofxGgmlCodeAssistant codeAssistant;
codeAssistant.setInference(&inference);

// Set workspace
ofxGgmlScriptSource workspace;
workspace.setLocalFolder("/path/to/project");
codeAssistant.setWorkspace(&workspace);

// Search for function definitions
auto symbols = codeAssistant.findSymbols(
    "runInference",
    workspace,
    5  // max results
);

for (auto& symbol : symbols) {
    cout << symbol.filePath << ":" << symbol.line << endl;
    cout << symbol.context << endl;
}
```

### Inline Completion

```cpp
string beforeCursor = "void setup() {\n    ofSetWindowTitle(";
string afterCursor = ");\n}";

auto completion = codeAssistant.prepareInlineCompletion(
    beforeCursor,
    afterCursor,
    "ofApp.cpp",
    workspace
);

// completion.text contains suggested code
```

### Generate Code

```cpp
auto result = codeAssistant.generateCode(
    "Add error handling to the parseConfig function",
    workspace,
    settings
);

if (result.success) {
    cout << "Plan:\n" << result.plan << endl;
    cout << "\nCode:\n" << result.code << endl;
}
```

### Code Review

```cpp
ofxGgmlCodeReview reviewer;
reviewer.setInference(&inference);

auto review = reviewer.reviewRepository(
    workspace,
    "Focus on error handling and resource management"
);

cout << "Architecture:\n" << review.architectureSummary << endl;
cout << "\nFindings:\n";
for (auto& finding : review.findings) {
    cout << "- " << finding.file << ": " << finding.issue << endl;
}
```

## Workspace Assistant

### Validate Patches

```cpp
ofxGgmlWorkspaceAssistant wsAssistant;
wsAssistant.setCodeAssistant(&codeAssistant);

// Structured patch from code assistant
auto validation = wsAssistant.validatePatch(
    result.patch,
    "/project/root"
);

if (!validation.safe) {
    cout << "Unsafe operations:\n";
    for (auto& issue : validation.issues) {
        cout << "- " << issue << endl;
    }
}
```

### Apply Unified Diff

```cpp
string diff = R"(
--- src/main.cpp
+++ src/main.cpp
@@ -10,7 +10,8 @@
 void setup() {
-    loadConfig("config.json");
+    if (!loadConfig("config.json")) {
+        ofLogError() << "Failed to load config";
+    }
 }
)";

auto result = wsAssistant.applyUnifiedDiff(
    diff,
    "/project/root"
);

if (result.success) {
    cout << "Applied to " << result.touchedFiles.size() << " files\n";
}
```

### Transaction Rollback

```cpp
auto transaction = wsAssistant.beginTransaction();

// Apply changes
wsAssistant.applyPatch(patch, workspace);

// Something went wrong, rollback
wsAssistant.rollbackTransaction(transaction);
```

### Verify Changes

```cpp
auto verification = wsAssistant.verify(
    touchedFiles,
    workspace
);

for (auto& cmd : verification.commands) {
    cout << "Running: " << cmd.command << endl;
    if (!cmd.success) {
        cout << "Failed: " << cmd.output << endl;
    }
}
```

## Coding Agent

### Plan Mode (Read-Only)

```cpp
ofxGgmlCodingAgent agent;
agent.setCodeAssistant(&codeAssistant);
agent.setWorkspaceAssistant(&wsAssistant);

ofxGgmlCodingAgentRequest request;
request.mode = ofxGgmlCodingAgentMode::Plan;
request.task = "Add input validation to user registration";
request.workspace = &workspace;

auto result = agent.run(request);

cout << "Plan:\n" << result.plan << endl;
// No code changes made in Plan mode
```

### Build Mode (With Approval)

```cpp
request.mode = ofxGgmlCodingAgentMode::Build;
request.autoApprove = false;  // Require approval for risky operations

// Set approval callback
agent.setApprovalCallback([](const string& operation, const string& details) {
    cout << "Approve " << operation << "? (y/n): ";
    cout << details << endl;

    string response;
    cin >> response;
    return (response == "y" || response == "yes");
});

// Set event callback for progress
agent.setEventCallback([](const ofxGgmlCodeAssistantEvent& event) {
    if (event.type == ofxGgmlCodeAssistantEventType::ToolProposed) {
        cout << "Tool: " << event.toolName << endl;
    }
});

auto result = agent.run(request);

if (result.success) {
    cout << "Changes applied to " << result.touchedFiles.size() << " files\n";

    if (result.verificationPassed) {
        cout << "Verification passed!\n";
    }
}
```

### Retry on Failure

```cpp
// Agent maintains session state
if (!result.success) {
    // Retry with failure context
    request.task = "Fix the validation error: " + result.error;
    auto retry = agent.run(request);
}
```

## Code Map and Planning

### Build Semantic Code Map

```cpp
auto codeMap = codeAssistant.buildCodeMap(
    workspace,
    "src/"  // focus on src directory
);

cout << "Key modules:\n";
for (auto& module : codeMap.modules) {
    cout << "- " << module.name << ": " << module.purpose << endl;
}

cout << "\nDependencies:\n";
for (auto& dep : codeMap.dependencies) {
    cout << dep.from << " → " << dep.to << endl;
}
```

### Spec to Implementation Plan

```cpp
string spec = R"(
Feature: User authentication with JWT tokens
- Login endpoint accepts username/password
- Returns JWT token on success
- Protected endpoints verify token
- Token expires after 24 hours
)";

auto plan = codeAssistant.runSpecToCode(
    spec,
    workspace,
    settings
);

cout << "Implementation plan:\n";
for (size_t i = 0; i < plan.steps.size(); i++) {
    cout << (i+1) << ". " << plan.steps[i].description << endl;
    cout << "   Files: " << plan.steps[i].files << endl;
}

cout << "\nTests needed:\n";
for (auto& test : plan.tests) {
    cout << "- " << test << endl;
}
```

## Project Memory

### Store Coding Context

```cpp
ofxGgmlProjectMemory memory;
memory.loadFromFile("project-memory.json");

// Store facts about the codebase
memory.addMemory(
    "config",
    "Configuration is loaded from config.json in the data folder",
    "src/config.cpp:45"
);

memory.addMemory(
    "error-handling",
    "Use ofLogError for user-facing errors, ofLogWarning for internal issues",
    "coding-guidelines.md"
);

memory.saveToFile("project-memory.json");
```

### Query Memory

```cpp
auto memories = memory.search("error handling");

for (auto& m : memories) {
    cout << m.topic << ": " << m.content << endl;
    cout << "Source: " << m.source << endl;
}
```

### Use in Code Assistant

```cpp
codeAssistant.setProjectMemory(&memory);

// Code assistant now includes project memory in context
auto code = codeAssistant.generateCode(
    "Add error handling to loadTexture",
    workspace,
    settings
);
// Will follow project's error handling conventions
```

## Performance Tips

### Chat Assistant
- Clear history periodically to reduce context size
- Use shorter system prompts when possible
- Batch related questions in single turns

### Code Assistant
- Index workspace once, reuse for multiple queries
- Use compile database (compile_commands.json) for faster symbol lookup
- Limit symbol search scope to relevant directories

### Workspace Assistant
- Shadow workspace for safe testing before real apply
- Batch verify commands when possible
- Use incremental diffs vs full file rewrites

### Coding Agent
- Start with Plan mode to review approach
- Use focused tasks vs broad "improve everything"
- Enable auto-approve only for trusted operations

## Integration with IDE

### VS Code Extension Pattern

```cpp
// Language server style integration
codeAssistant.setWorkspace(projectRoot);

// On hover
auto hover = codeAssistant.getHoverInfo(file, line, column);

// On completion request
auto completions = codeAssistant.getCompletions(file, position);

// On code action request
auto actions = codeAssistant.getCodeActions(file, range);
```

## Next Steps

- **Workflows**: [WORKFLOWS.md](WORKFLOWS.md)
- **Performance**: [../PERFORMANCE.md](../PERFORMANCE.md)
- **GUI Example**: `ofxGgmlGuiExample` - Script mode demonstrates coding agent

## Examples

- `ofxGgmlGuiExample` - Script mode with coding agent
- Check `tests/` for assistant test cases
