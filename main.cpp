#include <algorithm>
#include <atomic>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <regex>
#include <span>
#include <sstream>
#include <thread>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

struct Config {
  fs::path dirPath;
  unsigned long long maxFileSizeB = 0;
  bool recursiveSearch = true;
  std::vector<std::string> fileExtensions;
  bool ignoreDotFolders = true;
  std::vector<std::string> ignoredFolders;
  std::vector<std::string> ignoredFiles;
  std::vector<std::string> regexFilters;
  bool removeComments = false;
  bool removeEmptyLines = false;
  bool showRelativePath = false;
  bool ordered = false;
  std::vector<fs::path> lastFiles;
  std::vector<fs::path> lastDirs;
  bool markdownlintFixes = false;
};

class DirectoryProcessor {
public:
  DirectoryProcessor(Config cfg)
      : config(std::move(cfg)),
        threadCount(std::min(std::thread::hardware_concurrency(), 8u)) {}

  void stop() { shouldStop = true; }

  bool process();

private:
  Config config;
  std::atomic<bool> shouldStop{false};
  std::atomic<size_t> processedFiles{0};
  std::atomic<size_t> totalBytes{0};
  mutable std::mutex consoleMutex;
  mutable std::mutex orderedResultsMutex;
  std::vector<fs::path> lastFilesList;

  const unsigned int threadCount;

  bool isFileSizeValid(const fs::path &path) const;
  bool isFileExtensionAllowed(const fs::path &path) const;
  bool shouldIgnoreFolder(const fs::path &path) const;
  bool shouldIgnoreFile(const fs::path &path) const;
  bool matchesRegexFilters(const fs::path &path) const;
  void printToConsole(const std::string &message) const;
  void logError(const std::string &message) const;
  std::vector<fs::path> collectFiles();
  void processFileChunk(std::span<const fs::path> chunk,
                        std::vector<std::pair<fs::path, std::string>> &results);
  void
  processSingleFile(const fs::path &path,
                    std::vector<std::pair<fs::path, std::string>> &results);
  void processLastFiles();
  std::string removeCppComments(const std::string &code);
};

bool DirectoryProcessor::isFileSizeValid(const fs::path &path) const {
  try {
    return config.maxFileSizeB == 0 ||
           fs::file_size(path) <= config.maxFileSizeB;
  } catch (const fs::filesystem_error &) {
    return false;
  }
}

bool DirectoryProcessor::isFileExtensionAllowed(const fs::path &path) const {
  if (config.fileExtensions.empty())
    return true;
  const auto ext = path.extension().string();
  if (ext.empty())
    return false;
  return std::find(config.fileExtensions.begin(), config.fileExtensions.end(),
                   ext.substr(1)) != config.fileExtensions.end();
}

bool DirectoryProcessor::shouldIgnoreFolder(const fs::path &path) const {
  if (config.ignoreDotFolders && path.filename().string().front() == '.')
    return true;
  return std::find(config.ignoredFolders.begin(), config.ignoredFolders.end(),
                   path.filename().string()) != config.ignoredFolders.end();
}

bool DirectoryProcessor::shouldIgnoreFile(const fs::path &path) const {
  const auto filename = path.filename().string();
  const auto relativePath = fs::relative(path, config.dirPath).string();

  // Check ignoredFiles list for either the filename or the relative path
  return std::find(config.ignoredFiles.begin(), config.ignoredFiles.end(),
                   filename) != config.ignoredFiles.end() ||
         std::find(config.ignoredFiles.begin(), config.ignoredFiles.end(),
                   relativePath) != config.ignoredFiles.end();
}

bool DirectoryProcessor::matchesRegexFilters(const fs::path &path) const {
  if (config.regexFilters.empty())
    return false;
  const std::string filename = path.filename().string();
  for (const auto &regexStr : config.regexFilters) {
    try {
      if (std::regex_search(filename, std::regex(regexStr)))
        return true;
    } catch (const std::regex_error &e) {
      logError("Invalid regex: " + regexStr + ": " + e.what());
    }
  }
  return false;
}

void DirectoryProcessor::printToConsole(const std::string &message) const {
  std::lock_guard<std::mutex> lock(consoleMutex);
  std::cout << message;
}

void DirectoryProcessor::logError(const std::string &message) const {
  std::lock_guard<std::mutex> lock(consoleMutex);
  std::cerr << "ERROR: " << message << '\n';
}

std::string DirectoryProcessor::removeCppComments(const std::string &code) {
  std::string result;
  bool inString = false;
  bool inChar = false;
  bool inSingleLineComment = false;
  bool inMultiLineComment = false;

  for (size_t i = 0; i < code.size(); ++i) {
    if (inString) {
      result += code[i];
      if (code[i] == '\\' && i + 1 < code.size()) {
        result += code[++i]; // Handle escaped characters
      } else if (code[i] == '"') {
        inString = false;
      }
    } else if (inChar) {
      result += code[i];
      if (code[i] == '\\' && i + 1 < code.size()) {
        result += code[++i]; // Handle escaped characters
      } else if (code[i] == '\'') {
        inChar = false;
      }
    } else if (inSingleLineComment) {
      if (code[i] == '\n') {
        inSingleLineComment = false;
        result += code[i];
      }
    } else if (inMultiLineComment) {
      if (code[i] == '*' && i + 1 < code.size() && code[i + 1] == '/') {
        inMultiLineComment = false;
        ++i; // Skip the closing '/'
      }
    } else {
      if (code[i] == '"') {
        inString = true;
        result += code[i];
      } else if (code[i] == '\'') {
        inChar = true;
        result += code[i];
      } else if (code[i] == '/' && i + 1 < code.size()) {
        if (code[i + 1] == '/') {
          inSingleLineComment = true;
          ++i;
        } else if (code[i + 1] == '*') {
          inMultiLineComment = true;
          ++i;
        } else {
          result += code[i];
        }
      } else {
        result += code[i];
      }
    }
  }
  return result;
}

std::vector<fs::path> DirectoryProcessor::collectFiles() {
  std::vector<fs::path> normalFiles;
  lastFilesList.clear();
  std::unordered_set<fs::path> lastFilesSet;

  auto isLastFile = [&](const fs::path &absPath) {
    fs::path relPath = fs::relative(absPath, config.dirPath);

    // Check in config.lastFiles (exact or filename match)
    if (std::find(config.lastFiles.begin(), config.lastFiles.end(), relPath) !=
            config.lastFiles.end() ||
        std::find(config.lastFiles.begin(), config.lastFiles.end(),
                  absPath.filename()) != config.lastFiles.end()) {
      return true;
    }

    // Check if the file is within any of the lastDirs
    return std::any_of(config.lastDirs.begin(), config.lastDirs.end(),
                       [&](const auto &dirRelPath) {
                         return relPath.string().find(dirRelPath.string()) == 0;
                       });
  };

  auto processEntry = [&](const auto &entry) {
    if (shouldStop)
      return;
    if (fs::is_directory(entry) && shouldIgnoreFolder(entry.path())) {
      if (config.recursiveSearch)
        fs::recursive_directory_iterator(entry).disable_recursion_pending();
      return;
    }

    if (fs::is_regular_file(entry) && isFileExtensionAllowed(entry) &&
        !shouldIgnoreFile(entry.path()) && !matchesRegexFilters(entry.path())) {
      if (isLastFile(entry.path())) {
        if (lastFilesSet.insert(entry.path()).second)
          lastFilesList.push_back(entry.path());
      } else {
        normalFiles.push_back(entry.path());
      }
    }
  };

  try {
    auto options = fs::directory_options::skip_permission_denied;

    if (config.recursiveSearch) {
      for (auto &entry :
           fs::recursive_directory_iterator(config.dirPath, options)) {
        processEntry(entry);
      }
    } else {
      for (auto &entry : fs::directory_iterator(config.dirPath, options)) {
        processEntry(entry);
      }
    }
  } catch (const fs::filesystem_error &e) {
    logError("Error scanning directory: " + std::string(e.what()));
  }

  std::sort(normalFiles.begin(), normalFiles.end());
  return normalFiles;
}

void DirectoryProcessor::processFileChunk(
    std::span<const fs::path> chunk,
    std::vector<std::pair<fs::path, std::string>> &results) {
  for (const auto &path : chunk) {
    if (shouldStop)
      break;
    try {
      std::ifstream file(path, std::ios::binary);
      if (!file || !isFileSizeValid(path))
        continue;

      std::stringstream content;
      content << (config.markdownlintFixes ? "\n## File: " : "\n### File: ")
              << (config.showRelativePath
                      ? fs::relative(path, config.dirPath).string()
                      : path.filename().string())
              << (config.markdownlintFixes ? "\n" : "") << "\n```";
      if (path.has_extension())
        content << path.extension().string().substr(1);
      content << "\n";

      std::string fileContent((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());

      if (config.removeComments)
        fileContent = removeCppComments(fileContent);

      std::istringstream iss(fileContent);
      std::string line;
      while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r')
          line.pop_back();
        if (config.removeEmptyLines &&
            line.find_first_not_of(" \t\r\n") == std::string::npos)
          continue;
        content << line << '\n';
      }

      content << "\n```\n";
      totalBytes += fs::file_size(path);

      if (config.ordered) {
        std::lock_guard<std::mutex> lock(orderedResultsMutex);
        results.emplace_back(path, content.str());
      } else {
        printToConsole(content.str());
      }
      ++processedFiles;
    } catch (const std::exception &e) {
      logError("Error processing " + path.string() + ": " + e.what());
    }
  }
}

void DirectoryProcessor::processSingleFile(
    const fs::path &path,
    std::vector<std::pair<fs::path, std::string>> &results) {
  if (shouldStop)
    return;
  try {
    std::ifstream file(path, std::ios::binary);
    if (!file || !isFileSizeValid(path))
      return;

    std::stringstream content;
    content << (config.markdownlintFixes ? "\n## File: " : "\n### File: ")
            << (config.showRelativePath
                    ? fs::relative(path, config.dirPath).string()
                    : path.filename().string())
            << (config.markdownlintFixes ? "\n" : "") << "\n```";
    if (path.has_extension())
      content << path.extension().string().substr(1);
    content << "\n";

    std::string fileContent((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());

    if (config.removeComments)
      fileContent = removeCppComments(fileContent);

    std::istringstream iss(fileContent);
    std::string line;
    while (std::getline(iss, line)) {
      if (!line.empty() && line.back() == '\r')
        line.pop_back();
      if (config.removeEmptyLines &&
          line.find_first_not_of(" \t\r\n") == std::string::npos)
        continue;
      content << line << '\n';
    }

    content << "\n```\n";
    totalBytes += fs::file_size(path);

    printToConsole(content.str());
    ++processedFiles;
  } catch (const std::exception &e) {
    logError("Error processing " + path.string() + ": " + e.what());
  }
}

void DirectoryProcessor::processLastFiles() {
  std::sort(lastFilesList.begin(), lastFilesList.end(),
            [&](const fs::path &a, const fs::path &b) {
              fs::path relA = fs::relative(a, config.dirPath);
              fs::path relB = fs::relative(b, config.dirPath);

              auto findPosition = [&](const fs::path &relPath) -> int {
                // Check if the file is in lastFiles (exact match)
                auto exactIt = std::find(config.lastFiles.begin(),
                                         config.lastFiles.end(), relPath);
                if (exactIt != config.lastFiles.end()) {
                  return config.lastDirs.size() +
                         std::distance(config.lastFiles.begin(), exactIt);
                }

                // Check if the file is in lastFiles (filename match)
                auto filenameIt =
                    std::find(config.lastFiles.begin(), config.lastFiles.end(),
                              relPath.filename());
                if (filenameIt != config.lastFiles.end()) {
                  return config.lastDirs.size() +
                         std::distance(config.lastFiles.begin(), filenameIt);
                }

                // Check if the file is in any of the lastDirs
                for (size_t i = 0; i < config.lastDirs.size(); ++i) {
                  if (relPath.string().find(config.lastDirs[i].string()) == 0) {
                    return i;
                  }
                }

                return -1; // Should not reach here
              };

              int posA = findPosition(relA);
              int posB = findPosition(relB);
              return posA < posB;
            });

  for (const auto &file : lastFilesList) {
    // Use processSingleFile for individual files
    std::vector<std::pair<fs::path, std::string>>
        results; // Not used, but keeping the signature consistent
    processSingleFile(file, results);
  }
}

bool DirectoryProcessor::process() {
  if (!fs::exists(config.dirPath) || !fs::is_directory(config.dirPath)) {
    logError("Invalid directory path: " + config.dirPath.string());
    return false;
  }

  auto normalFiles = collectFiles();

  if (normalFiles.empty() && config.lastFiles.empty() &&
      config.lastDirs.empty()) {
    printToConsole("No matching files found in: " + config.dirPath.string() +
                   "\n");
    return true;
  }

  std::vector<std::pair<fs::path, std::string>> orderedResults;
  if (config.ordered)
    orderedResults.reserve(normalFiles.size());

  std::vector<std::thread> threads;
  const size_t filesPerThread =
      (normalFiles.size() + threadCount - 1) / threadCount;

  if (config.markdownlintFixes)
    printToConsole("#\n");

  for (size_t i = 0; i < threadCount && i * filesPerThread < normalFiles.size();
       ++i) {
    threads.emplace_back([this, &normalFiles, &orderedResults, i,
                          filesPerThread] {
      try {
        processFileChunk(
            std::span(normalFiles.begin() + i * filesPerThread,
                      std::min(normalFiles.begin() + (i + 1) * filesPerThread,
                               normalFiles.end())),
            orderedResults);
      } catch (const std::exception &e) {
        logError("Exception in thread: " + std::string(e.what()));
      }
    });
  }

  for (auto &thread : threads)
    thread.join();

  if (config.ordered) {
    std::lock_guard<std::mutex> lock(orderedResultsMutex);
    std::sort(
        orderedResults.begin(), orderedResults.end(),
        [&normalFiles](const auto &a, const auto &b) {
          return std::find(normalFiles.begin(), normalFiles.end(), a.first) <
                 std::find(normalFiles.begin(), normalFiles.end(), b.first);
        });
    for (const auto &result : orderedResults)
      printToConsole(result.second);
  }

  processLastFiles();
  return true;
}

DirectoryProcessor *globalProcessor = nullptr;
void signalHandler(int signum) {
  if (globalProcessor) {
    std::cout << "\nInterrupt received, stopping...\n";
    globalProcessor->stop();
  }
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr
        << "Usage: " << argv[0] << " <directory_path> [options]\n"
        << "Options:\n"
        << "  -m, --max-size <bytes>  Maximum file size in bytes (default: no "
           "limit)\n"
        << "  -n, --no-recursive     Disable recursive search\n"
        << "  -e, --ext <ext>        Process only files with given extension "
           "(can be used multiple times, grouped)\n"
        << "  -d, --dot-folders      Include folders starting with a dot\n"
        << "  -i, --ignore <item>    Ignore specific folder or file (can be "
           "used multiple times, grouped)\n"
        << "  -r, --regex <pattern>  Exclude files matching the regex pattern "
           "(can be used multiple times, grouped)\n"
        << "  -c, --remove-comments  Remove C++ style comments (// and /* */) "
           "from code\n"
        << "  -l, --remove-empty-lines Remove empty lines from output\n"
        << "  -p, --relative-path    Show relative path in file headers "
           "instead of filename\n"
        << "  -o, --ordered          Output files in the order they were "
           "found\n"
        << "  -z, --last <item>      Process specified file, directory, or "
           "filename last (order of multiple -z options is preserved).\n"
        << "  -w, --markdownlint-fixes Enable fixes for Markdown linting\n";
    return 1;
  }

  Config config;
  config.dirPath = argv[1];

  for (int i = 2; i < argc; ++i) {
    std::string_view arg = argv[i];
    if ((arg == "-m" || arg == "--max-size") && i + 1 < argc) {
      config.maxFileSizeB = std::stoll(argv[++i]);
    } else if (arg == "-n" || arg == "--no-recursive") {
      config.recursiveSearch = false;
    } else if (arg == "-e" || arg == "--ext") {
      while (i + 1 < argc && argv[i + 1][0] != '-')
        config.fileExtensions.emplace_back(argv[++i]);
    } else if (arg == "-d" || arg == "--dot-folders") {
      config.ignoreDotFolders = false;
    } else if (arg == "-i" || arg == "--ignore") {
      while (i + 1 < argc && argv[i + 1][0] != '-') {
        std::string item = argv[++i];
        // Store the ignored item as-is (do not prepend the input directory)
        if (fs::path(item).is_absolute())
          config.ignoredFiles.emplace_back(item);
        else
          config.ignoredFiles.emplace_back(item);
      }
    } else if (arg == "-r" || arg == "--regex") {
      while (i + 1 < argc && argv[i + 1][0] != '-')
        config.regexFilters.emplace_back(argv[++i]);
    } else if (arg == "-c" || arg == "--remove-comments") {
      config.removeComments = true;
    } else if (arg == "-l" || arg == "--remove-empty-lines") {
      config.removeEmptyLines = true;
    } else if (arg == "-p" || arg == "--relative-path") {
      config.showRelativePath = true;
    } else if (arg == "-o" || arg == "--ordered") {
      config.ordered = true;
    } else if (arg == "-z" || arg == "--last") {
      while (i + 1 < argc && argv[i + 1][0] != '-') {
        std::string entry = argv[++i];
        // Store the entry as-is (do not prepend the input directory)
        if (fs::is_directory(config.dirPath / entry))
          config.lastDirs.emplace_back(fs::path(entry));
        else
          config.lastFiles.emplace_back(fs::path(entry));
      }
    } else if (arg == "-w" || arg == "--markdownlint-fixes") {
      config.markdownlintFixes = true;
    } else {
      std::cerr << "Invalid option: " << arg << "\n";
      return 1;
    }
  }

  DirectoryProcessor processor(std::move(config));
  globalProcessor = &processor;
  std::signal(SIGINT, signalHandler);
  return processor.process() ? 0 : 1;
}