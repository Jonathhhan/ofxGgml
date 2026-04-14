# Prompt Size Issue Fix

## Problem Summary

The issue manifested with two symptoms:

1. **Logit bias warnings** during initialization:
   ```
   common_init_result: added  logit bias = -inf
   ```

2. **Prompt too long error**:
   ```
   main: prompt is too long (630088 tokens, max 2044)
   ```

## Root Cause Analysis

### Logit Bias Warnings
These warnings are **informational messages from llama.cpp**, not errors. They occur during model initialization when llama.cpp sets up logit biases to suppress certain tokens (e.g., special tokens, end-of-text markers). The `-inf` value means those tokens are given zero probability. This is normal behavior and does not indicate a bug.

### Prompt Size Overflow
The actual problem was in the "Review All Files" feature in `ofxGgmlGuiExample`:
- When clicking "Review All Files", the application would load up to 20 files
- It concatenated all file contents without checking the total size
- With large files, this created prompts of 630K+ tokens
- The context window was only 2048 tokens (max 2044 for prompt after overhead)
- This caused llama.cpp to reject the prompt as too large

## Solutions Implemented

### 1. Pre-emptive Size Limiting in "Review All Files" (ofApp.cpp:1440-1474)

Added logic to check prompt size before adding each file:
- Estimates maximum safe prompt size as `contextSize * 3 / 2` characters
  - Conservative estimate: ~3 chars per token
  - Reserves 50% of context for response generation
- Tracks cumulative prompt size while loading files
- Stops adding files when the next file would exceed the limit
- Displays informative message when files are excluded

### 2. Validation Before Inference (ofApp.cpp:2676-2699)

Added a safety check in `runInference()` that:
- Estimates token count from character count (chars / 3)
- Compares against the configured context size
- Prevents submission if prompt is too large
- Provides clear user-facing error message with:
  - Estimated token count
  - Maximum allowed tokens
  - Suggestion to reduce input or increase context size

## Benefits

1. **Prevents cryptic errors**: Users now see clear, actionable warnings instead of llama.cpp errors
2. **Protects all inference paths**: Not just "Review All Files", but any oversized prompt
3. **Maintains functionality**: Files are still reviewed, just limited to what fits in context
4. **User guidance**: Error messages guide users to increase context size if needed

## Token Estimation

The fix uses a conservative estimate of **3 characters per token**. This is on the safe side:
- Actual ratios vary by tokenizer and content (typically 3-4 chars/token)
- Using 3 chars/token ensures we stay well within limits
- Better to be conservative and prevent errors than to be aggressive and fail

## Technical Details

**Character-based limiting** was chosen over actual tokenization because:
- The codebase uses llama.cpp CLI tools, not the library directly
- Tokenization would require loading the model just to count tokens
- Character-based estimation is fast and sufficiently accurate for validation
- The conservative ratio provides adequate safety margin
