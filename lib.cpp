#include <algorithm>
#include <atomic>
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

// --- Configuration ---
struct Config {
  fs::path dirPath;
  unsigned long long maxFileSizeB = 0;
  bool recursiveSearch = true;
  std::vector<std::string> fileExtensions;
  bool ignoreDotFolders = true;
  std::vector<fs::path> ignoredFolders;
  std::vector<fs::path> ignoredFiles;
  std::vector<std::string> regexFilters;
  bool removeComments = false;
  bool removeEmptyLines = false;
  bool showRelativePath = false;
  bool ordered = false;
  std::vector<fs::path> lastFiles;
  std::vector<fs::path> lastDirs;
  bool markdownlintFixes = false;
};

// --- Utility Functions ---

// Checks if a file's size is within the allowed limit.
bool is_file_size_valid(const fs::path &path,
                        unsigned long long max_file_size_b) {
  try {
    return max_file_size_b == 0 || fs::file_size(path) <= max_file_size_b;
  } catch (const fs::filesystem_error &) {
    return false;
  }
}

// Checks if a file's extension is allowed.
bool is_file_extension_allowed(const fs::path &path,
                               const std::vector<std::string> &extensions) {
  if (extensions.empty())
    return true;
  const auto ext = path.extension().string();
  if (ext.empty())
    return false;
  return std::find(extensions.begin(), extensions.end(), ext.substr(1)) !=
         extensions.end();
}

// Checks if a folder should be ignored.
bool should_ignore_folder(const fs::path &path, const Config &config) {
  if (config.ignoreDotFolders && path.filename().string().front() == '.') {
    return true;
  }

  fs::path relativePath;
  try {
    relativePath = fs::relative(path, config.dirPath);
  } catch (const std::exception &e) {
    //  log_error("Error getting relative path for " + path.string() + ": " +
    //  e.what());  // Use consistent logging.
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
  content << (config.markdownlintFixes ? "\n## File: " : "\n### File: ")
          << (config.showRelativePath
                  ? fs::relative(path, config.dirPath).string()
                  : path.filename().string())
          << (config.markdownlintFixes ? "\n" : "") << "\n```";
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

  try {
    auto options = fs::directory_options::skip_permission_denied;

    if (config.recursiveSearch) {
      // Use recursive_directory_iterator
      fs::recursive_directory_iterator it(config.dirPath, options);
      fs::recursive_directory_iterator end; // Default-constructed iterator as end marker.
      while (it != end) {
        if (should_stop) {
          return {normalFiles, lastFilesList};
        }
        if (shouldSkipDirectory(it->path())) {
          it.disable_recursion_pending();
          ++it; // Increment here, after skip.
          continue;
        }
        if (fs::is_regular_file(it->path()) &&
            is_file_extension_allowed(it->path(), config.fileExtensions) &&
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
        ++it; // Increment *after* processing the current entry.
      }
    } else {
      // Use directory_iterator
      for (fs::directory_iterator it(config.dirPath, options), end; it != end; ++it) {
          if (should_stop) {
            return { normalFiles, lastFilesList };
          }

            if (fs::is_regular_file(it->path()) &&
              is_file_extension_allowed(it->path(), config.fileExtensions) &&
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
    return {normalFiles,
            lastFilesList}; // Return what we have, even on error.
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

        if (config.ordered) {
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
  // Determine the order for processing last files and dirs.  This function is
  // now simpler.
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

  auto [normalFiles, lastFilesList] = collect_files(config, should_stop);

  if (normalFiles.empty() && config.lastFiles.empty() &&
      config.lastDirs.empty()) {
    std::cout << "No matching files found in: " << config.dirPath.string()
              << "\n";
    return true;
  }

  std::vector<std::pair<fs::path, std::string>> orderedResults;
  if (config.ordered) {
    orderedResults.reserve(normalFiles.size());
  }

  if (config.markdownlintFixes) {
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
  if (config.ordered) {
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
    exit(1); // Exit immediately on usage error.
  }

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
        std::string entry = argv[++i];
        fs::path absoluteEntry = fs::absolute(config.dirPath / entry);
        if (fs::is_directory(absoluteEntry)) {
          try {
            fs::path relativeEntry =
                fs::relative(absoluteEntry, config.dirPath);
            config.ignoredFolders.emplace_back(relativeEntry);
          } catch (const std::exception &e) {
            std::cerr << "Invalid ignore path: " << absoluteEntry
                      << " is not under " << config.dirPath << '\n';
            exit(1); //  consistent error handling
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
    } else if (arg == "-p" || arg == "--relative-path") {
      config.showRelativePath = true;
    } else if (arg == "-o" || arg == "--ordered") {
      config.ordered = true;
    } else if (arg == "-z" || arg == "--last") {
      while (i + 1 < argc && argv[i + 1][0] != '-') {
        std::string entry = argv[++i];
        fs::path relativeEntry =
            fs::weakly_canonical(fs::path(entry)); // Normalize path
        if (fs::is_directory(config.dirPath / relativeEntry)) {
          config.lastDirs.emplace_back(
              fs::relative(config.dirPath / relativeEntry, config.dirPath));
        } else {
          config.lastFiles.emplace_back(
              fs::relative(config.dirPath / relativeEntry, config.dirPath));
        }
      }
    } else if (arg == "-w" || arg == "--markdownlint-fixes") {
      config.markdownlintFixes = true;
    } else {
      std::cerr << "Invalid option: " << arg << "\n";
      exit(1); // Exit on invalid options
    }
  }
  return config;
}