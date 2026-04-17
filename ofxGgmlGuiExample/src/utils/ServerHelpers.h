#pragma once

#include "ImHelpers.h"
#include <string>

// ---------------------------------------------------------------------------
// Server URL Utilities
// ---------------------------------------------------------------------------

// Normalize a configured text server URL to a base URL
std::string serverBaseUrlFromConfiguredUrl(const std::string & configuredUrl);

// Normalize a configured speech server URL to a base URL
std::string speechServerBaseUrlFromConfiguredUrl(const std::string & configuredUrl);
