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

## Current Local Confidence

Last checked on 2026-05-10 on Windows x64 with Visual Studio 18:

- `scripts\validate-local.ps1` passed.
- `scripts\build-simple-example.ps1 -Configuration Release -Platform x64`
  passed after a transient openFrameworks tlog lock retry; 23 warnings, 0
  errors.
- `scripts\build-text-example.ps1 -Configuration Release -Platform x64`
  passed after closing a running `ofxGgmlTextExample.exe`; 0 warnings, 0
  errors.
- `scripts\build-chat-example.ps1 -Configuration Release -Platform x64`
  passed; 23 warnings, 0 errors.
- `scripts\build-embedding-example.ps1 -Configuration Release -Platform x64`
  passed; 23 warnings, 0 errors.
- README commands for setup, validation, llama-server, embedding, and SAM3 smoke
  were checked against the script parameters.
- Generated project files, binaries, build caches, local dependency builds, and
  model artifacts were checked against `.gitignore`; no untracked non-ignored
  files remain.

The remaining warnings are from openFrameworks/ofxImGui headers and generated
Visual Studio project integration, not addon compile errors.

## Tag Gate

Before creating the first rewrite tag:

- `main` is clean after validation.
- README setup and validation commands match the actual scripts.
- `docs/CORE_CONTRACT.md` has no stale "Next" item that should have been done.
- generated binaries, model files, caches, and project files are not staged.
- optional runtimes fail clearly when not installed.
- any new public type has a focused headless test.
- any new example has a build script and a dry-run or repair smoke path.

## Gap Review

These items are intentionally not blockers for the first rewrite tag:

- Real SAM3 model/image UX: deferred until a known-compatible model and sample
  image workflow are available. See `docs/SEGMENTATION.md`.
- Model downloads: deferred. The addon should document paths and environment
  variables, not ship or download large model files by default.
- Full example builds in `validate-local`: deferred. `validate-local` stays
  fast and non-interactive; full openFrameworks example builds remain explicit
  confidence checks.
- All-backend build verification: deferred from the core tag gate. CPU is the
  required baseline; CUDA, Vulkan, Metal, and OpenCL are validated through their
  setup switches on machines that have the relevant SDKs.
- POSIX generated project builds: deferred. Shell wrappers are included, but
  platform projects still depend on local openFrameworks project generation.
- Exact release version string: closed for the first rewrite tag as
  `2.0.0-rewrite.0`.

These items should be closed before the first rewrite tag:

- No open pre-tag hygiene items remain in this document. If new public API or
  scripts are added, rerun this checklist before tagging.

## Versioning

The current rewritten API is allowed to break before the first rewrite tag. After
that tag, breaking changes should be called out in release notes and should move
the version intentionally.

`legacy-full` remains the archive branch for the previous broad implementation.
