#include "ofxGgmlScriptSource.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace {

constexpr size_t kMaxCachedContentBytes = 16 * 1024 * 1024;
constexpr size_t kMaxCachedEntryBytes = 2 * 1024 * 1024;

static size_t totalCachedBytes(const std::vector<ofxGgmlScriptSourceFileEntry> & files) {
	size_t total = 0;
	for (const auto & f : files) {
		if (!f.isCached) continue;
		total += f.cachedContent.size();
	}
	return total;
}

std::string normalizeLower(const std::string & s) {
	std::string out = s;
	std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return out;
}

}

ofxGgmlScriptSource::~ofxGgmlScriptSource() {
	cancelFetchWorker("Fetch canceled: source destroyed");
}

void ofxGgmlScriptSource::clear() {
	cancelFetchWorker("Fetch canceled: source cleared");
	std::lock_guard<std::mutex> lock(m_mutex);
	m_sourceType = ofxGgmlScriptSourceType::None;
	m_localFolderPath.clear();
	m_gitHubOwnerRepo.clear();
	m_gitHubBranch.clear();
	m_internetUrls.clear();
	m_files.clear();
	m_status.clear();
	m_fetchDiagnostics.clear();
}

void ofxGgmlScriptSource::setGitHubMode() {
	cancelFetchWorker("Fetch canceled: switched to GitHub mode");
	std::lock_guard<std::mutex> lock(m_mutex);
	m_sourceType = ofxGgmlScriptSourceType::GitHubRepo;
	m_localFolderPath.clear();
	m_internetUrls.clear();
	m_files.clear();
	m_status.clear();
}

void ofxGgmlScriptSource::setInternetMode() {
	cancelFetchWorker("Fetch canceled: switched to Internet mode");
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_sourceType != ofxGgmlScriptSourceType::Internet) {
		m_internetUrls.clear();
		m_files.clear();
		m_status.clear();
	}
	m_sourceType = ofxGgmlScriptSourceType::Internet;
	m_localFolderPath.clear();
	m_gitHubOwnerRepo.clear();
	m_gitHubBranch.clear();
}

void ofxGgmlScriptSource::setPreferredExtension(const std::string & ext) {
	std::string normalized = trim(ext);
	if (!normalized.empty() && normalized.front() != '.') {
		normalized = "." + normalized;
	}
	normalized = normalizeLower(normalized);
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_preferredExtension = normalized;
	}
	rescan();
}

std::string ofxGgmlScriptSource::getPreferredExtension() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_preferredExtension;
}

bool ofxGgmlScriptSource::setLocalFolder(const std::string & path) {
	cancelFetchWorker("Fetch canceled: switched to local folder");
	std::lock_guard<std::mutex> lock(m_mutex);
	m_sourceType = ofxGgmlScriptSourceType::LocalFolder;
	m_localFolderPath = path;
	m_gitHubOwnerRepo.clear();
	m_gitHubBranch.clear();
	m_internetUrls.clear();
	return scanLocalFolderLocked();
}

bool ofxGgmlScriptSource::setGitHubRepo(const std::string & ownerRepo, const std::string & branch) {
	const std::string ownerRepoTrim = trim(ownerRepo);
	const std::string branchTrim = trim(branch);
	if (!isValidOwnerRepo(ownerRepoTrim)) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_sourceType = ofxGgmlScriptSourceType::GitHubRepo;
		m_localFolderPath.clear();
		m_gitHubOwnerRepo = ownerRepoTrim;
		m_gitHubBranch = branchTrim;
		m_files.clear();
		m_status = "Invalid repo format (use owner/repo)";
		return false;
	}
	if (!isValidBranch(branchTrim)) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_sourceType = ofxGgmlScriptSourceType::GitHubRepo;
		m_localFolderPath.clear();
		m_gitHubOwnerRepo = ownerRepoTrim;
		m_gitHubBranch = branchTrim;
		m_files.clear();
		m_status = "Invalid branch name";
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_sourceType = ofxGgmlScriptSourceType::GitHubRepo;
		m_localFolderPath.clear();
		m_gitHubOwnerRepo = ownerRepoTrim;
		m_gitHubBranch = branchTrim;
		m_internetUrls.clear();
		m_files.clear();
		m_status.clear();
	}

	return true;
}

void ofxGgmlScriptSource::setInternetUrls(const std::vector<std::string> & urls) {
	cancelFetchWorker("Fetch canceled: internet sources updated");
	std::lock_guard<std::mutex> lock(m_mutex);
	m_sourceType = ofxGgmlScriptSourceType::Internet;
	m_localFolderPath.clear();
	m_gitHubOwnerRepo.clear();
	m_gitHubBranch.clear();
	m_internetUrls.clear();
	m_internetUrls.reserve(urls.size()); // Pre-allocate capacity
	for (const auto & url : urls) {
		std::string trimmed = trim(url);
		if (isValidUrl(trimmed)) {
			m_internetUrls.push_back(std::move(trimmed)); // Use move semantics
		}
	}

	m_files.clear();
	m_files.reserve(m_internetUrls.size()); // Pre-allocate capacity
	for (const auto & url : m_internetUrls) {
		ofxGgmlScriptSourceFileEntry fe;
		fe.name = url.size() > 96 ? url.substr(0, 96) + "..." : url;
		fe.fullPath = url;
		fe.isDirectory = false;
		m_files.push_back(std::move(fe));
	}
	m_status = m_internetUrls.empty()
		? "No internet sources"
		: ofToString(m_internetUrls.size()) + " internet sources";
}

bool ofxGgmlScriptSource::addInternetUrl(const std::string & url) {
	std::string trimmed = trim(url);
	if (!isValidUrl(trimmed)) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_sourceType = ofxGgmlScriptSourceType::Internet;
		m_status = "Invalid URL (use http/https)";
		return false;
	}

	std::lock_guard<std::mutex> lock(m_mutex);
	m_sourceType = ofxGgmlScriptSourceType::Internet;
	m_localFolderPath.clear();
	m_gitHubOwnerRepo.clear();
	m_gitHubBranch.clear();

	auto it = std::find(m_internetUrls.begin(), m_internetUrls.end(), trimmed);
	if (it == m_internetUrls.end()) {
		m_internetUrls.push_back(std::move(trimmed)); // Use move semantics
	}

	// Rebuild file list to reflect latest URLs.
	m_files.clear();
	m_files.reserve(m_internetUrls.size()); // Pre-allocate capacity
	for (const auto & entryUrl : m_internetUrls) {
		ofxGgmlScriptSourceFileEntry fe;
		fe.name = entryUrl.size() > 96 ? entryUrl.substr(0, 96) + "..." : entryUrl;
		fe.fullPath = entryUrl;
		fe.isDirectory = false;
		m_files.push_back(std::move(fe));
	}

	m_status = m_internetUrls.empty()
		? "No internet sources"
		: ofToString(m_internetUrls.size()) + " internet sources";
	return true;
}

bool ofxGgmlScriptSource::removeInternetUrl(size_t index) {
	std::lock_guard<std::mutex> lock(m_mutex);
	if (index >= m_internetUrls.size()) return false;
	m_internetUrls.erase(m_internetUrls.begin() + static_cast<long>(index));

	m_files.clear();
	for (const auto & entryUrl : m_internetUrls) {
		ofxGgmlScriptSourceFileEntry fe;
		fe.name = entryUrl.size() > 96 ? entryUrl.substr(0, 96) + "..." : entryUrl;
		fe.fullPath = entryUrl;
		fe.isDirectory = false;
		m_files.push_back(std::move(fe));
	}

	m_status = m_internetUrls.empty()
		? "No internet sources"
		: ofToString(m_internetUrls.size()) + " internet sources";
	return true;
}

bool ofxGgmlScriptSource::fetchGitHubRepo() {
	cancelFetchWorker("Fetch canceled: superseded by new fetch");

	std::string ownerRepo;
	std::string branch;
	std::string preferredExt;
	ofxGgmlScriptSourceType sourceType;
	uint64_t generation = 0;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		sourceType = m_sourceType;
		if (sourceType != ofxGgmlScriptSourceType::GitHubRepo) {
			m_status = "Source is not GitHub";
			return false;
		}
		ownerRepo = m_gitHubOwnerRepo;
		branch = m_gitHubBranch;
		preferredExt = m_preferredExtension;
		if (!isValidOwnerRepo(ownerRepo)) {
			m_status = "Invalid repo format (use owner/repo)";
			return false;
		}
		if (!isValidBranch(branch)) {
			m_status = "Invalid branch name";
			return false;
		}
		m_files.clear();
		m_status = "Fetching...";
		m_cancelFetch.store(false, std::memory_order_release);
		generation = m_fetchGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
		pushFetchDiagnosticLocked("start", "GitHub fetch started", generation);
	}
	m_fetching.store(true, std::memory_order_release);

	const std::string apiUrl = "https://api.github.com/repos/" + ownerRepo +
		"/git/trees/" + branch + "?recursive=1";
	const std::string rawPrefix = "https://raw.githubusercontent.com/" +
		ownerRepo + "/" + branch + "/";

	std::thread worker([this, generation, apiUrl, rawPrefix, preferredExt, sourceType]() {
		std::vector<ofxGgmlScriptSourceFileEntry> entries;
		std::string status;
		bool hadError = false;

		ofHttpResponse response = ofLoadURL(apiUrl);
		if (m_cancelFetch.load(std::memory_order_acquire) || m_fetchGeneration.load(std::memory_order_acquire) != generation) {
			m_fetching.store(false, std::memory_order_release);
			return;
		}
		if (response.status < 200 || response.status >= 300) {
			if (response.status == 404) {
				status = "Repo not found";
			} else {
				status = "GitHub API error: " + ofToString(response.status);
			}
			hadError = true;
		} else {
			const std::string body = response.data.getText();
			ofJson json = ofJson::parse(body, nullptr, false);
			if (json.is_discarded() || !json.contains("tree") || !json["tree"].is_array()) {
				status = "Failed to parse GitHub response";
				hadError = true;
			} else {
				for (const auto & item : json["tree"]) {
					if (m_cancelFetch.load(std::memory_order_acquire) || m_fetchGeneration.load(std::memory_order_acquire) != generation) {
						m_fetching.store(false, std::memory_order_release);
						return;
					}
					if (!item.is_object()) continue;
					const std::string type = item.value("type", "");
					if (type != "blob") continue;

					const std::string path = item.value("path", "");
					if (!isSafeRepoPath(path)) continue;

					const std::filesystem::path p(path);
					const std::string ext = normalizeLower(p.extension().string());
					bool include = hasSourceExtension(ext, true);
					if (!preferredExt.empty()) {
						include = (ext == preferredExt);
					}
					if (!include) continue;

					ofxGgmlScriptSourceFileEntry fe;
					fe.name = path;
					fe.fullPath = rawPrefix + path;
					fe.isDirectory = false;
					entries.push_back(std::move(fe));
				}

				std::sort(entries.begin(), entries.end(),
					[](const ofxGgmlScriptSourceFileEntry & a,
						const ofxGgmlScriptSourceFileEntry & b) {
						return a.name < b.name;
					});
				status = ofToString(entries.size()) + " files from GitHub";
			}
		}

		if (m_cancelFetch.load(std::memory_order_acquire) || m_fetchGeneration.load(std::memory_order_acquire) != generation) {
			m_fetching.store(false, std::memory_order_release);
			return;
		}

		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_sourceType == sourceType &&
				m_fetchGeneration.load(std::memory_order_acquire) == generation) {
				m_files = std::move(entries);
				m_status = status;
				pushFetchDiagnosticLocked(
					hadError ? "error" : "complete",
					status,
					generation);
			}
		}
		m_fetching.store(false, std::memory_order_release);
	});
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_fetchThread = std::move(worker);
	}

	return true;
}

bool ofxGgmlScriptSource::rescan() {
	ofxGgmlScriptSourceType type;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		type = m_sourceType;
	}
	if (type == ofxGgmlScriptSourceType::LocalFolder) {
		std::lock_guard<std::mutex> lock(m_mutex);
		return scanLocalFolderLocked();
	} else if (type == ofxGgmlScriptSourceType::Internet) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_files.clear();
		for (const auto & url : m_internetUrls) {
			ofxGgmlScriptSourceFileEntry fe;
			fe.name = url.size() > 96 ? url.substr(0, 96) + "..." : url;
			fe.fullPath = url;
			fe.isDirectory = false;
			m_files.push_back(std::move(fe));
		}
		m_status = m_internetUrls.empty()
			? "No internet sources"
			: ofToString(m_internetUrls.size()) + " internet sources";
		return true;
	}
	return true;
}

bool ofxGgmlScriptSource::loadFileContent(int index, std::string & outContent) {
	outContent.clear();
	ofxGgmlScriptSourceType type = ofxGgmlScriptSourceType::None;
	ofxGgmlScriptSourceFileEntry entry;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (index < 0 || index >= static_cast<int>(m_files.size())) return false;
		entry = m_files[static_cast<size_t>(index)];
		type = m_sourceType;
	}
	if (entry.isDirectory) return false;

	if (entry.isCached) {
		outContent = entry.cachedContent;
		return true;
	}

	if (type == ofxGgmlScriptSourceType::LocalFolder) {
		std::ifstream in(entry.fullPath, std::ios::binary);
		if (!in.is_open()) {
			std::lock_guard<std::mutex> lock(m_mutex);
			m_status = "Failed to load: " + entry.name;
			return false;
		}
		std::string content((std::istreambuf_iterator<char>(in)),
			std::istreambuf_iterator<char>());
		in.close();
		outContent = std::move(content);
		std::lock_guard<std::mutex> lock(m_mutex);
		m_files[static_cast<size_t>(index)].cachedContent = outContent;
		m_files[static_cast<size_t>(index)].isCached = true;
		m_status = "Loaded: " + entry.name;
		return true;
	}

	if (type == ofxGgmlScriptSourceType::GitHubRepo) {
		const std::string expectedPrefix = "https://raw.githubusercontent.com/";
		if (entry.fullPath.rfind(expectedPrefix, 0) != 0) {
			std::lock_guard<std::mutex> lock(m_mutex);
			m_status = "Invalid URL: " + entry.name;
			return false;
		}
		ofHttpResponse response = ofLoadURL(entry.fullPath);
		if (response.status < 200 || response.status >= 300) {
			std::lock_guard<std::mutex> lock(m_mutex);
			m_status = "Failed to download: " + entry.name;
			return false;
		}
		outContent = response.data.getText();
		std::lock_guard<std::mutex> lock(m_mutex);
		m_files[static_cast<size_t>(index)].cachedContent = outContent;
		m_files[static_cast<size_t>(index)].isCached = true;
		m_status = "Loaded: " + entry.name;
		return true;
	}

	if (type == ofxGgmlScriptSourceType::Internet) {
		if (!isValidUrl(entry.fullPath)) {
			std::lock_guard<std::mutex> lock(m_mutex);
			m_status = "Invalid URL: " + entry.name;
			return false;
		}
		ofHttpResponse response = ofLoadURL(entry.fullPath);
		if (response.status < 200 || response.status >= 300) {
			std::lock_guard<std::mutex> lock(m_mutex);
			m_status = "Failed to download: " + entry.name;
			return false;
		}
		outContent = response.data.getText();
		std::lock_guard<std::mutex> lock(m_mutex);
		m_files[static_cast<size_t>(index)].cachedContent = outContent;
		m_files[static_cast<size_t>(index)].isCached = true;
		m_status = "Loaded: " + entry.name;
		return true;
	}

	return false;
}

bool ofxGgmlScriptSource::saveToLocalSource(const std::string & filename, const std::string & content) {
	std::string folder;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_sourceType != ofxGgmlScriptSourceType::LocalFolder || m_localFolderPath.empty()) {
			return false;
		}
		folder = m_localFolderPath;
	}

	if (filename.empty() || filename.find('/') != std::string::npos ||
		filename.find('\\') != std::string::npos || filename.find("..") != std::string::npos) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_status = "Invalid filename";
		return false;
	}

	const std::filesystem::path outPath = std::filesystem::path(folder) / filename;
	std::ofstream out(outPath, std::ios::binary);
	if (!out.is_open()) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_status = "Failed to save: " + filename;
		return false;
	}
	out << content;
	out.close();

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_status = "Saved: " + filename;
	}
	return rescan();
}

ofxGgmlScriptSourceType ofxGgmlScriptSource::getSourceType() const noexcept {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_sourceType;
}

std::string ofxGgmlScriptSource::getLocalFolderPath() const noexcept {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_localFolderPath;
}

std::string ofxGgmlScriptSource::getGitHubOwnerRepo() const noexcept {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_gitHubOwnerRepo;
}

std::string ofxGgmlScriptSource::getGitHubBranch() const noexcept {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_gitHubBranch;
}

std::vector<std::string> ofxGgmlScriptSource::getInternetUrls() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_internetUrls;
}

std::vector<ofxGgmlScriptSourceFileEntry> ofxGgmlScriptSource::getFiles() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_files;
}

std::string ofxGgmlScriptSource::getStatus() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_status;
}

bool ofxGgmlScriptSource::isFetching() const noexcept {
	return m_fetching.load(std::memory_order_acquire);
}

std::vector<ofxGgmlScriptSourceFetchDiagnostic> ofxGgmlScriptSource::getFetchDiagnostics() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_fetchDiagnostics;
}

void ofxGgmlScriptSource::cancelFetchWorker(const std::string & reason) {
	std::thread worker;
	bool wasFetching = false;
	bool hadWorker = false;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		wasFetching = m_fetching.load();
		hadWorker = m_fetchThread.joinable();
		if (!wasFetching && !hadWorker) {
			return;
		}
		const uint64_t generation = m_fetchGeneration.fetch_add(1) + 1;
		m_cancelFetch.store(true);
		m_fetching.store(false);
		if (wasFetching) {
			pushFetchDiagnosticLocked("cancel", reason, generation);
			if (m_sourceType == ofxGgmlScriptSourceType::GitHubRepo) {
				m_status = "Fetch canceled";
			}
		}
		worker = std::move(m_fetchThread);
	}
	if (worker.joinable()) {
		worker.join();
	}
}

/// Creates a fetch diagnostic entry with timestamp. Maintains maximum of 64 entries.
/// Must be called with m_mutex locked.
void ofxGgmlScriptSource::pushFetchDiagnosticLocked(
	const std::string & state,
	const std::string & message,
	uint64_t generation) {
	ofxGgmlScriptSourceFetchDiagnostic diagnostic;
	diagnostic.state = state;
	diagnostic.message = message;
	diagnostic.generation = generation;
	diagnostic.timestampMs = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch())
			.count());
	m_fetchDiagnostics.push_back(std::move(diagnostic));
	static constexpr size_t kMaxDiagnostics = 64;
	if (m_fetchDiagnostics.size() > kMaxDiagnostics) {
		m_fetchDiagnostics.erase(m_fetchDiagnostics.begin(),
			m_fetchDiagnostics.begin() + (m_fetchDiagnostics.size() - kMaxDiagnostics));
	}
}

bool ofxGgmlScriptSource::scanLocalFolderLocked() {
	m_files.clear();
	std::error_code ec;
	if (!std::filesystem::is_directory(m_localFolderPath, ec) || ec) {
		m_status = "Not a directory";
		return false;
	}
	m_files = scanLocalFolderEntries(m_localFolderPath);
	m_status = ofToString(m_files.size()) + " items";
	return true;
}

std::vector<ofxGgmlScriptSourceFileEntry> ofxGgmlScriptSource::scanLocalFolderEntries(
	const std::string & path) const {
	std::vector<ofxGgmlScriptSourceFileEntry> files;
	std::error_code ec;
	const std::string preferredExt = m_preferredExtension;
	const std::filesystem::path basePath(path);

	auto it = std::filesystem::recursive_directory_iterator(path, std::filesystem::directory_options::skip_permission_denied, ec);
	auto end = std::filesystem::recursive_directory_iterator();
	for (; it != end; it.increment(ec)) {
		if (ec) {
			ec.clear();
			continue;
		}
		const auto& entry = *it;
		std::string filename = entry.path().filename().string();

		// Skip hidden folders like .git to significantly speed up indexing
		if (entry.is_directory(ec) && !filename.empty() && filename[0] == '.') {
			it.disable_recursion_pending();
			continue;
		}

		ofxGgmlScriptSourceFileEntry fe;
		// Use relative path from base directory for better organization
		ec.clear();
		const std::filesystem::path relativePath = std::filesystem::relative(entry.path(), basePath, ec);
		if (ec) {
			fe.name = filename;
			ec.clear();
		} else {
			fe.name = relativePath.string();
		}
		fe.fullPath = entry.path().string();
		fe.isDirectory = entry.is_directory(ec);
		if (ec) {
			ec.clear();
			continue;
		}

		if (fe.isDirectory) {
			files.push_back(std::move(fe));
			continue;
		}

		const std::string ext = normalizeLower(entry.path().extension().string());
		bool include = hasSourceExtension(ext, false);
		if (!preferredExt.empty()) {
			include = (ext == preferredExt);
		}
		if (include) {
			files.push_back(std::move(fe));
		}
	}

	std::sort(files.begin(), files.end(),
		[](const ofxGgmlScriptSourceFileEntry & a, const ofxGgmlScriptSourceFileEntry & b) {
			if (a.isDirectory != b.isDirectory) return a.isDirectory > b.isDirectory;
			return a.name < b.name;
		});
	return files;
}

/// Validates GitHub owner/repo format (e.g., "owner/repo").
/// Rejects path traversal, multiple slashes, and non-alphanumeric characters (except -_.).
bool ofxGgmlScriptSource::isValidOwnerRepo(const std::string & ownerRepo) {
	if (ownerRepo.empty() || ownerRepo.find("..") != std::string::npos) return false;
	const size_t slashPos = ownerRepo.find('/');
	if (slashPos == std::string::npos) return false;
	if (ownerRepo.find('/', slashPos + 1) != std::string::npos) return false;
	if (slashPos == 0 || slashPos == ownerRepo.size() - 1) return false;
	for (char c : ownerRepo) {
		if (!std::isalnum(static_cast<unsigned char>(c)) &&
			c != '/' && c != '-' && c != '_' && c != '.') {
			return false;
		}
	}
	return true;
}

/// Validates GitHub branch name.
/// Rejects path traversal (..), leading/trailing/double slashes, and control characters.
bool ofxGgmlScriptSource::isValidBranch(const std::string & branch) {
	if (branch.empty()) return false;
	// Reject any path traversal patterns
	if (branch.find("..") != std::string::npos) return false;
	// Reject leading/trailing slashes and double slashes
	if (branch.front() == '/' || branch.back() == '/') return false;
	if (branch.find("//") != std::string::npos) return false;
	// Reject control characters
	for (char c : branch) {
		if (static_cast<unsigned char>(c) < 32) return false;
		if (!std::isalnum(static_cast<unsigned char>(c)) &&
			c != '-' && c != '_' && c != '.' && c != '/') {
			return false;
		}
	}
	return true;
}

/// Validates repository file path for safety.
/// Rejects path traversal (..), absolute paths, backslashes, double slashes,
/// control characters, and null bytes to prevent injection attacks.
bool ofxGgmlScriptSource::isSafeRepoPath(const std::string & path) {
	if (path.empty()) return false;
	// Reject any path traversal patterns
	if (path.find("..") != std::string::npos) return false;
	// Reject absolute paths and backslashes
	if (path.front() == '/' || path.find('\\') != std::string::npos) return false;
	// Reject double slashes (could indicate path manipulation)
	if (path.find("//") != std::string::npos) return false;
	// Reject all control characters (ASCII < 32)
	for (char c : path) {
		if (static_cast<unsigned char>(c) < 32) return false;
	}
	// Validate that the path doesn't contain any suspicious patterns
	// that could be used for injection
	if (path.find('\0') != std::string::npos) return false;
	return true;
}

/// Trims leading and trailing whitespace from a string.
std::string ofxGgmlScriptSource::trim(const std::string & s) {
	size_t start = 0;
	while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) start++;
	size_t end = s.size();
	while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) end--;
	return s.substr(start, end - start);
}

/// Validates URL for internet script sources.
/// Only allows http/https schemes. Rejects control characters, null bytes, whitespace,
/// and shell metacharacters in query strings to prevent injection attacks.
bool ofxGgmlScriptSource::isValidUrl(const std::string & url) {
	if (url.empty()) return false;
	// Reject whitespace characters
	if (url.find(' ') != std::string::npos || url.find('\t') != std::string::npos) return false;
	// Reject control characters
	for (char c : url) {
		if (static_cast<unsigned char>(c) < 32) return false;
	}
	// Reject null bytes
	if (url.find('\0') != std::string::npos) return false;

	const std::string lower = normalizeLower(url);
	// Only allow http and https schemes
	const bool hasValidScheme = (lower.rfind("http://", 0) == 0 || lower.rfind("https://", 0) == 0);
	if (!hasValidScheme) return false;

	// Ensure URL has content after the scheme
	const size_t schemeEnd = lower.find("://");
	if (schemeEnd == std::string::npos || schemeEnd + 3 >= url.size()) return false;

	// Reject URLs with potentially dangerous characters in query/fragment
	// These could be used for shell injection if URLs are ever passed to shell commands
	const size_t queryStart = url.find('?');
	if (queryStart != std::string::npos) {
		const std::string query = url.substr(queryStart);
		// Reject shell metacharacters in query parameters
		if (query.find(';') != std::string::npos ||
		    query.find('`') != std::string::npos ||
		    query.find('$') != std::string::npos ||
		    query.find('|') != std::string::npos) {
			return false;
		}
	}

	return true;
}

bool ofxGgmlScriptSource::hasSourceExtension(
	const std::string & ext, bool includeTextLikeExtensions) {
	static constexpr const char* sourceExts[] = {
		".cpp", ".h", ".py", ".js", ".ts", ".rs", ".go",
		".glsl", ".vert", ".frag", ".sh", ".c", ".hpp",
		".java", ".kt", ".swift", ".lua", ".rb", ".cs"
	};
	static constexpr const char* textLikeExts[] = {
		".md", ".txt", ".json", ".yaml", ".yml", ".toml"
	};
	for (const char* candidate : sourceExts) {
		if (ext == candidate) return true;
	}
	if (includeTextLikeExtensions) {
		for (const char* candidate : textLikeExts) {
			if (ext == candidate) return true;
		}
	}
	return false;
}
