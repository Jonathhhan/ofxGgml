#pragma once

#include "ofMain.h"

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

enum class ofxGgmlScriptSourceType {
	None = 0,
	LocalFolder = 1,
	GitHubRepo = 2
};

struct ofxGgmlScriptSourceFileEntry {
	std::string name;
	std::string fullPath;
	bool isDirectory = false;
};

class ofxGgmlScriptSource {
public:
	void clear();

	void setPreferredExtension(const std::string & ext);
	std::string getPreferredExtension() const;

	bool setLocalFolder(const std::string & path);
	bool setGitHubRepo(const std::string & ownerRepo, const std::string & branch);
	bool fetchGitHubRepo();
	bool rescan();

	bool loadFileContent(int index, std::string & outContent);
	bool saveToLocalSource(const std::string & filename, const std::string & content);

	ofxGgmlScriptSourceType getSourceType() const;
	std::string getLocalFolderPath() const;
	std::string getGitHubOwnerRepo() const;
	std::string getGitHubBranch() const;
	std::vector<ofxGgmlScriptSourceFileEntry> getFiles() const;
	std::string getStatus() const;
	bool isFetching() const;

private:
	bool scanLocalFolderLocked();
	std::vector<ofxGgmlScriptSourceFileEntry> scanLocalFolderEntries(
		const std::string & path) const;

	static bool isValidOwnerRepo(const std::string & ownerRepo);
	static bool isValidBranch(const std::string & branch);
	static bool isSafeRepoPath(const std::string & path);
	static std::string trim(const std::string & s);
	static bool hasSourceExtension(
		const std::string & ext, bool includeTextLikeExtensions);

	mutable std::mutex m_mutex;
	ofxGgmlScriptSourceType m_sourceType = ofxGgmlScriptSourceType::None;
	std::string m_localFolderPath;
	std::string m_gitHubOwnerRepo;
	std::string m_gitHubBranch;
	std::string m_preferredExtension;
	std::vector<ofxGgmlScriptSourceFileEntry> m_files;
	std::string m_status;

	std::atomic<bool> m_fetching{false};
	std::atomic<uint64_t> m_fetchGeneration{0};
};

