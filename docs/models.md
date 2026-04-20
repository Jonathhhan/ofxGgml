# Supported Models

This document lists model families that are known to fit the current `ofxGgml` backend integrations, along with the file formats those backends actually expect.

Use this as the first stop before downloading a model into the addon.

## Backend rule

`ofxGgml` presents a common API, but backend compatibility is still backend-specific.

Do not assume that:

- every GGUF works with every text backend
- every TTS backend loads raw GGUF
- every speech or vision model can be swapped across runtimes

Always check the backend section below first.

## Piper TTS

Status: first-class backend option in this addon

### What works conceptually

The Piper path is for direct Piper voice models, not converted `chatllm.cpp` artifacts.

### What file format the addon should load

Load the Piper voice model itself:

- `.onnx`

Keep the matching config file beside it:

- `.onnx.json`

If the config sidecar is missing, the addon now rejects the model before launch.

### Current adapter scope

The current Piper adapter is wired for plain text-to-speech synthesis.

It does not currently wire:

- voice cloning
- continuation from prompt audio

### Executable expectation

Point the TTS Executable field at one of these:

- `piper` or `piper.exe`
- `python` or `py` with the Piper module installed

Leaving Executable blank makes the addon try the default Piper command name on `PATH`.

For the addon-local Windows path, use:

- `scripts/install-piper.ps1`

That helper installs Piper into a local virtual environment, writes `libs/piper/bin/piper.bat`, and can also download a recommended starter voice into `models/piper`.

## chatllm.cpp TTS

Status: verified integration behavior in this addon

### What works conceptually

The `chatllm.cpp` TTS path is wired for converted TTS models, not for raw Hugging Face exports.

For OuteTTS, upstream `chatllm.cpp` documents support for:

- `OuteAI/Llama-OuteTTS-1.0-1B`
- `OuteAI/OuteTTS-1.0-0.6B`

It also expects the DAC codec model:

- `ibm-research/DAC.speech.v1.0`

### What file format the addon should load

Load the converted `chatllm.cpp` model artifact produced by `convert.py`, such as:

- `.bin`
- `.ggmm`

Do not load these directly into the `chatllm.cpp` TTS path:

- raw `.gguf`
- raw `.safetensors`

This includes valid OuteTTS GGUF files. They are real model files, but this backend path does not load them directly.

### Conversion workflow

Use `chatllm.cpp`'s `convert.py` against the Hugging Face source model and DAC assets.

For OuteTTS, upstream documents the additional conversion options:

```bash
python convert.py -i path/to/model -o outetts.bin --name OuteTTS -a OuteTTS --dac_model /path/to/dac
```

The resulting converted artifact is what should be selected in the addon TTS panel.

### Voice cloning

Clone voice currently expects:

- a converted `chatllm.cpp` OuteTTS model artifact
- a prepared `speaker.json`

The addon does not currently generate `speaker.json` from reference audio on its own.

### Known non-working path

This was directly verified in this workspace:

- `OuteTTS-0.2-500M-Q8_0.gguf` is a valid GGUF file
- current `chatllm.cpp` in this addon still rejects it in the TTS load path

So raw OuteTTS GGUF should be treated as unsupported for this specific addon backend path.

## How to extend this file

When adding a backend section, include:

- backend name
- supported source model families
- required conversion step, if any
- actual runtime file format to load
- required sidecar files
- known unsupported shortcuts that users are likely to try

That keeps the repo honest and prevents the UI from drifting into generic advice that does not match the runtime.
