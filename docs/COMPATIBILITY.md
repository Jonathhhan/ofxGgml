# Compatibility And Versioning

This document describes how `ofxGgml` should coexist with optional companion addons such as `ofxStableDiffusion`, and how to manage upstream `ggml` / `stable-diffusion.cpp` updates safely.

## Short version

- `ofxGgml` is the primary `ggml`-centric addon in this workspace.
- `ofxStableDiffusion` remains a separate addon with its own `stable-diffusion.cpp` integration.
- We prefer **pinned, tested revisions** over tracking `main` for either upstream.
- We do **not** assume that one shared `ggml` source tree is automatically safer than two pinned integrations.

## Recommended policy

Use a compatibility matrix instead of a rolling "latest everywhere" strategy.

For each addon release:

- pin the `ggml` revision used by `ofxGgml`
- pin the `stable-diffusion.cpp` revision used by `ofxStableDiffusion`
- record whether that pair was tested together
- only upgrade one side when the pair is revalidated

This reduces three common failure modes:

- API drift between `ggml` and `stable-diffusion.cpp`
- runtime conflicts from mixed `ggml` DLLs or stale copied binaries
- debugging ambiguity when one addon is on a much newer upstream than the other

## Why not force one shared ggml right now

Sharing one physical `ggml` checkout or build across both addons sounds attractive, but it creates a tighter coupling contract:

- `stable-diffusion.cpp` can lag upstream `ggml`
- `ofxStableDiffusion` exposes a higher-level API over `stable-diffusion.cpp`, not raw `ggml`
- a shared build means every `ggml` update must be validated against both addon integrations at once

That can be worth doing later, but only when the exact upstream revisions are known to be compatible and the maintenance burden is acceptable.

For now, the safer default is:

- shared policy
- separate vendored integrations
- strict runtime packaging

## Runtime packaging rules

When an app or example uses both addons:

- do not manually copy old `ggml*.dll` files into `bin`
- let build or setup scripts own runtime copying
- only ship one consistent set of runtime libraries per backend path
- avoid broad runtime auto-loading from arbitrary `bin` contents
- keep addon-local helper runtimes in addon-local locations where possible

On Windows in particular, stale copied DLLs are a common source of subtle breakage.

## Tested matrix

Fill this table in when updating either upstream:

| ofxGgml release | ofxGgml ggml revision | ofxStableDiffusion release | stable-diffusion.cpp revision | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| 1.0.2+ | `record exact commit/tag` | `record exact addon tag/commit` | `record exact commit/tag` | Pending | Update this row when validating a new pair |

Recommended status values:

- `Tested`
- `Experimental`
- `Known broken`

## Upgrade workflow

When updating `ggml` or `stable-diffusion.cpp`:

1. Pin the candidate upstream revision in the relevant addon.
2. Rebuild the example app and any local helper runtimes.
3. Verify text, speech, vision, and diffusion flows together if both addons are enabled.
4. Check runtime packaging for stale copied DLLs or duplicate backend libraries.
5. Record the validated revision pair in the matrix above.

## Rule of thumb

If you want maximum stability:

- do not chase the latest upstream on both sides at the same time
- move in small pinned steps
- treat "works together" as a release artifact, not an assumption
