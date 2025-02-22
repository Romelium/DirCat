#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <regex>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
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
  std::vector<std::string> excludedFileExtensions;
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
  bool onlyLast = false;
  fs::path outputFile;
  bool showLineNumbers = false;
  bool dryRun = false; // ADDED: Dry run mode
};

// --- Utility Functions ---

std::string trim(std::string_view str) {
  constexpr std::string_view whitespace = " \t\n\r\f\v";
  size_t start = str.find_first_not_of(whitespace);
  if (start == std::string_view::npos)
    return "";
  size_t end = str.find_last_not_of(whitespace);
  return std::string(str.substr(start, end - start + 1));
}

std::string normalize_path(const fs::path &path) {
  std::string path_str = path.string();
  std::replace(path_str.begin(), path_str.end(), '\\', '/');
  return path_str;
}

std::vector<std::string> load_gitignore_rules(const fs::path &gitignore_path) {
  std::vector<std::string> rules;
  std::ifstream file(gitignore_path);
  if (!file.is_open()) {
    std::cerr << "ERROR: Could not open gitignore file: " << normalize_path(gitignore_path) << '\n'; // Granular error reporting: path
    return rules;
  }
  std::string line;
  while (std::getline(file, line)) {
    line = trim(line);
    if (!line.empty() && line[0] != '#') {
      rules.push_back(line);
    }
  }
  return rules;
}

bool matches_gitignore_rule(const fs::path &path, const std::string &rule) {
  std::string path_str = path.string();
  std::string rule_str = rule;

  std::replace(path_str.begin(), path_str.end(), '\\', '/');
  std::replace(rule_str.begin(), rule_str.end(), '\\', '/');

  bool negate = false;
  if (!rule_str.empty() && rule_str[0] == '!') {
    negate = true;
    rule_str.erase(0, 1);
  }

  if (rule_str.back() == '/') {
    std::string rule_prefix = rule_str.substr(0, rule_str.length() - 1);
    if (path_str.rfind(rule_prefix, 0) == 0)
      return true;
  } else if (rule_str.find('*') != std::string::npos) {
    std::regex regex_rule;
    std::string regex_pattern;
    for (char c : rule_str) {
      if (c == '*')
        regex_pattern += ".*";
      else if (c == '?')
        regex_pattern += ".";
      else if (c == '.')
        regex_pattern += "\\.";
      else
        regex_pattern += c;
    }
    regex_rule = std::regex("^" + regex_pattern + "$", std::regex::icase);
    if (std::regex_match(path_str, regex_rule))
      return true;
  } else {
    if (path.filename().string() == rule_str)
      return true;
  }
  return false;
}

bool is_path_ignored_by_gitignore(const fs::path &path,
                                  const fs::path &base_path) {
  std::vector<std::string> accumulated_rules;
  fs::path current_path = path.parent_path();
  fs::path current_base = base_path;

  while (current_path != current_base.parent_path() &&
         current_path.has_relative_path()) {
    fs::path gitignore_path = current_path / ".gitignore";
    if (fs::exists(gitignore_path) && fs::is_regular_file(gitignore_path)) {
      std::vector<std::string> rules = load_gitignore_rules(gitignore_path);
      accumulated_rules.insert(accumulated_rules.begin(), rules.begin(),
                               rules.end());
    }
    if (current_path == current_base)
      break;
    current_path = current_path.parent_path();
  }

  if (accumulated_rules.empty())
    return false;

  fs::path relative_path;
  try {
    relative_path = fs::relative(path, base_path);
  } catch (const std::exception &e) {
    std::cerr << "ERROR: Error getting relative path for gitignore check: "
              << normalize_path(path) << ": " << e.what() << '\n'; // Granular error reporting: path
    return false;
  }

  bool ignored = false;
  for (const auto &rule : accumulated_rules) {
    if (matches_gitignore_rule(relative_path, rule)) {
      if (rule.rfind("!", 0) == 0)
        ignored = false;
      else
        ignored = true;
    }
  }
  return ignored;
}

bool is_file_size_valid(const fs::path &path,
                        unsigned long long max_file_size_b) {
  try {
    return max_file_size_b == 0 || fs::file_size(path) <= max_file_size_b;
  } catch (const fs::filesystem_error &e) {
    std::cerr << "ERROR: Could not get file size for: " << normalize_path(path) << ": " << e.what() << '\n'; // Granular error reporting: path
    return false;
  }
}

bool is_file_extension_allowed(
    const fs::path &path, const std::vector<std::string> &extensions,
    const std::vector<std::string> &excludedExtensions) {
  const auto ext = path.extension().string();
  if (ext.empty())
    return false;
  const std::string ext_no_dot = ext.substr(1);

  if (std::find(excludedExtensions.begin(), excludedExtensions.end(),
                ext_no_dot) != excludedExtensions.end()) {
    return false;
  }

  if (extensions.empty())
    return true;

  return std::find(extensions.begin(), extensions.end(), ext_no_dot) !=
         extensions.end();
}

bool should_ignore_folder(const fs::path &path, bool disableGitignore,
                          const fs::path &dirPath, bool ignoreDotFolders,
                          const std::vector<fs::path> &ignoredFolders) {
  if (!disableGitignore && is_path_ignored_by_gitignore(path, dirPath))
    return true;
  if (ignoreDotFolders && path.filename().string().front() == '.')
    return true;

  fs::path relativePath;
  try {
    relativePath = fs::relative(path, dirPath);
  } catch (const std::exception &e) {
    std::cerr << "ERROR: Error getting relative path for " << normalize_path(path) // Granular error reporting: path
              << ": " << e.what() << '\n';
    return false;
  }

  for (const auto &ignoredFolder : ignoredFolders) {
    if (relativePath.string().find(ignoredFolder.string()) == 0)
      return true;
  }

  return false;
}

bool should_ignore_file(const fs::path &path, bool disableGitignore,
                        const fs::path &dirPath,
                        unsigned long long maxFileSizeB,
                        const std::vector<fs::path> &ignoredFiles) {
  if (!disableGitignore && is_path_ignored_by_gitignore(path, dirPath))
    return true;
  if (!is_file_size_valid(path, maxFileSizeB))
    return true;

  fs::path relativePath = fs::relative(path, dirPath);
  return std::find(ignoredFiles.begin(), ignoredFiles.end(), relativePath) !=
         ignoredFiles.end();
}

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
      std::cerr << "ERROR: Invalid regex: " << regexStr << ": " << e.what() // Already has regex string
                << '\n';
    }
  }
  return false;
}

std::string remove_cpp_comments(const std::string &code) {
  std::string result;
  bool inString = false;
  bool inChar = false;
  bool inSingleLineComment = false;
  bool inMultiLineComment = false;

  for (size_t i = 0; i < code.size(); ++i) {
    if (inString) {
      result += code[i];
      if (code[i] == '\\' && i + 1 < code.size())
        result += code[++i];
      else if (code[i] == '"')
        inString = false;
    } else if (inChar) {
      result += code[i];
      if (code[i] == '\\' && i + 1 < code.size())
        result += code[++i];
      else if (code[i] == '\'')
        inChar = false;
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

std::string format_file_output(const fs::path &path,
                               bool disableMarkdownlintFixes,
                               bool showFilenameOnly, const fs::path &dirPath,
                               const std::string &file_content,
                               bool removeEmptyLines, bool showLineNumbers) {
  std::stringstream content;
  content << (!disableMarkdownlintFixes ? "\n## File: " : "\n### File: ")
          << (!showFilenameOnly ? fs::relative(path, dirPath).string()
                                : path.filename().string())
          << (!disableMarkdownlintFixes ? "\n" : "") << "\n```";
  if (path.has_extension())
    content << path.extension().string().substr(1);
  content << "\n";

  std::istringstream iss(file_content);
  std::string line;
  int lineNumber = 1;
  while (std::getline(iss, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (removeEmptyLines &&
        line.find_first_not_of(" \t\r\n") == std::string::npos)
      continue;
    if (showLineNumbers)
      content << lineNumber++ << " | ";
    content << line << '\n';
  }
  content << "\n```\n";
  return content.str();
}

std::string process_single_file(const fs::path &path,
                                unsigned long long maxFileSizeB,
                                bool removeComments,
                                bool disableMarkdownlintFixes,
                                bool showFilenameOnly, const fs::path &dirPath,
                                bool removeEmptyLines, bool showLineNumbers,
                                bool dryRun) { // ADDED: dryRun
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    std::cerr << "ERROR: Could not open file: " << normalize_path(path) << '\n'; // Granular error reporting: path
    return "";
  }
  if (!is_file_size_valid(path, maxFileSizeB))
    return "";

  if (dryRun) { // In dry-run mode, skip file content reading and processing
    return format_file_output(
        path, disableMarkdownlintFixes, showFilenameOnly, dirPath, "",
        removeEmptyLines,
        showLineNumbers); // Return formatted header even if content is empty
  }

  std::string fileContent((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());

  if (removeComments) {
    fileContent = remove_cpp_comments(fileContent);
  }

  return format_file_output(path, disableMarkdownlintFixes, showFilenameOnly,
                            dirPath, fileContent, removeEmptyLines,
                            showLineNumbers);
}

// --- File Collection ---

bool is_last_file(const fs::path &absPath, const fs::path &dirPath,
                  const std::vector<fs::path> &lastFiles,
                  const std::vector<fs::path> &lastDirs) {
  fs::path relPath = fs::relative(absPath, dirPath);

  if (std::find(lastFiles.begin(), lastFiles.end(), relPath) !=
          lastFiles.end() ||
      std::find(lastFiles.begin(), lastFiles.end(), absPath.filename()) !=
          lastFiles.end()) {
    return true;
  }

  return std::any_of(lastDirs.begin(), lastDirs.end(),
                     [&](const auto &dirRelPath) {
                       return relPath.string().find(dirRelPath.string()) == 0;
                     });
}

std::pair<std::vector<fs::path>, std::vector<fs::path>>
collect_files(const Config &config, std::atomic<bool> &should_stop) {
  std::vector<fs::path> normalFiles;
  std::vector<fs::path> lastFilesList;
  std::unordered_set<fs::path> lastFilesSet;

  auto shouldSkipDirectory = [&](const fs::path &dirPath) {
    return fs::is_directory(dirPath) &&
           should_ignore_folder(dirPath, config.disableGitignore,
                                config.dirPath, config.ignoreDotFolders,
                                config.ignoredFolders);
  };

  if (config.onlyLast) {
    if (config.lastFiles.empty() && config.lastDirs.empty()) {
      std::cerr << "ERROR: --only-last option used but no files or directories "
                   "were specified with --last. Nothing to process.\n";
      exit(1);
    }
    for (const auto &lastFile : config.lastFiles) {
      fs::path absPath = fs::absolute(config.dirPath / lastFile);
      if (fs::exists(absPath) && fs::is_regular_file(absPath)) {
        if (lastFilesSet.insert(absPath).second) {
          lastFilesList.push_back(absPath);
        }
      } else {
        std::cerr << "ERROR: --only-last specified file not found or not a file: " // Granular error reporting: file type
                  << normalize_path(absPath) << '\n'; // Granular error reporting: path
        exit(1);
      }
    }
    for (const auto &lastDir : config.lastDirs) {
      fs::path absDirPath = fs::absolute(config.dirPath / lastDir);
      if (fs::exists(absDirPath) && fs::is_directory(absDirPath)) {
        fs::recursive_directory_iterator it(
            absDirPath, fs::directory_options::skip_permission_denied);
        fs::recursive_directory_iterator end;
        for (; it != end && !should_stop; ++it) {
          if (fs::is_regular_file(it->path()) &&
              is_file_extension_allowed(it->path(), config.fileExtensions,
                                        config.excludedFileExtensions) &&
              !should_ignore_file(it->path(), config.disableGitignore,
                                  config.dirPath, config.maxFileSizeB,
                                  config.ignoredFiles) &&
              !matches_regex_filters(it->path(), config.regexFilters)) {
            if (lastFilesSet.insert(it->path()).second) {
              lastFilesList.push_back(it->path());
            }
          }
        }
      } else {
        std::cerr << "ERROR: --only-last specified directory not found or not a directory: " // Granular error reporting: file type
                  << normalize_path(absDirPath) << '\n'; // Granular error reporting: path
        exit(1);
      }
    }
    return {normalFiles, lastFilesList};
  }

  try {
    auto options = fs::directory_options::skip_permission_denied;

    if (config.recursiveSearch) {
      fs::recursive_directory_iterator it(config.dirPath, options);
      fs::recursive_directory_iterator end;
      for (; it != end && !should_stop; ++it) {
        if (shouldSkipDirectory(it->path())) {
          it.disable_recursion_pending();
          continue;
        }
        if (fs::is_regular_file(it->path()) &&
            is_file_extension_allowed(it->path(), config.fileExtensions,
                                      config.excludedFileExtensions) &&
            !should_ignore_file(it->path(), config.disableGitignore,
                                config.dirPath, config.maxFileSizeB,
                                config.ignoredFiles) &&
            !matches_regex_filters(it->path(), config.regexFilters)) {
          if (is_last_file(it->path(), config.dirPath, config.lastFiles,
                           config.lastDirs)) {
            if (lastFilesSet.insert(it->path()).second) {
              lastFilesList.push_back(it->path());
            }
          } else {
            normalFiles.push_back(it->path());
          }
        }
      }
    } else {
      for (fs::directory_iterator it(config.dirPath, options), end;
           it != end && !should_stop; ++it) {
        if (shouldSkipDirectory(it->path()))
          continue;
        if (fs::is_regular_file(it->path()) &&
            is_file_extension_allowed(it->path(), config.fileExtensions,
                                      config.excludedFileExtensions) &&
            !should_ignore_file(it->path(), config.disableGitignore,
                                config.dirPath, config.maxFileSizeB,
                                config.ignoredFiles) &&
            !matches_regex_filters(it->path(), config.regexFilters)) {
          if (is_last_file(it->path(), config.dirPath, config.lastFiles,
                           config.lastDirs)) {
            if (lastFilesSet.insert(it->path()).second)
              lastFilesList.push_back(it->path());
          } else {
            normalFiles.push_back(it->path());
          }
        }
      }
    }

  } catch (const fs::filesystem_error &e) {
    std::cerr << "ERROR: Error scanning directory: " << config.dirPath.string() << ": " << e.what() << '\n'; // Already has dir path
    return {normalFiles, lastFilesList};
  }

  std::sort(normalFiles.begin(), normalFiles.end());
  return {normalFiles, lastFilesList};
}

// --- File Processing ---

void process_file_chunk(std::span<const fs::path> chunk, bool unorderedOutput,
                        bool removeComments, unsigned long long maxFileSizeB,
                        bool disableMarkdownlintFixes, bool showFilenameOnly,
                        const fs::path &dirPath, bool removeEmptyLines,
                        std::vector<std::pair<fs::path, std::string>> &results,
                        std::atomic<size_t> &processed_files,
                        std::atomic<size_t> &total_bytes,
                        std::mutex &console_mutex,
                        std::mutex &ordered_results_mutex,
                        std::atomic<bool> &should_stop,
                        std::ostream &output_stream, bool showLineNumbers,
                        bool dryRun) { // ADDED: dryRun
  for (const auto &path : chunk) {
    if (should_stop)
      break;
    try {
      std::string file_content = process_single_file(
          path, maxFileSizeB, removeComments, disableMarkdownlintFixes,
          showFilenameOnly, dirPath, removeEmptyLines, showLineNumbers,
          dryRun); // ADDED: dryRun
      if (!file_content.empty()) {
        total_bytes += fs::file_size(path);

        if (!unorderedOutput) {
          std::lock_guard<std::mutex> lock(ordered_results_mutex);
          results.emplace_back(path, file_content);
        } else {
          if (!dryRun) { // Only output content if not dry run
            std::lock_guard<std::mutex> lock(console_mutex);
            output_stream << file_content;
          } else {
            std::lock_guard<std::mutex> lock(console_mutex);
            output_stream << normalize_path(path) // Normalized path for dry-run
                          << "\n";
          }
        }
      }
      ++processed_files;
    } catch (const std::exception &e) {
      std::cerr << "ERROR: Error processing " << normalize_path(path) << ": " // Granular error reporting: path
                << e.what() << '\n';
    }
  }
}

void process_last_files(const std::vector<fs::path> &last_files_list,
                        const Config &config, std::atomic<bool> &should_stop,
                        std::mutex &console_mutex, std::ostream &output_stream,
                        bool dryRun) { // ADDED: dryRun
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
    std::string file_content = process_single_file(
        file, config.maxFileSizeB, config.removeComments,
        config.disableMarkdownlintFixes, config.showFilenameOnly,
        config.dirPath, config.removeEmptyLines, config.showLineNumbers,
        dryRun); // ADDED: dryRun
    if (!file_content.empty()) {
      if (!dryRun) { // Only output content if not dry run
        std::lock_guard<std::mutex> lock(console_mutex);
        output_stream << file_content;
      } else {
        std::lock_guard<std::mutex> lock(console_mutex);
        output_stream << normalize_path(file)
                      << "\n"; // Normalized path for dry-run
      }
    }
  }
}

// --- Main Processing Function ---

bool process_file(const fs::path &path, const Config &config,
                  std::ostream &output_stream) {
  try {
    std::string file_content = process_single_file(
        path, config.maxFileSizeB, config.removeComments,
        config.disableMarkdownlintFixes, true, config.dirPath,
        config.removeEmptyLines, config.showLineNumbers,
        config.dryRun); // ADDED: dryRun
    if (!config.dryRun &&
        !file_content.empty()) { // Only output content if not dry run
      output_stream << file_content;
    } else if (config.dryRun &&
               !file_content.empty()) { // if dryRun and file should be
                                        // processed, print file path
      output_stream << normalize_path(path)
                    << "\n"; // Normalized path for dry-run
    }
  } catch (const std::exception &e) {
    std::cerr << "ERROR: Processing Single File: " << normalize_path(path) << ": " << e.what() << '\n'; // Granular error reporting: path
    return false;
  }
  return true;
}

bool process_directory(Config config, std::atomic<bool> &should_stop) {
  if (!fs::exists(config.dirPath) || !fs::is_directory(config.dirPath)) {
    std::cerr << "ERROR: Invalid directory path: " << config.dirPath.string() // Already has dir path
              << '\n';
    return false;
  }

  if (!config.disableGitignore) {
    if (config.gitignorePath.empty()) {
      config.gitignorePath = config.dirPath / ".gitignore";
    }
    if (!config.disableGitignore && !fs::exists(config.gitignorePath)) {
      std::cerr
          << "WARNING: Gitignore option used but no gitignore file found at: "
          << normalize_path(config.gitignorePath) << ". Ignoring gitignore.\n"; // Granular error reporting: path
      config.disableGitignore = true;
    }
  }

  auto [normalFiles, lastFilesList] = collect_files(config, should_stop);

  if (config.dryRun) { // Dry run mode: print file paths and exit
    std::cout << "Files to be processed:\n";
    for (const auto &file : normalFiles) {
      std::cout << normalize_path(file) << "\n"; // Normalized path for dry-run
    }
    for (const auto &file : lastFilesList) {
      std::cout << normalize_path(file) << "\n"; // Normalized path for dry-run
    }
    return true;
  }

  if (normalFiles.empty() && lastFilesList.empty()) {
    if (!config.onlyLast) {
      std::cout << "No matching files found in: " << config.dirPath.string() // Already has dir path
                << "\n";
    }
    return true;
  }

  std::vector<std::pair<fs::path, std::string>> orderedResults;
  if (!config.unorderedOutput) {
    orderedResults.reserve(normalFiles.size());
  }

  std::ofstream outputFileStream;
  std::ostream *outputPtr = &std::cout;
  if (!config.outputFile.empty()) {
    outputFileStream.open(config.outputFile);
    if (!outputFileStream.is_open()) {
      std::cerr << "ERROR: Could not open output file: " << normalize_path(config.outputFile) // Granular error reporting: path
                << '\n';
      return false;
    }
    outputPtr = &outputFileStream;
  }
  std::ostream &output_stream = *outputPtr;

  if (!config.disableMarkdownlintFixes) {
    output_stream << "#\n";
  }

  std::atomic<size_t> processedFiles{0};
  std::atomic<size_t> totalBytes{0};
  std::mutex consoleMutex;
  std::mutex orderedResultsMutex;

  unsigned int threadCount = std::min(std::thread::hardware_concurrency(), 8u);
  std::vector<std::thread> threads;
  const size_t filesPerThread =
      (normalFiles.size() + threadCount - 1) / threadCount;

  for (size_t i = 0; i < threadCount; ++i) {
    const size_t start = i * filesPerThread;
    const size_t end = std::min((i + 1) * filesPerThread, normalFiles.size());
    if (start >= end)
      break;

    threads.emplace_back([&, start, end] {
      try {
        process_file_chunk(
            std::span{normalFiles.begin() + start, normalFiles.begin() + end},
            config.unorderedOutput, config.removeComments, config.maxFileSizeB,
            config.disableMarkdownlintFixes, config.showFilenameOnly,
            config.dirPath, config.removeEmptyLines, orderedResults,
            processedFiles, totalBytes, consoleMutex, orderedResultsMutex,
            should_stop, output_stream, config.showLineNumbers,
            config.dryRun); // ADDED: dryRun
      } catch (const std::exception &e) {
        std::cerr << "ERROR: Exception in thread: " << e.what() << '\n'; // Generic thread error, no path context
      }
    });
  }

  for (auto &thread : threads) {
    thread.join();
  }

  if (!config.unorderedOutput) {
    std::lock_guard<std::mutex> lock(orderedResultsMutex);
    std::sort(
        orderedResults.begin(), orderedResults.end(),
        [&normalFiles](const auto &a, const auto &b) {
          return std::find(normalFiles.begin(), normalFiles.end(), a.first) <
                 std::find(normalFiles.begin(), normalFiles.end(), b.first);
        });
    for (const auto &result : orderedResults) {
      output_stream << result.second;
    }
  }

  process_last_files(lastFilesList, config, should_stop, consoleMutex,
                     output_stream, config.dryRun); // ADDED: dryRun

  if (outputFileStream.is_open()) {
    outputFileStream.close();
  }

  return true;
}

// --- Signal Handling ---

std::atomic<bool> *globalShouldStop = nullptr;
void signalHandler(int signum) {
  if (globalShouldStop) {
    std::cout << "\nInterrupt received, stopping...\n";
    *globalShouldStop = true;
  }
}

// --- Argument Parsing ---

Config parse_arguments(int argc, char *argv[]) {
  Config config;

  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <directory_path> [options]\n";
    std::cerr << "Options:\n";

    std::vector<std::pair<std::string, std::string>> options = {
        {"-m, --max-size <bytes>",
         "Maximum file size in bytes (default: no limit)"},
        {"-n, --no-recursive", "Disable recursive search"},
        {"-e, --ext <ext>", "Process only files with given extension (can be "
                            "used multiple times, grouped)"},
        {"-x, --exclude-ext <ext>", "Exclude files with given extension (can "
                                    "be used multiple times, grouped)"},
        {"-d, --dot-folders", "Include folders starting with a dot"},
        {"-i, --ignore <item>", "Ignore specific folder or file (can be used "
                                "multiple times, grouped)"},
        {"-r, --regex <pattern>", "Exclude files matching the regex pattern "
                                  "(can be used multiple times, grouped)"},
        {"-c, --remove-comments",
         "Remove C++ style comments (// and /* */) from code"},
        {"-l, --remove-empty-lines", "Remove empty lines from output"},
        {"-f, --filename-only", "Show only filename in file headers"},
        {"-u, --unordered", "Output files in unordered they were found"},
        {"-z, --last <item>",
         "Process specified file, directory, or filename last (order of "
         "multiple -z options is preserved)."},
        {"-Z, --only-last", "Only process specified files and directories from "
                            "--last options, ignoring all other files."},
        {"-w, --no-markdownlint-fixes", "Disable fixes for Markdown linting"},
        {"-t, --no-gitignore", "Disable gitignore rules"},
        {"-g, --gitignore <path>", "Use gitignore rules from a specific path."},
        {"-o, --output <file>",
         "Output to the specified file instead of stdout."},
        {"-L, --line-numbers", "Show line numbers in output."},
        {"-D, --dry-run", "Dry-run: list files to be processed without "
                          "concatenating them."}, // ADDED: Dry run option
    };

    size_t max_option_length = 0;
    for (const auto &option : options) {
      if (option.first.length() > max_option_length) {
        max_option_length = option.first.length();
      }
    }

    for (const auto &option : options) {
      std::cerr << "  " << std::left << std::setw(max_option_length)
                << option.first << "  " << option.second << "\n";
    }

    exit(1);
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
          ext.erase(0, 1);
        }
        config.fileExtensions.emplace_back(ext);
      }
    } else if (arg == "-x" || arg == "--exclude-ext") {
      while (i + 1 < argc && argv[i + 1][0] != '-') {
        std::string ext = argv[++i];
        if (!ext.empty() && ext[0] == '.') {
          ext.erase(0, 1);
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
            std::cerr << "ERROR: Invalid ignore path: " << normalize_path(absoluteEntry) // Granular error reporting: path
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
        fs::path absoluteEntry = fs::absolute(config.dirPath / entry);
        fs::path relativeEntry;
        try {
          relativeEntry = fs::relative(absoluteEntry, config.dirPath);
        } catch (const std::exception &e) {
          std::cerr << "ERROR: Invalid last path: " << normalize_path(absoluteEntry) // Granular error reporting: path
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
      config.disableGitignore = false;
      if (i + 1 >= argc || argv[i + 1][0] == '-') {
        std::cerr << "ERROR: --gitignore option requires a path to the "
                     "gitignore file.\n";
        exit(1);
      }
      config.gitignorePath = argv[++i];
      if (!fs::exists(config.gitignorePath)) {
        std::cerr << "WARNING: Gitignore file not found at: "
                  << normalize_path(config.gitignorePath) // Granular error reporting: path
                  << ". Using gitignore might not work as expected.\n";
      }
    } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
      config.outputFile = argv[++i];
    } else if (arg == "-L" || arg == "--line-numbers") {
      config.showLineNumbers = true;
    } else if (arg == "-D" || arg == "--dry-run") { // ADDED: Dry run option
      config.dryRun = true;
    } else {
      std::cerr << "Invalid option: " << arg << "\n";
      exit(1);
    }
  }
  return config;
}
