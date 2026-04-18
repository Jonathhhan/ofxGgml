#pragma once

#include "ServerHelpers.h"

#include <string>
#include <utility>

enum class AiMode;
enum class TextInferenceBackend;

// Parse server host and port from configured URL
std::pair<std::string, int> parseServerHostPort(const std::string & configuredUrl);

// Parse speech server host and port from configured URL
std::pair<std::string, int> parseSpeechServerHostPort(const std::string & configuredUrl);

// Check if we should manage local text server
bool shouldManageLocalTextServer(const std::string & configuredUrl);

// Check if we should manage local speech server
bool shouldManageLocalSpeechServer(const std::string & configuredUrl);

// Check if AI mode supports text backend
bool aiModeSupportsTextBackend(AiMode mode);

// Clamp text inference backend value
TextInferenceBackend clampTextInferenceBackend(int value);

// Describe process exit code
std::string describeExitCode(int code);
