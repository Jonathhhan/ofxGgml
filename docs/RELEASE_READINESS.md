# Release Readiness

This document is the pre-tag checklist for the rewritten `main` line. It defines
what belongs in the first rewrite tag and what must stay optional.

## First Rewrite Tag Scope

The first tag should promise a narrow, dependable addon:

- ggml setup pinned to `v0.11.0`
- `ofxGgmlCore.h` for runtime, tensors, graphs, GGUF metadata, and results
- `ofxGgmlText.h` for backend-neutral text requests and llama.cpp adapters
- `ofxGgmlEmbedding.h` for embedding requests, server adapter, and vector helpers
- `ofxGgmlSegmentation.h` for point-prompt segmentation and optional SAM3 hooks
- focused openFrameworks examples for core, text, chat, and embeddings
- script-tested segmentation/SAM3 boundary
- Windows batch and PowerShell scripts, with POSIX shell wrappers where practical

The first tag should not promise assistants, RAG, speech, TTS, diffusion, broad
vision workflows, model downloads, or product-level GUI workflows.

## Required Checks

Run the fast local validation command before tagging:

```bat
scripts\validate-local.bat
```

That command must pass and must not open UI windows or start long-running
servers. It covers:

- headless C++ addon tests
- generated Visual Studio project repair checks
- launch dry-run smoke checks

When validating optional local runtimes, also run the relevant smoke scripts:

```bat
scripts\test-sam3-smoke.bat
scripts\test-sam3-smoke.bat -ModelPath C:\path\to\sam3-model.gguf
scripts\run-embedding.bat -Prompt "openFrameworks local inference"
```

Example builds are useful release confidence checks when generated project files
exist locally:

```bat
scripts\build-simple-example.bat
scripts\build-text-example.bat
scripts\build-chat-example.bat
scripts\build-embedding-example.bat
```

## Tag Gate

Before creating the first rewrite tag:

- `main` is clean after validation.
- README setup and validation commands match the actual scripts.
- `docs/CORE_CONTRACT.md` has no stale "Next" item that should have been done.
- generated binaries, model files, caches, and project files are not staged.
- optional runtimes fail clearly when not installed.
- any new public type has a focused headless test.
- any new example has a build script and a dry-run or repair smoke path.

## Versioning

The current rewritten API is allowed to break before the first rewrite tag. After
that tag, breaking changes should be called out in release notes and should move
the version intentionally.

`legacy-full` remains the archive branch for the previous broad implementation.
