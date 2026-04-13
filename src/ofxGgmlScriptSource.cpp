#include "ofxGgmlScriptSource.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <thread>

namespace {

std::string normalizeLower(const std::string & s) {
	std::string out = s;
	std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return out;
}

}

void ofxGgmlScriptSource::clear() {
	const uint64_t generation = m_fetchGeneration.fetch_add(1) + 1;
	(void)generation;
	m_fetching.store(false);
	std::lock_guard<std::mutex> lock(m_mutex);
	m_sourceType = ofxGgmlScriptSourceType::None;
	m_localFolderPath.clear();
	m_gitHubOwnerRepo.clear();
	m_gitHubBranch.clear();
	m_files.clear();
	m_status.clear();
}

void ofxGgmlScriptSource::setGitHubMode() {
	const uint64_t generation = m_fetchGeneration.fetch_add(1) + 1;
	(void)generation;
	m_fetching.store(false);
	std::lock_guard<std::mutex> lock(m_mutex);
	m_sourceType = ofxGgmlScriptSourceType::GitHubRepo;
	m_localFolderPath.clear();
	m_files.clear();
	m_status.clear();
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
	const uint64_t generation = m_fetchGeneration.fetch_add(1) + 1;
	(void)generation;
	m_fetching.store(false);
	std::lock_guard<std::mutex> lock(m_mutex);
	m_sourceType = ofxGgmlScriptSourceType::LocalFolder;
	m_localFolderPath = path;
	m_gitHubOwnerRepo.clear();
	m_gitHubBranch.clear();
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
		m_files.clear();
		m_status.clear();
	}

	return true;
}

bool ofxGgmlScriptSource::fetchGitHubRepo() {
	std::string ownerRepo;
	std::string branch;
	uint64_t generation = 0;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_sourceType != ofxGgmlScriptSourceType::GitHubRepo) {
			m_status = "Source is not GitHub";
			return false;
		}
		ownerRepo = m_gitHubOwnerRepo;
		branch = m_gitHubBranch;
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
		generation = m_fetchGeneration.fetch_add(1) + 1;
	}
	m_fetching.store(true);

	const std::string apiUrl = "https://api.github.com/repos/" + ownerRepo +
		"/git/trees/" + branch + "?recursive=1";
	const std::string rawPrefix = "https://raw.githubusercontent.com/" +
		ownerRepo + "/" + branch + "/";
	const std::string preferredExt = getPreferredExtension();

	std::thread([this, generation, apiUrl, rawPrefix, preferredExt]() {
		std::vector<ofxGgmlScriptSourceFileEntry> entries;
		std::string status;

		ofHttpResponse response = ofLoadURL(apiUrl);
		if (response.status < 200 || response.status >= 300) {
			if (response.status == 404) {
				status = "Repo not found";
			} else {
				status = "GitHub API error: " + ofToString(response.status);
			}
		} else {
			const std::string body = response.data.getText();
			ofJson json = ofJson::parse(body, nullptr, false);
			if (json.is_discarded() || !json.contains("tree") || !json["tree"].is_array()) {
				status = "Failed to parse GitHub response";
			} else {
				for (const auto & item : json["tree"]) {
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

		if (m_fetchGeneration.load() != generation) {
			return;
		}

		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_sourceType == ofxGgmlScriptSourceType::GitHubRepo &&
				m_fetchGeneration.load() == generation) {
				m_files = std::move(entries);
				m_status = status;
			}
		}
		m_fetching.store(false);
	}).detach();

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

ofxGgmlScriptSourceType ofxGgmlScriptSource::getSourceType() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_sourceType;
}

std::string ofxGgmlScriptSource::getLocalFolderPath() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_localFolderPath;
}

std::string ofxGgmlScriptSource::getGitHubOwnerRepo() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_gitHubOwnerRepo;
}

std::string ofxGgmlScriptSource::getGitHubBranch() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_gitHubBranch;
}

std::vector<ofxGgmlScriptSourceFileEntry> ofxGgmlScriptSource::getFiles() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_files;
}

std::string ofxGgmlScriptSource::getStatus() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_status;
}

bool ofxGgmlScriptSource::isFetching() const {
	return m_fetching.load();
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

	for (const auto & entry : std::filesystem::directory_iterator(path, ec)) {
		if (ec) break;
		ofxGgmlScriptSourceFileEntry fe;
		fe.name = entry.path().filename().string();
		fe.fullPath = entry.path().string();
		fe.isDirectory = entry.is_directory(ec);
		if (ec) continue;

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

bool ofxGgmlScriptSource::isValidBranch(const std::string & branch) {
	if (branch.empty() || branch.find("..") != std::string::npos) return false;
	if (branch.front() == '/' || branch.back() == '/') return false;
	for (char c : branch) {
		if (!std::isalnum(static_cast<unsigned char>(c)) &&
			c != '-' && c != '_' && c != '.' && c != '/') {
			return false;
		}
	}
	return true;
}

bool ofxGgmlScriptSource::isSafeRepoPath(const std::string & path) {
	if (path.empty()) return false;
	if (path.find("..") != std::string::npos) return false;
	if (path.front() == '/' || path.find('\\') != std::string::npos) return false;
	for (char c : path) {
		if (static_cast<unsigned char>(c) < 32) return false;
	}
	return true;
}

std::string ofxGgmlScriptSource::trim(const std::string & s) {
	size_t start = 0;
	while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) start++;
	size_t end = s.size();
	while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) end--;
	return s.substr(start, end - start);
}

bool ofxGgmlScriptSource::hasSourceExtension(
	const std::string & ext, bool includeTextLikeExtensions) {
	static const std::vector<std::string> sourceExts = {
		".cpp", ".h", ".py", ".js", ".ts", ".rs", ".go",
		".glsl", ".vert", ".frag", ".sh", ".c", ".hpp",
		".java", ".kt", ".swift", ".lua", ".rb", ".cs"
	};
	static const std::vector<std::string> textLikeExts = {
		".md", ".txt", ".json", ".yaml", ".yml", ".toml"
	};
	for (const auto & candidate : sourceExts) {
		if (ext == candidate) return true;
	}
	if (includeTextLikeExtensions) {
		for (const auto & candidate : textLikeExts) {
			if (ext == candidate) return true;
		}
	}
	return false;
}
