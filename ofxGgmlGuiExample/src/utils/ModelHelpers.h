#pragma once

#include <string>

// ---------------------------------------------------------------------------
// Model Utilities
// ---------------------------------------------------------------------------

// Detect the number of layers in a GGUF model file by reading metadata
int detectGgufLayerCountMetadata(const std::string & modelPath);
