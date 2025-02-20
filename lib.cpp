#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <regex>
#include <span>
#include <sstream>
#include <string> // Make sure to include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

// --- Configuration ---
struct Config {
  fs::path dirPath;
  unsigned long long maxFileSizeB = 0;
  bool recursiveSearch = true;
  std::vector<std::string> fileExtensions;
  std::vector<std::string> excludedFileExtensions; // New: Excluded extensions
  bool ignoreDotFolders = true;
  std::vector<fs::path> ignoredFolders;
  std::vector<fs::path> ignoredFiles;
  std::vector<std::string> regexFilters;
  bool removeComments = false;
  bool removeEmptyLines = false;
  bool showFilenameOnly = false;
  bool unorderedOutput = false;
  std::vector<fs::path> lastFiles;
  std::vector<fs::path> lastDirs;
  bool disableMarkdownlintFixes = false;
  bool disableGitignore = false;
  fs::path gitignorePath;
  std::vector<std::string> gitignoreRules;
  bool onlyLast = false; // New option: only include files/dirs in -z
};

// --- Utility Functions ---

// Helper function to trim whitespace from a string
std::string trim(const std::string &str) {
  size_t first = str.find_first_not_of(' ');
  if (std::string::npos == first) {
    return str;
  }
  size_t last = str.find_last_not_of(' ');
  return str.substr(first, (last - first + 1));
}

// Loads gitignore rules from a file
std::vector<std::string> load_gitignore_rules(const fs::path &gitignore_path) {
  std::vector<std::string> rules;
  std::ifstream file(gitignore_path);
  if (!file.is_open()) {
    std::cerr << "WARNING: Could not open gitignore file: " << gitignore_path
              << '\n';
    return rules; // Return empty vector if file can't be opened
  }
  std::string line;
  while (std::getline(file, line)) {
    line = trim(line);
    if (!line.empty() && line[0] != '#') { // Ignore comments and empty lines
      rules.push_back(line);
    }
  }
  return rules;
}

// Simple gitignore-style matching (improved wildcard and directory matching)
bool matches_gitignore_rule(const fs::path &path, const std::string &rule) {
  std::string path_str = path.string();
  std::string rule_str = rule;

  bool negate = false;
  if (!rule_str.empty() && rule_str[0] == '!') {
    negate = true;
    rule_str.erase(0, 1); // Remove negation prefix for matching
  }

  if (rule_str.back() == '/') { // Directory rule (must match directory prefix)
    if (path_str.rfind(rule_str, 0) == 0 && fs::is_directory(path)) {
      return !negate;
    }
  } else if (rule_str.find('*') != std::string::npos) { // Wildcard matching
    std::regex regex_rule;
    std::string regex_pattern;
    for (char c : rule_str) {
      if (c == '*') {
        regex_pattern += ".*"; // Match zero or more of any character
      } else if (c == '?') {
        regex_pattern += "."; // Match any single character
      } else if (c == '.') {
        regex_pattern += "\\."; // Escape dot
      } else {
        regex_pattern += c;
      }
    }
    regex_rule =
        std::regex("^" + regex_pattern + "$"); // Anchor to start and end
    if (std::regex_match(path_str, regex_rule)) {
      return !negate;
    }
  } else { // Exact file name matching
    if (path_str == rule_str) {
      return !negate;
    }
  }
  return negate; // If no match, and it's a negate rule, return true
                 // (effectively un-ignoring)
}

bool is_path_ignored_by_gitignore(
    const fs::path &path, const std::vector<std::string> &gitignore_rules,
    const fs::path &base_path) {
  if (gitignore_rules.empty())
    return false;

  fs::path relative_path;
  try {
    relative_path = fs::relative(path, base_path);
  } catch (const std::exception &e) {
    std::cerr << "ERROR: Error getting relative path for gitignore check: "
              << path.string() << ": " << e.what() << '\n';
    return false; // Or decide how to handle errors, maybe ignore?
  }

  bool ignored = false;
  for (const auto &rule : gitignore_rules) {
    if (matches_gitignore_rule(relative_path, rule)) {
      if (rule.rfind("!", 0) == 0) { // Negation rule
        ignored = false;             // Un-ignore if negation rule matches
      } else {
        ignored = true;
      }
    }
  }
  return ignored;
}

// Checks if a file's size is within the allowed limit.
bool is_file_size_valid(const fs::path &path,
                        unsigned long long max_file_size_b) {
  try {
    return max_file_size_b == 0 || fs::file_size(path) <= max_file_size_b;
  } catch (const fs::filesystem_error &e) {
    return false;
  }
}

// Checks if a file's extension is allowed (and not excluded).
bool is_file_extension_allowed(const fs::path &path,
                               const std::vector<std::string> &extensions,
                               const std::vector<std::string> &excludedExtensions) {
  const auto ext = path.extension().string();
  if (ext.empty())
    return false;
  const std::string ext_no_dot = ext.substr(1);

  // Check for excluded extensions first
  if (std::find(excludedExtensions.begin(), excludedExtensions.end(), ext_no_dot) !=
      excludedExtensions.end()) {
    return false; // Extension is explicitly excluded
  }

  if (extensions.empty())
    return true; // No allowed extensions specified, not excluded, so allowed

  return std::find(extensions.begin(), extensions.end(), ext_no_dot) !=
         extensions.end(); // Check if extension is in allowed list
}


// Checks if a folder should be ignored.
bool should_ignore_folder(const fs::path &path, const Config &config) {
  if (!config.disableGitignore &&
      is_path_ignored_by_gitignore(path, config.gitignoreRules,
                                   config.dirPath)) {
    return true;
  }
  if (config.ignoreDotFolders && path.filename().string().front() == '.') {
    return true;
  }

  fs::path relativePath;
  try {
    relativePath = fs::relative(path, config.dirPath);
  } catch (const std::exception &e) {
    std::cerr << "ERROR: Error getting relative path for " << path.string()
              << ": " << e.what() << '\n';
    return false;
  }

  for (const auto &ignoredFolder : config.ignoredFolders) {
    if (relativePath.string().find(ignoredFolder.string()) == 0) {
      return true;
    }
  }

  return false;
}

// Checks if a file should be ignored.
bool should_ignore_file(const fs::path &path, const Config &config) {
  if (!config.disableGitignore &&
      is_path_ignored_by_gitignore(path, config.gitignoreRules,
                                   config.dirPath)) {
    return true;
  }
  fs::path relativePath = fs::relative(path, config.dirPath);
  return std::find(config.ignoredFiles.begin(), config.ignoredFiles.end(),
                   relativePath) != config.ignoredFiles.end();
}

// Checks if a file matches any of the regex filters.
bool matches_regex_filters(const fs::path &path,
                           const std::vector<std::string> &regex_filters) {
  if (regex_filters.empty())
    return false;
  const std::string filename = path.filename().string();
  for (const auto &regexStr : regex_filters) {
    try {
      if (std::regex_search(filename, std::regex(regexStr)))
        return true;
    } catch (const std::regex_error &e) {
      std::cerr << "ERROR: Invalid regex: " << regexStr << ": " << e.what()
                << '\n';
    }
  }
  return false;
}

// Removes C++ style comments from a string.
std::string remove_cpp_comments(const std::string &code) {
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

// Formats the output for a single file.
std::string format_file_output(const fs::path &path, const Config &config,
                               const std::string &file_content) {
  std::stringstream content;
  content << (!config.disableMarkdownlintFixes ? "\n## File: " : "\n### File: ")
          << (!config.showFilenameOnly
                  ? fs::relative(path, config.dirPath).string()
                  : path.filename().string())
          << (!config.disableMarkdownlintFixes ? "\n" : "") << "\n```";
  if (path.has_extension())
    content << path.extension().string().substr(1);
  content << "\n";

  std::istringstream iss(file_content);
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
  return content.str();
}

// Processes a single file and returns its formatted content.
std::string process_single_file(const fs::path &path, const Config &config) {
  std::ifstream file(path, std::ios::binary);
  if (!file || !is_file_size_valid(path, config.maxFileSizeB)) {
    return ""; // Return empty string if file cannot be opened or size invalid.
  }

  std::string fileContent((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());

  if (config.removeComments) {
    fileContent = remove_cpp_comments(fileContent);
  }

  return format_file_output(path, config, fileContent);
}

// --- File Collection ---

// Checks if a file should be processed last.
bool is_last_file(const fs::path &absPath, const Config &config) {
  fs::path relPath = fs::relative(absPath, config.dirPath);

  if (std::find(config.lastFiles.begin(), config.lastFiles.end(), relPath) !=
          config.lastFiles.end() ||
      std::find(config.lastFiles.begin(), config.lastFiles.end(),
                absPath.filename()) != config.lastFiles.end()) {
    return true;
  }

  return std::any_of(config.lastDirs.begin(), config.lastDirs.end(),
                     [&](const auto &dirRelPath) {
                       return relPath.string().find(dirRelPath.string()) == 0;
                     });
}

// Collects files to be processed.
std::pair<std::vector<fs::path>, std::vector<fs::path>>
collect_files(const Config &config, std::atomic<bool> &should_stop) {
  std::vector<fs::path> normalFiles;
  std::vector<fs::path> lastFilesList;
  std::unordered_set<fs::path> lastFilesSet;

  auto shouldSkipDirectory = [&](const fs::path &dirPath) {
    return fs::is_directory(dirPath) && should_ignore_folder(dirPath, config);
  };

  if (config.onlyLast) {
    if (config.lastFiles.empty() && config.lastDirs.empty()) {
        std::cerr << "ERROR: --only-last option used but no files or directories were specified with --last. Nothing to process.\n";
        exit(1);
    }
    for (const auto& lastFile : config.lastFiles) {
      fs::path absPath = fs::absolute(config.dirPath / lastFile);
      if (fs::exists(absPath) && fs::is_regular_file(absPath)) {
        if (lastFilesSet.insert(absPath).second) {
          lastFilesList.push_back(absPath);
        }
      } else {
        std::cerr << "ERROR: --only-last specified file not found or not a regular file: " << absPath << '\n';
        exit(1);
      }
    }
    for (const auto& lastDir : config.lastDirs) {
      fs::path absDirPath = fs::absolute(config.dirPath / lastDir);
      if (fs::exists(absDirPath) && fs::is_directory(absDirPath)) {
        fs::recursive_directory_iterator it(absDirPath, fs::directory_options::skip_permission_denied);
        fs::recursive_directory_iterator end;
        for (; it != end && !should_stop; ++it) {
          if (fs::is_regular_file(it->path()) &&
              is_file_extension_allowed(it->path(), config.fileExtensions, config.excludedFileExtensions) && // Apply extension filter even in onlyLast mode
              !should_ignore_file(it->path(), config) && // Apply ignore file filter even in onlyLast mode
              !matches_regex_filters(it->path(), config.regexFilters)) { // Apply regex filter even in onlyLast mode
             if (lastFilesSet.insert(it->path()).second) {
                lastFilesList.push_back(it->path());
              }
          }
        }
      } else {
        std::cerr << "ERROR: --only-last specified directory not found or not a directory: " << absDirPath << '\n';
        exit(1);
      }
    }
    return {normalFiles, lastFilesList}; // normalFiles is empty when onlyLast is true
  }


  try {
    auto options = fs::directory_options::skip_permission_denied;

    if (config.recursiveSearch) {
      // Use recursive_directory_iterator
      fs::recursive_directory_iterator it(config.dirPath, options);
      fs::recursive_directory_iterator
          end; // Default-constructed iterator as end marker.
      for (; it != end && !should_stop; ++it) { // Loop control with should_stop
        if (shouldSkipDirectory(it->path())) {
          it.disable_recursion_pending();
          continue;
        }
        if (fs::is_regular_file(it->path()) &&
            is_file_extension_allowed(it->path(), config.fileExtensions, config.excludedFileExtensions) &&
            !should_ignore_file(it->path(), config) &&
            !matches_regex_filters(it->path(), config.regexFilters)) {
          if (is_last_file(it->path(), config)) {
            if (lastFilesSet.insert(it->path()).second) {
              lastFilesList.push_back(it->path());
            }
          } else {
            normalFiles.push_back(it->path());
          }
        }
      }
    } else {
      // Use directory_iterator
      for (fs::directory_iterator it(config.dirPath, options), end;
           it != end && !should_stop; ++it) { // Loop control with should_stop
        if (shouldSkipDirectory(it->path())) {
          // folders in non-recursive mode
          continue;
        }
        if (fs::is_regular_file(it->path()) &&
            is_file_extension_allowed(it->path(), config.fileExtensions, config.excludedFileExtensions) &&
            !should_ignore_file(it->path(), config) &&
            !matches_regex_filters(it->path(), config.regexFilters)) {
          if (is_last_file(it->path(), config)) {
            if (lastFilesSet.insert(it->path()).second)
              lastFilesList.push_back(it->path());
          } else {
            normalFiles.push_back(it->path());
          }
        }
      }
    }

  } catch (const fs::filesystem_error &e) {
    std::cerr << "ERROR: Error scanning directory: " << e.what() << '\n';
    return {normalFiles, lastFilesList}; // Return what we have, even on error.
  }

  std::sort(normalFiles.begin(), normalFiles.end());
  return {normalFiles, lastFilesList};
}

// --- File Processing ---

// Processes a chunk of files.
void process_file_chunk(std::span<const fs::path> chunk, const Config &config,
                        std::vector<std::pair<fs::path, std::string>> &results,
                        std::atomic<size_t> &processed_files,
                        std::atomic<size_t> &total_bytes,
                        std::mutex &console_mutex,
                        std::mutex &ordered_results_mutex,
                        std::atomic<bool> &should_stop) {
  for (const auto &path : chunk) {
    if (should_stop)
      break;
    try {
      std::string file_content = process_single_file(path, config);
      if (!file_content.empty()) {
        total_bytes += fs::file_size(path);

        if (!config.unorderedOutput) {
          std::lock_guard<std::mutex> lock(ordered_results_mutex);
          results.emplace_back(path, file_content);
        } else {
          std::lock_guard<std::mutex> lock(console_mutex);
          std::cout << file_content;
        }
      }
      ++processed_files;
    } catch (const std::exception &e) {
      std::cerr << "ERROR: Error processing " << path.string() << ": "
                << e.what() << '\n';
    }
  }
}

// Processes files that should be processed last.
void process_last_files(const std::vector<fs::path> &last_files_list,
                        const Config &config, std::atomic<bool> &should_stop,
                        std::mutex &console_mutex) {
  // Determine the order for processing last files and dirs.
  auto get_sort_position = [&](const fs::path &relPath) -> int {
    auto exactIt =
        std::find(config.lastFiles.begin(), config.lastFiles.end(), relPath);
    if (exactIt != config.lastFiles.end()) {
      return config.lastDirs.size() +
             std::distance(config.lastFiles.begin(), exactIt);
    }

    auto filenameIt = std::find(config.lastFiles.begin(),
                                config.lastFiles.end(), relPath.filename());
    if (filenameIt != config.lastFiles.end()) {
      return config.lastDirs.size() +
             std::distance(config.lastFiles.begin(), filenameIt);
    }

    for (size_t i = 0; i < config.lastDirs.size(); ++i) {
      if (relPath.string().find(config.lastDirs[i].string()) == 0) {
        return i;
      }
    }
    return -1;
  };

  // Create a sorted copy for processing.
  std::vector<fs::path> sorted_last_files = last_files_list;
  std::sort(sorted_last_files.begin(), sorted_last_files.end(),
            [&](const fs::path &a, const fs::path &b) {
              fs::path relA = fs::relative(a, config.dirPath);
              fs::path relB = fs::relative(b, config.dirPath);
              return get_sort_position(relA) < get_sort_position(relB);
            });

  for (const auto &file : sorted_last_files) {
    if (should_stop)
      break;
    std::string file_content = process_single_file(file, config);
    if (!file_content.empty()) {
      std::lock_guard<std::mutex> lock(
          console_mutex); // Lock for console output.
      std::cout << file_content;
    }
  }
}

// --- Main Processing Function ---

// Processes all files in the specified directory.
bool process_directory(Config config, std::atomic<bool> &should_stop) {
  if (!fs::exists(config.dirPath) || !fs::is_directory(config.dirPath)) {
    std::cerr << "ERROR: Invalid directory path: " << config.dirPath.string()
              << '\n';
    return false;
  }

  if (!config.disableGitignore) {
    if (config.gitignorePath.empty()) {
      config.gitignorePath = config.dirPath / ".gitignore";
    }
    config.gitignoreRules = load_gitignore_rules(config.gitignorePath);
    if (!config.disableGitignore && config.gitignoreRules.empty() &&
        !fs::exists(config.gitignorePath)) {
      std::cerr
          << "WARNING: Gitignore option used but no gitignore file found at: "
          << config.gitignorePath << ". Ignoring gitignore.\n";
      config.disableGitignore = true; // Disable gitignore if file not found and
                                      // option used.
    }
  }

  auto [normalFiles, lastFilesList] = collect_files(config, should_stop);

  if (normalFiles.empty() && lastFilesList.empty() ) {
    if (!config.onlyLast) {
        std::cout << "No matching files found in: " << config.dirPath.string() << "\n";
    }
    return true;
  }

  std::vector<std::pair<fs::path, std::string>> orderedResults;
  if (!config.unorderedOutput) {
    orderedResults.reserve(normalFiles.size());
  }

  if (!config.disableMarkdownlintFixes) {
    std::cout << "#\n";
  }

  std::atomic<size_t> processedFiles{0};
  std::atomic<size_t> totalBytes{0};
  std::mutex consoleMutex;
  std::mutex orderedResultsMutex;

  unsigned int threadCount =
      std::min(std::thread::hardware_concurrency(), 8u); // Determine threads.
  std::vector<std::thread> threads;
  const size_t filesPerThread =
      (normalFiles.size() + threadCount - 1) / threadCount;

  for (size_t i = 0; i < threadCount; ++i) {
    const size_t start = i * filesPerThread;
    const size_t end = std::min((i + 1) * filesPerThread, normalFiles.size());
    if (start >= end) {
      break;
    }

    threads.emplace_back([&, start, end] {
      try {
        process_file_chunk(
            std::span{normalFiles.begin() + start, normalFiles.begin() + end},
            config, orderedResults, processedFiles, totalBytes, consoleMutex,
            orderedResultsMutex, should_stop);
      } catch (const std::exception &e) {
        std::cerr << "ERROR: Exception in thread: " << e.what() << '\n';
      }
    });
  }

  for (auto &thread : threads) {
    thread.join();
  }

  //  Output ordered results.
  if (!config.unorderedOutput) {
    std::lock_guard<std::mutex> lock(orderedResultsMutex);
    std::sort(
        orderedResults.begin(), orderedResults.end(),
        [&normalFiles](const auto &a, const auto &b) {
          return std::find(normalFiles.begin(), normalFiles.end(), a.first) <
                 std::find(normalFiles.begin(), normalFiles.end(), b.first);
        });
    for (const auto &result : orderedResults) {
      std::cout << result.second; // Directly output here.
    }
  }

  process_last_files(lastFilesList, config, should_stop, consoleMutex);
  return true;
}

// --- Signal Handling ---

std::atomic<bool> *globalShouldStop = nullptr; // Pointer for signal handling.
void signalHandler(int signum) {
  if (globalShouldStop) {
    std::cout << "\nInterrupt received, stopping...\n";
    *globalShouldStop = true;
  }
}

// --- Main Function ---

// Parses command-line arguments and sets up the configuration.
Config parse_arguments(int argc, char *argv[]) {
  Config config;

  config.disableMarkdownlintFixes = false; // default is markdownlint fixes enabled
  config.showFilenameOnly = false;        // default is relative path shown
  config.disableGitignore = false;        // default is gitignore enabled
  config.unorderedOutput = false;         // default is ordered output
  config.onlyLast = false;                // default is not only-last mode

  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <directory_path> [options]\n"
              << "Options:\n"
              << "  -m, --max-size <bytes>  Maximum file size in bytes (default: no "
                 "limit)\n"
              << "  -n, --no-recursive     Disable recursive search\n"
              << "  -e, --ext <ext>        Process only files with given extension "
                 "(can be used multiple times, grouped)\n"
              << "  -x, --exclude-ext <ext> Exclude files with given extension "
                 "(can be used multiple times, grouped)\n" // New option
              << "  -d, --dot-folders      Include folders starting with a dot\n"
              << "  -i, --ignore <item>    Ignore specific folder or file (can be "
                 "used multiple times, grouped)\n"
              << "  -r, --regex <pattern>  Exclude files matching the regex pattern "
                 "(can be used multiple times, grouped)\n"
              << "  -c, --remove-comments  Remove C++ style comments (// and /* */) "
                 "from code\n"
              << "  -l, --remove-empty-lines Remove empty lines from output\n"
              << "  -f, --filename-only      Show only filename in file headers\n"
              << "  -u, --unordered          Output files in unordered they were "
                 "found\n"
              << "  -z, --last <item>      Process specified file, directory, or "
                 "filename last (order of multiple -z options is preserved).\n"
              << "  -Z, --only-last         Only process specified files and directories from --last options, ignoring all other files.\n"
              << "  -w, --no-markdownlint-fixes Disable fixes for Markdown linting\n"
              << "  -t, --no-gitignore         Disable gitignore rules\n"
              << "  -g, --gitignore <path>     Use gitignore rules from a specific path.\n";

    exit(1); // Exit immediately on usage error.
  }

  config.dirPath = argv[1];

  for (int i = 2; i < argc; ++i) {
    std::string_view arg = argv[i];
    if ((arg == "-m" || arg == "--max-size") && i + 1 < argc) {
      try {
        config.maxFileSizeB = std::stoll(argv[++i]);
      } catch (const std::invalid_argument &e) {
        std::cerr << "ERROR: Invalid max-size value: " << argv[i]
                  << ". Must be an integer.\n";
        exit(1);
      }
    } else if (arg == "-n" || arg == "--no-recursive") {
      config.recursiveSearch = false;
    } else if (arg == "-e" || arg == "--ext") {
      while (i + 1 < argc && argv[i + 1][0] != '-') {
        std::string ext = argv[++i];
        if (!ext.empty() && ext[0] == '.') {
          ext.erase(0, 1); // Remove leading dot if present
        }
        config.fileExtensions.emplace_back(ext);
      }
    } else if (arg == "-x" || arg == "--exclude-ext") { // New option handling
      while (i + 1 < argc && argv[i + 1][0] != '-') {
        std::string ext = argv[++i];
        if (!ext.empty() && ext[0] == '.') {
          ext.erase(0, 1); // Remove leading dot if present
        }
        config.excludedFileExtensions.emplace_back(ext);
      }
    } else if (arg == "-d" || arg == "--dot-folders") {
      config.ignoreDotFolders = false;
    } else if (arg == "-i" || arg == "--ignore") {
      while (i + 1 < argc && argv[i + 1][0] != '-') {
        std::string entry = argv[++i];
        fs::path absoluteEntry = fs::absolute(config.dirPath / entry);
        if (fs::is_directory(absoluteEntry)) {
          try {
            fs::path relativeEntry =
                fs::relative(absoluteEntry, config.dirPath);
            config.ignoredFolders.emplace_back(relativeEntry);
          } catch (const std::exception &e) {
            std::cerr << "ERROR: Invalid ignore path: " << absoluteEntry
                      << " is not under " << config.dirPath << '\n';
            exit(1);
          }
        } else {
          config.ignoredFiles.emplace_back(entry);
        }
      }
    } else if (arg == "-r" || arg == "--regex") {
      while (i + 1 < argc && argv[i + 1][0] != '-')
        config.regexFilters.emplace_back(argv[++i]);
    } else if (arg == "-c" || arg == "--remove-comments") {
      config.removeComments = true;
    } else if (arg == "-l" || arg == "--remove-empty-lines") {
      config.removeEmptyLines = true;
    } else if (arg == "-f" || arg == "--filename-only") {
      config.showFilenameOnly = true;
    } else if (arg == "-u" || arg == "--unordered") {
      config.unorderedOutput = true;
    } else if (arg == "-z" || arg == "--last") {
      while (i + 1 < argc && argv[i + 1][0] != '-') {
        std::string entry = argv[++i];
        fs::path absoluteEntry =
            fs::absolute(config.dirPath / entry); // Use absolute path first
        fs::path relativeEntry;
        try {
          relativeEntry =
              fs::relative(absoluteEntry, config.dirPath); // Then make relative
        } catch (const std::exception &e) {
          std::cerr << "ERROR: Invalid last path: " << absoluteEntry
                    << " is not under " << config.dirPath << '\n';
          exit(1);
        }

        if (fs::is_directory(absoluteEntry)) {
          config.lastDirs.emplace_back(relativeEntry);
        } else {
          config.lastFiles.emplace_back(relativeEntry);
        }
      }
    } else if (arg == "-Z" || arg == "--only-last") {
        config.onlyLast = true;
    } else if (arg == "-w" || arg == "--no-markdownlint-fixes") {
      config.disableMarkdownlintFixes = true;
    } else if (arg == "-t" || arg == "--no-gitignore") {
      config.disableGitignore = true;
    } else if (arg == "-g" || arg == "--gitignore") {
      config.disableGitignore = false; // Explicitly enable gitignore
      if (i + 1 >= argc || argv[i + 1][0] == '-') {
        std::cerr << "ERROR: --gitignore option requires a path to the gitignore file.\n";
        exit(1);
      }
      config.gitignorePath = argv[++i];
      if (!fs::exists(config.gitignorePath)) {
        std::cerr << "WARNING: Gitignore file not found at: "
                  << config.gitignorePath << ". Using gitignore might not work as expected.\n";
      }
    } else {
      std::cerr << "Invalid option: " << arg << "\n";
      exit(1); // Exit on invalid options
    }
  }
  return config;
}
