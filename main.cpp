#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <mutex>
#include <regex>
#include <span>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

class DirectoryProcessor {
public:
  struct Config {
    fs::path dirPath;
    unsigned long long maxFileSizeB = 0; // Default: no limit
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
    std::vector<fs::path> lastFiles; // Relative paths to exact files
    std::vector<fs::path> lastDirs;  // Relative paths to directories
    bool markdownlintFixes = false;
  };

private:
  const Config config;
  std::atomic<bool> shouldStop{false};
  std::atomic<size_t> processedFiles{0};
  std::atomic<size_t> totalBytes{0};
  mutable std::mutex consoleMutex;
  mutable std::mutex orderedResultsMutex;
  std::vector<fs::path> lastFilesList;
  const std::chrono::steady_clock::time_point startTime{
      std::chrono::steady_clock::now()};
  const unsigned int threadCount;

  class FileProcessor {
    DirectoryProcessor &processor;
    const fs::path &path;
    std::stringstream content;

    std::string removeCppComments(const std::string &code) {
      std::string result;
      bool inString = false;
      bool inChar = false;
      bool inSingleLineComment = false;
      bool inMultiLineComment = false;

      for (size_t i = 0; i < code.size(); ++i) {
        if (inString) {
          if (code[i] == '\\' && i + 1 < code.size()) {
            result += code[i];
            result += code[++i];
          } else {
            if (code[i] == '"') {
              inString = false;
            }
            result += code[i];
          }
        } else if (inChar) {
          if (code[i] == '\\' && i + 1 < code.size()) {
            result += code[i];
            result += code[++i];
          } else {
            if (code[i] == '\'') {
              inChar = false;
            }
            result += code[i];
          }
        } else if (inSingleLineComment) {
          if (code[i] == '\n') {
            inSingleLineComment = false;
            result += code[i];
          }
        } else if (inMultiLineComment) {
          if (code[i] == '*' && i + 1 < code.size() && code[i + 1] == '/') {
            inMultiLineComment = false;
            ++i;
          }
        } else {
          if (code[i] == '"') {
            inString = true;
            result += code[i];
          } else if (code[i] == '\'') {
            inChar = true;
            result += code[i];
          } else if (code[i] == '/' && i + 1 < code.size() &&
                     code[i + 1] == '/') {
            inSingleLineComment = true;
            ++i;
          } else if (code[i] == '/' && i + 1 < code.size() &&
                     code[i + 1] == '*') {
            inMultiLineComment = true;
            ++i;
          } else {
            result += code[i];
          }
        }
      }

      return result;
    }

    bool readFile() {
      std::ifstream file(path, std::ios::binary);
      if (!file)
        return false;

      if (processor.config.markdownlintFixes)
        content << "\n## File: ";
      else
        content << "\n### File: ";
      if (processor.config.showRelativePath) {
        content << fs::relative(path, processor.config.dirPath).string();
      } else {
        content << path.filename().string();
      }

      if (processor.config.markdownlintFixes)
        content << '\n';

      content << "\n```";
      if (path.has_extension()) {
        content << path.extension().string().substr(1);
      }
      content << "\n";

      // Read file content in chunks to handle large files better
      constexpr size_t BUFFER_SIZE = 8192;
      char buffer[BUFFER_SIZE];
      std::string fileContent;

      while (file.good() && !processor.shouldStop) {
        file.read(buffer, BUFFER_SIZE);
        fileContent.append(buffer, file.gcount());
      }

      if (processor.config.removeComments) {
        fileContent = removeCppComments(fileContent);
      }

      std::string line;
      std::istringstream iss(fileContent);
      while (std::getline(iss, line)) {
        // Remove trailing '\r' to handle CRLF line endings
        if (!line.empty() && line.back() == '\r') {
          line.pop_back();
        }
        if (processor.config.removeEmptyLines &&
            line.find_first_not_of(" \t\r\n") == std::string::npos) {
          continue;
        }
        content << line << '\n';
      }

      content << "\n```\n";
      processor.totalBytes += fs::file_size(path);
      return true;
    }

  public:
    FileProcessor(DirectoryProcessor &proc, const fs::path &p)
        : processor(proc), path(p) {}

    bool process() {
      try {
        if (!processor.isFileSizeValid(path))
          return false;
        if (!readFile()) {
          processor.logError("Failed to process: " + path.string());
          return false;
        }
        if (!processor.config.ordered) {
          processor.printToConsole(content.str());
        }
        return true;
      } catch (const std::exception &e) {
        processor.logError(
            std::format("Error processing {}: {}", path.string(), e.what()));
        return false;
      }
    }

    const std::stringstream &getContent() const { return content; }
  };

  bool isFileSizeValid(const fs::path &path) const {
    try {
      return config.maxFileSizeB == 0 ||
             fs::file_size(path) <= config.maxFileSizeB;
    } catch (const fs::filesystem_error &) {
      return false;
    }
  }

  bool isFileExtensionAllowed(const fs::path &path) const {
    if (config.fileExtensions.empty())
      return true;
    const auto ext = path.extension().string();
    if (ext.empty())
      return false;
    return std::find(config.fileExtensions.begin(), config.fileExtensions.end(),
                     ext.substr(1)) != config.fileExtensions.end();
  }

  bool shouldIgnoreFolder(const fs::path &path) const {
    if (config.ignoreDotFolders && path.filename().string().front() == '.') {
      return true;
    }
    return std::find(config.ignoredFolders.begin(), config.ignoredFolders.end(),
                     path.filename().string()) != config.ignoredFolders.end();
  }

  bool shouldIgnoreFile(const fs::path &path) const {
    return std::find(config.ignoredFiles.begin(), config.ignoredFiles.end(),
                     path.filename().string()) != config.ignoredFiles.end();
  }

  // Function to check if a file matches any of the regex filters
  bool matchesRegexFilters(const fs::path &path) const {
    if (config.regexFilters.empty())
      return false;

    const std::string filename = path.filename().string();
    for (const auto &regexStr : config.regexFilters) {
      try {
        std::regex regex(regexStr);
        if (std::regex_search(filename, regex)) {
          return true;
        }
      } catch (const std::regex_error &e) {
        logError(std::format("Invalid regex: {}: {}", regexStr, e.what()));
      }
    }
    return false;
  }

  void printToConsole(const std::string &message) const {
    std::lock_guard<std::mutex> lock(consoleMutex);
    std::cout << message;
  }

  void logError(const std::string &message) const {
    std::lock_guard<std::mutex> lock(consoleMutex);
    std::cerr << "ERROR: " << message << '\n';
  }

  std::vector<fs::path> collectFiles() {
    std::vector<fs::path> normalFiles;
    lastFilesList.clear();
    std::unordered_set<fs::path> lastFilesSet;

    // Lambda function to check if a file should be treated as a "last" file
    auto isLastFile = [&](const fs::path &absPath) {
      fs::path relPath = fs::relative(absPath, config.dirPath);

      // Check in config.lastFiles (exact or filename match)
      if (std::find(config.lastFiles.begin(), config.lastFiles.end(),
                    relPath) != config.lastFiles.end() ||
          std::find(config.lastFiles.begin(), config.lastFiles.end(),
                    absPath.filename()) != config.lastFiles.end()) {
        return true;
      }

      // Check if the file is within any of the lastDirs
      for (const auto &dirRelPath : config.lastDirs) {
        if (relPath.string().find(dirRelPath.string()) == 0) {
          return true;
        }
      }

      return false;
    };

    auto processEntry = [&](const auto &entry) {
      if (shouldStop)
        return;
      if (fs::is_directory(entry) && shouldIgnoreFolder(entry.path())) {
        if (config.recursiveSearch) {
          auto it = fs::recursive_directory_iterator(entry);
          it.disable_recursion_pending();
        }
        return;
      }

      if (fs::is_regular_file(entry) && isFileExtensionAllowed(entry) &&
          !shouldIgnoreFile(entry.path()) &&
          !matchesRegexFilters(entry.path())) {
        if (isLastFile(entry.path())) {
          // Insert into set to prevent duplicates; add to list if unique
          if (lastFilesSet.insert(entry.path()).second) {
            lastFilesList.push_back(entry.path());
          }
        } else {
          normalFiles.push_back(entry.path());
        }
      }
    };

    try {
      if (config.recursiveSearch) {
        for (auto it = fs::recursive_directory_iterator(
                 config.dirPath, fs::directory_options::skip_permission_denied);
             it != fs::recursive_directory_iterator(); ++it) {
          processEntry(*it);
        }
      } else {
        for (const auto &entry : fs::directory_iterator(
                 config.dirPath,
                 fs::directory_options::skip_permission_denied)) {
          processEntry(entry);
        }
      }
    } catch (const fs::filesystem_error &e) {
      logError("Error scanning directory: " + std::string(e.what()));
    }

    std::sort(normalFiles.begin(), normalFiles.end());
    return normalFiles;
  }

  void
  processFileChunk(std::span<const fs::path> chunk,
                   std::vector<std::pair<fs::path, std::string>> &results) {
    for (const auto &path : chunk) {
      if (shouldStop)
        break;
      try {
        FileProcessor processor(*this, path);
        if (processor.process()) {
          if (config.ordered) {
            // Lock mutex before modifying orderedResults
            std::lock_guard<std::mutex> lock(orderedResultsMutex);
            results.emplace_back(path, processor.getContent().str());
          }
          ++processedFiles;
        }
      } catch (const std::exception &e) {
        logError(std::format("Exception in processFileChunk: {}", e.what()));
      }
    }
  }

  void processLastFiles() {
    // Sort lastFilesList based on the order in config.lastFiles and
    // config.lastDirs
    std::sort(
        lastFilesList.begin(), lastFilesList.end(),
        [&](const fs::path &a, const fs::path &b) {
          fs::path relA = fs::relative(a, config.dirPath);
          fs::path relB = fs::relative(b, config.dirPath);

          auto findPosition = [&](const fs::path &relPath) -> int {
            // Check if the file is in lastFiles (exact match or filename)
            bool isInLastFiles = false;
            auto exactIt = std::find(config.lastFiles.begin(),
                                     config.lastFiles.end(), relPath);
            if (exactIt != config.lastFiles.end()) {
              isInLastFiles = true;
            } else {
              auto filenameIt =
                  std::find(config.lastFiles.begin(), config.lastFiles.end(),
                            relPath.filename());
              if (filenameIt != config.lastFiles.end()) {
                isInLastFiles = true;
              }
            }

            if (!isInLastFiles) {
              // Check if the file is in any of the lastDirs
              for (size_t i = 0; i < config.lastDirs.size(); ++i) {
                if (relPath.string().find(config.lastDirs[i].string()) == 0) {
                  return i; // Position based on lastDirs order
                }
              }
            }

            // Handle files in lastFiles
            if (isInLastFiles) {
              // Find the index in lastFiles considering both exact and filename
              // matches
              exactIt = std::find(config.lastFiles.begin(),
                                  config.lastFiles.end(), relPath);
              if (exactIt != config.lastFiles.end()) {
                return config.lastDirs.size() +
                       std::distance(config.lastFiles.begin(), exactIt);
              } else {
                auto filenameIt =
                    std::find(config.lastFiles.begin(), config.lastFiles.end(),
                              relPath.filename());
                if (filenameIt != config.lastFiles.end()) {
                  return config.lastDirs.size() +
                         std::distance(config.lastFiles.begin(), filenameIt);
                }
              }
            }

            return -1; // Should not reach here
          };

          int posA = findPosition(relA);
          int posB = findPosition(relB);

          if (posA >= config.lastFiles.size() &&
              posB >= config.lastFiles.size()) {
            // Both files are in lastDirs, compare their positions within
            // lastDirs
            return posA < posB;
          } else if (posA >= config.lastFiles.size()) {
            // A is in lastDirs, B is in lastFiles, A should come after B
            return false;
          } else if (posB >= config.lastFiles.size()) {
            // B is in lastDirs, A is in lastFiles, B should come after A
            return true;
          } else {
            // Both files are in lastFiles, compare their positions within
            // lastFiles
            return posA < posB;
          }
        });

    for (const auto &file : lastFilesList) {
      FileProcessor processor(*this, file);
      if (processor.process()) {
        printToConsole(processor.getContent().str());
        ++processedFiles;
      }
    }
  }

public:
  DirectoryProcessor(Config cfg)
      : config(std::move(cfg)),
        threadCount(std::min(std::thread::hardware_concurrency(),
                             static_cast<unsigned int>(8))) {}
  void stop() { shouldStop = true; }

  bool process() {
    if (!fs::exists(config.dirPath) || !fs::is_directory(config.dirPath)) {
      logError("Invalid directory path: " + config.dirPath.string());
      return false;
    }

    auto normalFiles = collectFiles();

    if (normalFiles.empty() && config.lastFiles.empty() &&
        config.lastDirs.empty()) {
      printToConsole(std::format("No matching files found in: {}\n",
                                 config.dirPath.string()));
      return true;
    }

    // Process normal files
    std::vector<std::pair<fs::path, std::string>> orderedResults;
    if (config.ordered) {
      orderedResults.reserve(normalFiles.size());
    }

    std::vector<std::thread> threads;
    const size_t filesPerThread =
        (normalFiles.size() + threadCount - 1) / threadCount;

    if (config.markdownlintFixes)
      printToConsole("#\n");

    for (size_t i = 0;
         i < threadCount && i * filesPerThread < normalFiles.size(); ++i) {
      const size_t start = i * filesPerThread;
      const size_t end = std::min(start + filesPerThread, normalFiles.size());

      threads.emplace_back([this, &normalFiles, start, end, &orderedResults] {
        try {
          processFileChunk(
              std::span(normalFiles.begin() + start, normalFiles.begin() + end),
              orderedResults);
        } catch (const std::exception &e) {
          logError(std::format("Exception in thread: {}", e.what()));
        }
      });
    }

    for (auto &thread : threads) {
      thread.join();
    }

    // Print ordered results for normal files (if -o is enabled)
    if (config.ordered) {
      try {
        // Lock mutex before accessing orderedResults
        std::lock_guard<std::mutex> lock(orderedResultsMutex);
        std::sort(orderedResults.begin(), orderedResults.end(),
                  [&normalFiles](const auto &a, const auto &b) {
                    return std::find(normalFiles.begin(), normalFiles.end(),
                                     a.first) < std::find(normalFiles.begin(),
                                                          normalFiles.end(),
                                                          b.first);
                  });

        for (const auto &result : orderedResults) {
          printToConsole(result.second);
        }
      } catch (const std::exception &e) {
        logError(std::format(
            "Exception while sorting/printing ordered results: {}", e.what()));
      }
    }

    // Process last files (always ordered)
    processLastFiles();

    return true;
  }
};

// Signal handler
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

  try {
    DirectoryProcessor::Config config;
    config.dirPath = argv[1];

    // Parse command line options
    for (int i = 2; i < argc; ++i) {
      std::string_view arg = argv[i];
      if ((arg == "-m" || arg == "--max-size") && i + 1 < argc) {
        config.maxFileSizeB = std::stoll(argv[++i]);
      } else if (arg == "-n" || arg == "--no-recursive") {
        config.recursiveSearch = false;
      } else if (arg == "-e" || arg == "--ext") {
        while (i + 1 < argc && argv[i + 1][0] != '-') {
          config.fileExtensions.push_back(argv[++i]);
        }
      } else if (arg == "-d" || arg == "--dot-folders") {
        config.ignoreDotFolders = false;
      } else if (arg == "-i" || arg == "--ignore") {
        while (i + 1 < argc && argv[i + 1][0] != '-') {
          // Check if the ignored item is a file or a folder
          fs::path itemPath = fs::path(config.dirPath) / argv[++i];
          if (fs::is_regular_file(itemPath)) {
            config.ignoredFiles.push_back(itemPath.filename().string());
          } else if (fs::is_directory(itemPath)) {
            config.ignoredFolders.push_back(itemPath.filename().string());
          } else {
            std::cerr << "Warning: Ignored item '" << itemPath.string()
                      << "' is neither a file nor a directory.\n";
          }
        }
      } else if (arg == "-r" || arg == "--regex") {
        while (i + 1 < argc && argv[i + 1][0] != '-') {
          config.regexFilters.push_back(argv[++i]);
        }
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
          fs::path absPath = config.dirPath / entry;
          if (fs::exists(absPath)) {
            if (fs::is_directory(absPath)) {
              config.lastDirs.push_back(fs::path(entry).lexically_normal());
            } else {
              config.lastFiles.push_back(fs::path(entry).lexically_normal());
            }
          } else {
            // Treat as a file (may not exist yet)
            config.lastFiles.push_back(fs::path(entry).lexically_normal());
          }
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

    // Register signal handler
    std::signal(SIGINT, signalHandler);

    return processor.process() ? 0 : 1;

  } catch (const std::exception &e) {
    std::cerr << "Fatal error: " << e.what() << '\n';
    return 1;
  }
}