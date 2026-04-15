# Contributing to ofxGgml

Thanks for contributing to `ofxGgml`.

## Getting started

Fork the repository, clone your fork into the openFrameworks `addons` folder, and run setup for your platform:

```bash
cd openFrameworks/addons
git clone https://github.com/<you>/ofxGgml.git
cd ofxGgml
./scripts/setup_linux_macos.sh
```

On Windows, use:

```bat
cd openFrameworks\addons\ofxGgml
scripts\setup_windows.bat
```

Then create a branch for your work:

```bash
git checkout -b feature/my-improvement
```

## Source layout

Main public header:

- `src/ofxGgml.h`

Core runtime files:

- `src/ofxGgmlCore.*`
- `src/ofxGgmlGraph.*`
- `src/ofxGgmlInference.*`
- `src/ofxGgmlModel.*`
- `src/ofxGgmlTensor.*`
- `src/ofxGgmlProjectMemory.*`
- `src/ofxGgmlScriptSource.*`
- `src/ofxGgmlTypes.h`
- `src/ofxGgmlResult.h`
- `src/ofxGgmlHelpers.h`
- `src/ofxGgmlVersion.h`

Other important areas:

- `scripts/`
- `tests/`
- `ofxGgmlBasicExample/`
- `ofxGgmlGuiExample/`
- `ofxGgmlNeuralExample/`

## Code style

- Use tabs for indentation in source files unless a file already follows a different local convention.
- Keep braces on the same line.
- Prefer short, direct comments over long explanatory blocks.
- Keep public headers compact and focused on API shape.
- Use forward declarations in public headers when practical.
- Keep new docs and comments ASCII-only unless a file already uses Unicode intentionally.
- Keep the umbrella header `src/ofxGgml.h` current when adding public API surface.

## Adding graph operations

When adding a new `ofxGgmlGraph` operation:

1. Declare it in `src/ofxGgmlGraph.h` under the matching section.
2. Implement it in `src/ofxGgmlGraph.cpp` with the usual invalid-tensor guard.
3. Keep the wrapper thin and let ggml own the actual operation semantics.
4. Add or update tests when the new operation changes observable behavior.

Typical pattern:

```cpp
ofxGgmlTensor ofxGgmlGraph::myOp(ofxGgmlTensor a) {
	if (!a.raw()) return ofxGgmlTensor();
	return ofxGgmlTensor(ggml_my_op(m_ctx, a.raw()));
}
```

## Build and verification

Before opening a pull request:

- rebuild at least one example project if you touched public headers or Windows linking
- rerun relevant tests if you changed runtime behavior
- regenerate your OF project when addon library lists changed
- confirm Windows script changes still work with `addon_config.mk`

## Documentation

Update these when relevant:

- `README.md` for user-facing setup or API changes
- `CHANGELOG.md` for release-visible changes
- `src/ofxGgmlVersion.h` when bumping the addon version

## Pull requests

Good pull requests tend to:

- keep a focused scope
- explain runtime impact clearly
- mention platform-specific effects, especially Windows build behavior
- include validation notes such as example builds or tests run

## License

By contributing, you agree that your contributions will be licensed under the MIT License.
