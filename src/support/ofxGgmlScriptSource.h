#pragma once

#include "ofMain.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

enum class ofxGgmlScriptSourceType {
	None = 0,
	LocalFolder = 1,
	GitHubRepo = 2,
	Internet = 3
};

struct ofxGgmlScriptSourceFileEntry {
	std::string name;
	std::string fullPath;
	std::string cachedContent;
	bool isDirectory = false;
	bool isCached = false;
};

struct ofxGgmlScriptSourceFetchDiagnostic {
	std::string state;
	std::string message;
	uint64_t generation = 0;
	uint64_t timestampMs = 0;
};

class ofxGgmlScriptSource {
public:
	~ofxGgmlScriptSource();

	void clear();
	void setGitHubMode();
	void setInternetMode();

	void setPreferredExtension(const std::string & ext);
	std::string getPreferredExtension() const;

	bool setLocalFolder(const std::string & path);
	bool setGitHubRepo(const std::string & ownerRepo, const std::string & branch);
	void setInternetUrls(const std::vector<std::string> & urls);
	bool addInternetUrl(const std::string & url);
	bool removeInternetUrl(size_t index);
	bool fetchGitHubRepo();
	bool rescan();

	bool loadFileContent(int index, std::string & outContent);
	bool saveToLocalSource(const std::string & filename, const std::string & content);

	ofxGgmlScriptSourceType getSourceType() const noexcept;
	std::string getLocalFolderPath() const noexcept;
	std::string getGitHubOwnerRepo() const noexcept;
	std::string getGitHubBranch() const noexcept;
	std::vector<std::string> getInternetUrls() const;
	std::vector<ofxGgmlScriptSourceFileEntry> getFiles() const;
	std::string getStatus() const;
	bool isFetching() const noexcept;
	std::vector<ofxGgmlScriptSourceFetchDiagnostic> getFetchDiagnostics() const;

private:
	bool scanLocalFolderLocked();
	std::vector<ofxGgmlScriptSourceFileEntry> scanLocalFolderEntries(
		const std::string & path) const;
	void cancelFetchWorker(const std::string & reason);
	void pushFetchDiagnosticLocked(const std::string & state,
		const std::string & message,
		uint64_t generation);

	static bool isValidOwnerRepo(const std::string & ownerRepo);
	static bool isValidBranch(const std::string & branch);
	static bool isSafeRepoPath(const std::string & path);
	static std::string trim(const std::string & s);
	static bool isValidUrl(const std::string & url);
	static bool hasSourceExtension(
		const std::string & ext, bool includeTextLikeExtensions);

	mutable std::mutex m_mutex;
	ofxGgmlScriptSourceType m_sourceType = ofxGgmlScriptSourceType::None;
	std::string m_localFolderPath;
	std::string m_gitHubOwnerRepo;
	std::string m_gitHubBranch;
	std::string m_preferredExtension;
	std::vector<std::string> m_internetUrls;
	std::vector<ofxGgmlScriptSourceFileEntry> m_files;
	std::string m_status;
	std::vector<ofxGgmlScriptSourceFetchDiagnostic> m_fetchDiagnostics;
	std::thread m_fetchThread;

	std::atomic<bool> m_fetching{false};
	std::atomic<bool> m_cancelFetch{false};
	std::atomic<uint64_t> m_fetchGeneration{0};
};
