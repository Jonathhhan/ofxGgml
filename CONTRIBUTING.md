# Contributing to ofxGgml

Thank you for your interest in contributing!

## Getting Started

1. Fork the repository on GitHub.
2. Clone your fork and set up the addon:
   ```bash
   cd openFrameworks/addons
   git clone https://github.com/<you>/ofxGgml.git
   cd ofxGgml
   ./scripts/setup_linux.sh
   ```
3. Create a feature branch:
   ```bash
   git checkout -b feature/my-improvement
   ```

## Addon Structure

```
src/
├── ofxGgml.h            # umbrella header (include this)
├── ofxGgmlCore.h/.cpp   # backend management, compute
├── ofxGgmlGraph.h/.cpp  # computation graph builder
├── ofxGgmlInference.h/.cpp # llama.cpp CLI generation/embedding helper
├── ofxGgmlModel.h/.cpp  # GGUF model loading
├── ofxGgmlTensor.h/.cpp # tensor wrapper
├── ofxGgmlTypes.h       # enums, settings, result structs
├── ofxGgmlHelpers.h     # utility functions
└── ofxGgmlVersion.h     # version macros
```

All source files live in a flat `src/` directory — this is the standard oF addon layout.

## Adding New Operations

To add a new tensor operation to `ofxGgmlGraph`:

1. Declare the method in `ofxGgmlGraph.h` under the appropriate section.
2. Implement it in `ofxGgmlGraph.cpp` following the pattern:
   ```cpp
   ofxGgmlTensor ofxGgmlGraph::myOp(ofxGgmlTensor a) {
       if (!a.raw()) return ofxGgmlTensor();
       return ofxGgmlTensor(ggml_my_op(m_ctx, a.raw()));
   }
   ```
3. Add a brief doxygen comment (`///`) before the declaration.

## Code Style

- Follow existing code conventions (tabs for indentation, braces on same line).
- Use `ofxGgml` prefix for all public class names.
- Keep ggml headers out of public `.h` files — use forward declarations.
- Keep the umbrella header (`ofxGgml.h`) up to date when adding new files.

## Examples

- Name example directories `example-<name>/` (e.g., `example-training/`).
- Each example needs `addons.make`, `Makefile`, `config.make`, and `src/` with `main.cpp` and `ofApp.h`/`ofApp.cpp`.

## Submitting Changes

1. Ensure your changes don't break existing examples.
2. Update `README.md` if adding new features or changing the API.
3. Bump the version in `ofxGgmlVersion.h` for significant changes.
4. Open a pull request with a clear description of your changes.

## License

By contributing, you agree that your contributions will be licensed under the MIT License.
