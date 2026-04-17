#pragma once

#include <string>

struct WorkspaceDiffSnapshot {
	bool success = false;
	bool hasChanges = false;
	std::string workspaceRoot;
	std::string repoRoot;
	std::string statusText;
	std::string diffText;
	std::string error;
};

// Capture workspace Git status and diff snapshot
WorkspaceDiffSnapshot captureWorkspaceDiffSnapshot(const std::string & workspaceRoot);
