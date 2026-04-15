#pragma once

#include "ofMain.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
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

struct ofxGgmlScriptSourceVisualStudioProjectInfo {
	std::string name;
	std::string relativePath;
	std::string projectGuid;
};

struct ofxGgmlScriptSourceWorkspaceInfo {
	std::string workspaceRoot;
	std::string workspaceLabel;
	std::string activeVisualStudioPath;
	std::string visualStudioSolutionPath;
	std::vector<std::string> visualStudioProjectPaths;
	std::vector<ofxGgmlScriptSourceVisualStudioProjectInfo> visualStudioProjects;
	std::string selectedVisualStudioProjectPath;
	bool hasExplicitVisualStudioProjectSelection = false;
	std::vector<std::string> visualStudioConfigurations;
	std::vector<std::string> visualStudioPlatforms;
	std::string selectedVisualStudioConfiguration = "Release";
	std::string selectedVisualStudioPlatform = "x64";
	std::string compilationDatabasePath;
	std::string cmakeListsPath;
	std::string addonsMakePath;
	std::string msbuildPath;
	std::string defaultBuildDirectory;
	std::string gitHubDefaultBranch;
	std::string gitHubResolvedCommitSha;
	std::string gitHubFocusedPath;
	std::string gitHubDiagnostic;
	uint64_t lastScanTimestampMs = 0;
	uint64_t lastObservedChangeTimestampMs = 0;
	uint64_t workspaceGeneration = 0;
	bool hasVisualStudioSolution = false;
	bool hasCompilationDatabase = false;
	bool hasCMakeProject = false;
	bool hasOpenFrameworksProject = false;
	bool gitHubTreeCached = false;
	bool localBackgroundMonitoringEnabled = false;
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
	bool setVisualStudioWorkspace(const std::string & path);
	bool configureVisualStudioWorkspace(
		const std::string & projectPath,
		const std::string & configuration,
		const std::string & platform);
	bool setGitHubRepo(const std::string & ownerRepo, const std::string & branch);
	bool setGitHubRepoFromInput(
		const std::string & ownerRepoOrUrl,
		const std::string & branchHint = "");
	void setGitHubAuthToken(const std::string & token);
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
	ofxGgmlScriptSourceWorkspaceInfo getWorkspaceInfo() const;
	std::string getStatus() const;
	bool isFetching() const noexcept;
	std::vector<ofxGgmlScriptSourceFetchDiagnostic> getFetchDiagnostics() const;

private:
	struct GitHubTreeCacheEntry {
		std::string ownerRepo;
		std::string branch;
		std::string commitSha;
		std::string focusedPath;
		std::string preferredExtension;
		std::vector<ofxGgmlScriptSourceFileEntry> entries;
		uint64_t fetchedAtMs = 0;
	};

	void clearWorkspaceInfoLocked();
	void refreshWorkspaceInfoLocked(
		const std::string & requestedVisualStudioPath = {});
	void startLocalMonitor();
	void stopLocalMonitor();
	bool scanLocalFolderLocked();
	std::vector<ofxGgmlScriptSourceFileEntry> scanLocalFolderEntries(
		const std::string & path) const;
	uint64_t computeLocalWorkspaceFingerprint(
		const std::string & path,
		const std::string & preferredExt) const;
	ofxGgmlScriptSourceWorkspaceInfo buildWorkspaceInfoForFolder(
		const std::string & path,
		const ofxGgmlScriptSourceWorkspaceInfo & previousInfo,
		const std::string & requestedVisualStudioPath) const;
	void cancelFetchWorker(const std::string & reason);
	void pushFetchDiagnosticLocked(const std::string & state,
		const std::string & message,
		uint64_t generation);

	static bool parseGitHubInput(
		const std::string & ownerRepoOrUrl,
		const std::string & branchHint,
		std::string * outOwnerRepo,
		std::string * outBranch,
		std::string * outFocusedPath = nullptr);
	static std::string sanitizeGitHubBranch(const std::string & branchHint);
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
	ofxGgmlScriptSourceWorkspaceInfo m_workspaceInfo;
	std::vector<ofxGgmlScriptSourceFetchDiagnostic> m_fetchDiagnostics;
	std::thread m_fetchThread;
	std::thread m_localMonitorThread;
	std::string m_gitHubAuthToken;
	std::unordered_map<std::string, GitHubTreeCacheEntry> m_gitHubTreeCache;

	std::atomic<bool> m_fetching{false};
	std::atomic<bool> m_cancelFetch{false};
	std::atomic<uint64_t> m_fetchGeneration{0};
	std::atomic<bool> m_stopLocalMonitor{false};
};
