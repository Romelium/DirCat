#include <algorithm>
#include <atomic>
#include <cctype> // For std::toupper
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <ios> // Needed for std::ios_base
#include <iostream>
#include <limits> // Needed for numeric_limits
#include <mutex>
#include <regex>
#include <shared_mutex> // For read-write mutex
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error> // For filesystem errors
#include <thread>
#include <tuple> // Needed for optimized sort
#include <unordered_map>
#include <unordered_set>
#include <utility> // For std::move
#include <vector>

namespace fs = std::filesystem;

// --- Configuration ---
struct Config {
  fs::path dirPath; // Input path (file or directory), stored as absolute
  unsigned long long maxFileSizeB = 0;
  bool recursiveSearch = true;
  std::vector<std::string> fileExtensions;         // Lowercase, no dot
  std::vector<std::string> excludedFileExtensions; // Lowercase, no dot
  std::vector<fs::path> ignoredFolders; // Relative paths from input dir
  std::vector<fs::path>
      ignoredFiles; // Relative paths or filenames from input dir
  std::vector<std::string> regexFilters; // Exclude patterns for filename
  std::vector<std::string>
      filenameRegexFilters; // Include patterns for filename
  bool removeComments = false;
  bool removeEmptyLines = false;
  bool showFilenameOnly = false;
  std::vector<fs::path>
      lastFiles; // Relative paths or filenames as provided by user
  std::vector<fs::path> lastDirs; // Relative paths as provided by user
  bool disableGitignore = false;
  bool onlyLast = false;
  fs::path outputFile; // Absolute or relative path
  bool showLineNumbers = false;
  bool dryRun = false;

  // --- Performance Optimizations ---
  // Sets for faster lookups in is_last_file (populated in parse_arguments)
  std::unordered_set<std::string> lastFilesSetRel; // Normalized relative paths
  std::unordered_set<std::string> lastFilesSetFilename; // Normalized filenames
  std::unordered_set<std::string> lastDirsSetRel; // Normalized relative paths
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

// Normalizes path separators to '/' and simplifies lexically
std::string normalize_path(const fs::path &path) {
  // Use weakly_canonical for potentially non-existent paths or lexical_normal?
  // Lexical normal is safer if paths might not exist during checks.
  std::string path_str = path.lexically_normal().string();
  std::replace(path_str.begin(), path_str.end(), '\\', '/');
  return path_str;
}

// --- Gitignore Caching (Improvement 2) ---
// Static cache for loaded gitignore rules (norm_abs_path/gitignore -> rules)
static std::unordered_map<std::string, std::vector<std::string>>
    gitignore_rules_cache;
static std::shared_mutex gitignore_cache_mutex;

// Static cache for compiled regex patterns (rule string -> regex object)
static std::unordered_map<std::string, std::regex> regex_cache;
static std::shared_mutex regex_cache_mutex;

// Static cache for accumulated gitignore rules (norm_abs_parent_dir_path ->
// effective rules)
static std::unordered_map<std::string, std::vector<std::string>>
    accumulated_rules_cache;
static std::shared_mutex accumulated_cache_mutex;
// --- End Gitignore Caching ---

// Loads rules from a specific .gitignore file, using cache
std::vector<std::string> load_gitignore_rules(const fs::path &gitignore_path) {
  std::string cache_key = normalize_path(fs::absolute(gitignore_path));

  {
    std::shared_lock<std::shared_mutex> lock(gitignore_cache_mutex);
    auto it = gitignore_rules_cache.find(cache_key);
    if (it != gitignore_rules_cache.end()) {
      return it->second;
    }
  }

  std::vector<std::string> rules;
  std::ifstream file(gitignore_path); // Not binary mode
  if (!file.is_open()) {
    // Cache empty rules even on error/not found to avoid re-checking file
    // system
    std::unique_lock<std::shared_mutex> lock(gitignore_cache_mutex);
    gitignore_rules_cache[cache_key] = rules; // Cache empty vector
    return rules;
  }

  std::string line;
  while (std::getline(file, line)) {
    line = trim(line);
    // Filter empty lines and comments
    if (!line.empty() && line[0] != '#') {
      rules.push_back(line);
    }
  }
  file.close();

  {
    std::unique_lock<std::shared_mutex> lock(gitignore_cache_mutex);
    gitignore_rules_cache[cache_key] = rules; // Cache loaded rules
  }
  return rules;
}

// --- Gitignore Matching Logic (Simplified - Full compliance is complex) ---

// Compiles a gitignore pattern (like *.c, /foo, bar/) into a regex string part
// Note: This is a simplified conversion and may not cover all gitignore
// nuances.
std::string gitignore_pattern_to_regex_string(const std::string &pattern) {
  std::string regex_str;
  regex_str.reserve(pattern.length() * 2);

  bool anchored_start = false;
  size_t start_pos = 0;

  if (pattern[0] == '/') {
    anchored_start = true;
    start_pos = 1;
  }

  // If not anchored to start, it can match anywhere after a '/' or at the
  // beginning
  if (!anchored_start) {
    regex_str += "(^|/)";
  } else {
    regex_str += "^"; // Anchor to the beginning of the relative path
  }

  bool is_dir_pattern = (!pattern.empty() && pattern.back() == '/');
  size_t end_pos = is_dir_pattern ? pattern.length() - 1 : pattern.length();

  for (size_t i = start_pos; i < end_pos; ++i) {
    char c = pattern[i];
    switch (c) {
    case '*':
      if (i + 1 < end_pos && pattern[i + 1] == '*') {
        // '**' basic handling: match zero or more characters including '/'
        // More correct gitignore '/**/' requires context. Simplification: '.*'
        regex_str += ".*";
        i++; // Skip the second '*'
             // Handle specific '**/' case if needed
        if (i + 1 < end_pos && pattern[i + 1] == '/') {
          i++; // Skip the '/' as well, the '.*' covers it.
        }
      } else {
        // '*' matches anything except '/'
        regex_str += "[^/]*";
      }
      break;
    case '?':
      regex_str += "[^/]"; // Matches any single character except '/'
      break;
    case '.':
      regex_str += "\\."; // Escape dot
      break;
    case '[': // Character classes are complex, treat literally for now
    case ']':
    // Escape other regex metacharacters
    case '\\':
    case '^':
    case '$':
    case '+':
    case '(':
    case ')':
    case '{':
    case '}':
    case '|':
      regex_str += '\\';
      regex_str += c;
      break;
    default:
      regex_str += c; // Regular character
    }
  }

  // If it was a directory pattern (ended with '/'), ensure match ends with '/'
  // or string end
  if (is_dir_pattern) {
    regex_str += "/";
  } else {
    // If not a dir pattern, it should match a full component or the end
    regex_str += "($|/)"; // Match end of string or just before a slash
  }

  return regex_str;
}

// Compiles and caches regex pattern
// FIX: Removed reg from regex_string parameter name
std::regex compile_and_cache_regex(const std::string &pattern_key,
                                   const std::string regex_string) {
  {
    std::shared_lock<std::shared_mutex> lock(regex_cache_mutex);
    auto it = regex_cache.find(pattern_key);
    if (it != regex_cache.end()) {
      return it->second;
    }
  }
  // Compile and cache (with write lock)
  try {
    // FIX: Use correct parameter name regex_string
    std::regex compiled(regex_string, std::regex::icase | std::regex::optimize);
    std::unique_lock<std::shared_mutex> lock(regex_cache_mutex);
    regex_cache[pattern_key] = compiled; // Cache successful compilation
    return compiled;
  } catch (const std::regex_error &e) {
    // FIX: Use correct parameter name regex_string
    std::cerr << "WARNING: Invalid regex generated from pattern '"
              << pattern_key << "': '" << regex_string << "' (" << e.what()
              << ")\n";
    std::regex empty_regex; // Regex that matches nothing
    std::unique_lock<std::shared_mutex> lock(regex_cache_mutex);
    regex_cache[pattern_key] = empty_regex; // Cache empty on error
    return empty_regex;
  }
}

// Checks if a normalized relative path matches a single gitignore rule string
bool matches_gitignore_rule(const std::string &normalized_relative_path,
                            bool is_dir, const std::string &rule) {
  if (rule.empty())
    return false;

  std::string pattern = rule;
  bool negate = false;
  if (pattern[0] == '!') {
    negate = true; // Negation handled by caller
    pattern.erase(0, 1);
  }
  if (pattern.empty())
    return false;

  bool pattern_is_dir = (pattern.back() == '/');

  // Generate regex string from pattern
  std::string regex_str = gitignore_pattern_to_regex_string(pattern);
  std::regex compiled_regex = compile_and_cache_regex(pattern, regex_str);

  // Use regex_search as patterns often match parts of the path
  return std::regex_search(normalized_relative_path, compiled_regex);
}

// Uses cached accumulated rules (Improvement 2)
bool is_path_ignored_by_gitignore(
    const fs::path &absolute_path, // Path to check (must be absolute)
    const fs::path &base_abs_path, // Base directory (must be absolute)
    const std::unordered_map<std::string, std::vector<std::string>>
        &dir_gitignore_rules // Raw rules map (abs dir path -> rules)
) {
  // Explicitly ignore .git directory components anywhere in the path
  if (std::any_of(
          absolute_path.begin(), absolute_path.end(),
          [](const fs::path &component) { return component == ".git"; })) {
    return true;
  }

  std::vector<std::string> effective_rules;
  fs::path parent_dir = absolute_path.has_parent_path()
                            ? absolute_path.parent_path()
                            : absolute_path;
  std::string parent_dir_key = normalize_path(parent_dir);

  // --- Improvement 2: Check accumulated cache ---
  bool found_in_cache = false;
  {
    std::shared_lock<std::shared_mutex> lock(accumulated_cache_mutex);
    auto cache_it = accumulated_rules_cache.find(parent_dir_key);
    if (cache_it != accumulated_rules_cache.end()) {
      effective_rules = cache_it->second;
      found_in_cache = true;
    }
  }
  // --- End Improvement 2 Check ---

  // If not found in cache, compute and store
  if (!found_in_cache) {
    std::vector<std::string> rules_to_cache;
    fs::path current_build_path = parent_dir;

    // Walk up from the path's parent directory towards the base directory
    while (true) {
      std::string normalized_current_path = normalize_path(current_build_path);
      auto it = dir_gitignore_rules.find(normalized_current_path);
      if (it != dir_gitignore_rules.end()) {
        // Insert rules from this level at the beginning (higher precedence)
        rules_to_cache.insert(rules_to_cache.begin(), it->second.begin(),
                              it->second.end());
      }

      // Stop if we've reached the base path or its parent or root
      if (current_build_path == base_abs_path ||
          !current_build_path.has_parent_path() ||
          current_build_path == current_build_path.root_path()) {
        // Check base path itself one last time if needed
        if (current_build_path == base_abs_path) {
          std::string normalized_base_path = normalize_path(base_abs_path);
          auto base_it = dir_gitignore_rules.find(normalized_base_path);
          if (base_it != dir_gitignore_rules.end()) {
            rules_to_cache.insert(rules_to_cache.begin(),
                                  base_it->second.begin(),
                                  base_it->second.end());
          }
        }
        break;
      }
      current_build_path = current_build_path.parent_path();
    }

    // --- Improvement 2: Store in accumulated cache ---
    {
      std::unique_lock<std::shared_mutex> lock(accumulated_cache_mutex);
      // Double check cache before inserting (another thread might have
      // finished)
      if (accumulated_rules_cache.find(parent_dir_key) ==
          accumulated_rules_cache.end()) {
        accumulated_rules_cache[parent_dir_key] = rules_to_cache;
      }
      // Use the rules (either freshly cached or computed by another thread)
      effective_rules = accumulated_rules_cache[parent_dir_key];
    }
    // --- End Improvement 2 Store ---
  }

  if (effective_rules.empty()) {
    return false; // No gitignore rules apply up to this path's parent
  }

  // Now check the relative path against the effective rules
  fs::path relative_path;
  try {
    relative_path = fs::relative(absolute_path, base_abs_path);
  } catch (const std::exception &e) {
    std::cerr << "ERROR: Error getting relative path for gitignore check: "
              << normalize_path(absolute_path) << " relative to "
              << normalize_path(base_abs_path) << ": " << e.what() << '\n';
    return false; // Don't ignore if relative path fails
  }
  std::string normalized_relative_path = normalize_path(relative_path);
  bool is_dir = fs::is_directory(absolute_path); // Check if path is a directory

  bool ignored = false;
  // Iterate through rules. Last matching rule wins. Negation rules (`!`)
  // override.
  for (const auto &rule : effective_rules) {
    std::string clean_rule = rule;
    bool negate = false;
    if (!clean_rule.empty() && clean_rule[0] == '!') {
      negate = true;
      clean_rule.erase(0, 1);
    }

    bool matched = false;
    // If rule is a directory rule, check against the path itself and the path
    // as if it were a directory entry
    if (!clean_rule.empty() && clean_rule.back() == '/') {
      matched = matches_gitignore_rule(
                    normalized_relative_path, is_dir,
                    clean_rule) || // Check if rule matches path directly (e.g.,
                                   // "dir/" matches "dir/subdir")
                matches_gitignore_rule(
                    normalized_relative_path + "/", true,
                    clean_rule); // Check if rule matches path + "/" (e.g.,
                                 // "dir/" matches "dir/")
    } else {
      matched = matches_gitignore_rule(
          normalized_relative_path, is_dir,
          clean_rule); // Standard check for file patterns
    }

    if (matched) {
      ignored = !negate; // If rule matches, set ignored based on negation
    }
  }
  return ignored;
}

// --- File Property Checks ---

bool is_file_size_valid(unsigned long long file_size,
                        unsigned long long max_file_size_b) {
  return max_file_size_b == 0 || file_size <= max_file_size_b;
}

bool is_file_extension_allowed(
    const fs::path &path,
    const std::vector<std::string> &allowed_extensions,  // lowercase, no dot
    const std::vector<std::string> &excluded_extensions) // lowercase, no dot
{
  if (!path.has_extension()) {
    // Allow files with no extension only if no specific extensions are required
    // AND it's not explicitly excluded (though excluding no-extension is rare)
    return allowed_extensions.empty();
  }

  std::string ext = path.extension().string();
  if (ext.length() <= 1)
    return allowed_extensions.empty(); // Only has dot, treat as no extension

  std::string ext_no_dot = ext.substr(1);
  std::transform(
      ext_no_dot.begin(), ext_no_dot.end(), ext_no_dot.begin(),
      [](unsigned char c) { return std::tolower(c); }); // Use lambda for safety

  // Check exclusion first
  if (std::find(excluded_extensions.begin(), excluded_extensions.end(),
                ext_no_dot) != excluded_extensions.end()) {
    return false;
  }

  // If allowed list is empty, allow everything not excluded
  if (allowed_extensions.empty()) {
    return true;
  }

  // Otherwise, check if it's in the allowed list
  return std::find(allowed_extensions.begin(), allowed_extensions.end(),
                   ext_no_dot) != allowed_extensions.end();
}

// Now uses cached gitignore check
bool should_ignore_folder(
    const fs::path &absolute_folder_path, // Must be absolute
    bool disableGitignore,
    const fs::path &base_abs_path,                   // Must be absolute
    const std::vector<fs::path> &ignoredFolderPaths, // Relative paths
    const std::unordered_map<std::string, std::vector<std::string>>
        &dir_gitignore_rules // Raw rules map
) {
  // Check gitignore first (uses caching internally now)
  if (!disableGitignore &&
      is_path_ignored_by_gitignore(absolute_folder_path, base_abs_path,
                                   dir_gitignore_rules)) {
    return true;
  }

  // Check manually ignored folders (match relative path prefix)
  fs::path relativePath;
  try {
    relativePath = fs::relative(absolute_folder_path, base_abs_path);
  } catch (const std::exception &) {
    // Folder not under base path? Should not happen in normal iteration.
    return false;
  }
  std::string relativePathStr = normalize_path(relativePath);

  for (const auto &ignoredRelativeFolder : ignoredFolderPaths) {
    std::string ignoredStr = normalize_path(ignoredRelativeFolder);
    // Ensure ignoredStr ends with '/' for directory matching logic
    if (ignoredStr.empty())
      continue;
    if (ignoredStr.empty())
      continue;
    // Check if the path being checked IS the ignored directory, OR
    // if the path being checked STARTS WITH the ignored directory name + '/'
    if (relativePathStr == ignoredStr ||
        (!ignoredStr.empty() &&
         relativePathStr.rfind(ignoredStr + '/', 0) == 0)) {
      return true;
    }
  }

  return false;
}

// Uses cached gitignore check and passed file size (Improvement 6)
bool should_ignore_file(
    const fs::path &absolute_file_path, // Must be absolute
    unsigned long long file_size,       // Passed in
    bool disableGitignore,
    const fs::path &base_abs_path, // Must be absolute
    unsigned long long maxFileSizeB,
    const std::vector<fs::path>
        &ignoredFilesOrPatterns, // Relative paths or filenames
    const std::unordered_map<std::string, std::vector<std::string>>
        &dir_gitignore_rules // Raw rules
) {
  // Check gitignore first (uses caching internally)

  // Explicitly ignore .gitignore files themselves
  if (absolute_file_path.filename() == ".gitignore") {
    return true;
  }

  if (!disableGitignore &&
      is_path_ignored_by_gitignore(absolute_file_path, base_abs_path,
                                   dir_gitignore_rules)) {
    return true;
  }

  // Check file size (using passed size)
  if (!is_file_size_valid(file_size, maxFileSizeB)) {
    return true;
  }

  // Check manually ignored files/patterns
  fs::path relativePath;
  try {
    relativePath = fs::relative(absolute_file_path, base_abs_path);
  } catch (const std::exception &) {
    return false; // Path not under dirPath? Shouldn't happen.
  }

  std::string relativePathStr = normalize_path(relativePath);
  std::string filenameStr = normalize_path(absolute_file_path.filename());

  for (const auto &ignoredEntry : ignoredFilesOrPatterns) {
    std::string ignoredStr = normalize_path(ignoredEntry);
    if (ignoredStr.find('/') != std::string::npos) {
      // Ignored entry contains path separator, treat as relative path
      if (relativePathStr == ignoredStr)
        return true;
    } else {
      // Ignored entry is just a filename
      if (filenameStr == ignoredStr)
        return true;
    }
  }

  return false;
}

// --- Regex Filters ---

// Helper to get/compile/cache regex
// FIX: Removed reg from regexStr parameter name
std::regex get_compiled_regex(const std::string regexStr) {
  {
    std::shared_lock<std::shared_mutex> lock(regex_cache_mutex);
    // FIX: Use correct parameter name regexStr
    auto it = regex_cache.find(regexStr);
    if (it != regex_cache.end()) {
      return it->second;
    }
  }
  // Compile and cache
  try {
    // Use optimize flag if available and potentially useful
    // FIX: Use correct parameter name regexStr
    std::regex compiled(regexStr, std::regex::optimize);
    std::unique_lock<std::shared_mutex> lock(regex_cache_mutex);
    // FIX: Use correct parameter name regexStr
    regex_cache[regexStr] = compiled; // Cache success
    return compiled;
  } catch (const std::regex_error &e) {
    // FIX: Use correct parameter name regexStr
    std::cerr << "ERROR: Invalid regex: '" << regexStr << "': " << e.what()
              << '\n';
    std::regex empty; // Matches nothing
    std::unique_lock<std::shared_mutex> lock(regex_cache_mutex);
    // FIX: Use correct parameter name regexStr
    regex_cache[regexStr] = empty; // Cache empty on error
    return empty;
  }
}

// Check if filename matches any exclusion regex
bool matches_regex_filters(const fs::path &path,
                           // FIX: Removed reg from regex_filters parameter name
                           const std::vector<std::string> regex_filters) {
  // FIX: Use correct parameter name regex_filters
  if (regex_filters.empty())
    return false; // No filters means no exclusion based on this

  const std::string filename = path.filename().string(); // Get filename part
  // FIX: Use correct loop variable name regexStr and parameter name
  // regex_filters
  for (const auto regexStr : regex_filters) {
    // FIX: Use correct variable name regexStr
    std::regex compiled_regex = get_compiled_regex(regexStr);
    if (std::regex_search(filename, compiled_regex)) { // search = find anywhere
      return true; // Exclude if any filter matches
    }
  }
  return false; // Don't exclude if no filters matched
}

// Check if filename matches any inclusion regex
bool matches_filename_regex_filters(
    const fs::path &path,
    const std::vector<std::string> &filename_regex_filters) {
  if (filename_regex_filters.empty())
    return true; // No filters means include all (that passed other checks)

  const std::string filename = path.filename().string();
  // FIX: Use correct loop variable name regexStr
  for (const auto regexStr : filename_regex_filters) {
    // FIX: Use correct variable name regexStr
    std::regex compiled_regex = get_compiled_regex(regexStr);
    // Use regex_match: the pattern must match the *entire* filename
    if (std::regex_match(filename, compiled_regex)) {
      return true; // Include if any filter matches
    }
  }
  return false; // Don't include if no filters matched
}

// --- File Content Processing ---

// No change needed in logic, added reserve()
std::string remove_cpp_comments(const std::string &code) {
  std::string result;
  result.reserve(code.length()); // Pre-allocate memory
  bool inString = false;
  bool inChar = false;
  bool inSingleLineComment = false;
  bool inMultiLineComment = false;

  for (size_t i = 0; i < code.size(); ++i) {
    char current_char = code[i];
    char next_char = (i + 1 < code.size()) ? code[i + 1] : '\0';

    if (inString) {
      result += current_char;
      if (current_char == '\\' && next_char != '\0') {
        result += next_char;
        ++i;
      } else if (current_char == '"') {
        inString = false;
      }
    } else if (inChar) {
      result += current_char;
      if (current_char == '\\' && next_char != '\0') {
        result += next_char;
        ++i;
      } else if (current_char == '\'') {
        inChar = false;
      }
    } else if (inSingleLineComment) {
      if (current_char == '\n') {
        inSingleLineComment = false;
        result += current_char;
      }
    } else if (inMultiLineComment) {
      if (current_char == '*' && next_char == '/') {
        inMultiLineComment = false;
        ++i;
      }
    } else {
      if (current_char == '"') {
        inString = true;
        result += current_char;
      } else if (current_char == '\'') {
        inChar = true;
        result += current_char;
      } else if (current_char == '/' && next_char == '/') {
        inSingleLineComment = true;
        ++i;
      } else if (current_char == '/' && next_char == '*') {
        inMultiLineComment = true;
        ++i;
      } else {
        result += current_char;
      }
    }
  }
  return result;
}

// Uses string_view for line processing (minor optimization)
std::string
format_file_output(const fs::path &absolute_path, bool showFilenameOnly,
                   const fs::path &base_abs_path, // Base directory path
                   const std::string &file_content, bool removeEmptyLines,
                   bool showLineNumbers) {
  std::stringstream content_buffer; // Use stringstream for easier formatting
  fs::path displayPath;
  if (showFilenameOnly) {
    displayPath = absolute_path.filename();
  } else {
    try {
      // Calculate relative path from base for display
      displayPath = fs::relative(absolute_path, base_abs_path);
    } catch (const std::exception &) {
      displayPath = absolute_path.filename(); // Fallback if relative fails
    }
  }

  content_buffer << "\n## File: " << normalize_path(displayPath) << "\n\n```";
  if (absolute_path.has_extension()) {
    std::string ext = absolute_path.extension().string();
    if (ext.length() > 1)
      content_buffer << ext.substr(1); // Add extension without dot
  }
  content_buffer << "\n";

  // Process content line by line using string_view
  std::string_view content_view = file_content;
  size_t line_start = 0;
  int lineNumber = 1;
  while (line_start < content_view.length()) {
    size_t line_end = content_view.find('\n', line_start);
    size_t current_line_len = (line_end == std::string_view::npos)
                                  ? (content_view.length() - line_start)
                                  : (line_end - line_start);
    std::string_view line = content_view.substr(line_start, current_line_len);

    // Trim potential trailing '\r'
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }

    bool is_empty_line =
        (line.find_first_not_of(" \t") == std::string_view::npos);

    if (!removeEmptyLines || !is_empty_line) {
      if (showLineNumbers) {
        content_buffer << lineNumber++ << " | ";
      }
      content_buffer << line << '\n'; // Add the newline back
    }

    if (line_end == std::string_view::npos)
      break;                   // Reached end of string
    line_start = line_end + 1; // Move past the newline character
  }
  // Ensure a newline exists after the last line of code if content wasn't empty
  // and didn't end with \n
  if (!file_content.empty() && file_content.back() != '\n') {
    // content_buffer << '\n'; // Add final newline if missing - loop handles
    // this.
  } else if (file_content.empty() && showLineNumbers) {
    // content_buffer << "1 | \n"; // Show line number 1 for empty file? Let's
    // show nothing.
  } else if (file_content.empty()) {
    // Add a newline for empty file for consistent formatting?
    // content_buffer << "\n"; // Let's not add extra newline for empty files.
  }

  content_buffer << "```\n";
  return content_buffer.str();
}

// Removed redundant file size check (Improvement 6)
std::string
process_single_file(const fs::path &absolute_path, // Must be absolute
                    const Config &config,          // Pass config
                    const fs::path &base_abs_path  // Base directory
) {
  if (config.dryRun) {
    // For dry run, we just need to format the header part
    // Ensure empty string is handled correctly by format_file_output
    return format_file_output(absolute_path, config.showFilenameOnly,
                              base_abs_path, "", config.removeEmptyLines,
                              config.showLineNumbers);
  }

  std::ifstream file(absolute_path, std::ios::binary);
  if (!file) {
    // Use cerr for errors
    std::cerr << "ERROR: Could not open file: " << normalize_path(absolute_path)
              << '\n';
    return ""; // Return empty string on error
  }

  // Read the whole file - This remains a potential bottleneck for large files
  // Consider using memory-mapped files for very large inputs if this is
  // profiled as slow.
  std::string fileContent((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());
  file.close(); // Close file ASAP

  if (config.removeComments) {
    fileContent = remove_cpp_comments(fileContent);
  }

  // Pass base path for relative path calculation in formatting
  return format_file_output(absolute_path, config.showFilenameOnly,
                            base_abs_path, fileContent, config.removeEmptyLines,
                            config.showLineNumbers);
}

// --- File Collection ---

// Uses Config sets for faster lookup (Improvement 4)
// Expects absolute path for absPath and config.dirPath
bool is_last_file(const fs::path &absPath, const Config &config) {
  if (!config.dirPath.is_absolute() || !absPath.is_absolute()) {
    std::cerr << "WARNING: is_last_file called with non-absolute paths.\n";
    return false;
  }

  fs::path relative_path;
  try {
    // Use lexically_relative if paths might be on different drives (Windows)
    // relative() might throw if not hierarchically related.
    relative_path = fs::relative(absPath, config.dirPath);
  } catch (const std::exception &) {
    return false; // Cannot be a last file if not relative to dirPath
  }

  std::string relPathStr = normalize_path(relative_path);
  std::string filenameStr = normalize_path(absPath.filename());

  // Check relative path set
  if (config.lastFilesSetRel.count(relPathStr)) {
    return true;
  }
  // Check filename set
  if (config.lastFilesSetFilename.count(filenameStr)) {
    return true;
  }

  // Check if path is within any of the last directories
  for (const auto &lastDirRelStr : config.lastDirsSetRel) {
    // Check if relPathStr starts with lastDirRelStr + "/"
    if (!lastDirRelStr.empty() && lastDirRelStr.back() != '/') {
      if (relPathStr.rfind(lastDirRelStr + '/', 0) == 0)
        return true;
    } else { // lastDirRelStr already ends with / or is empty
      if (relPathStr.rfind(lastDirRelStr, 0) == 0)
        return true;
    }
    // Or if relPathStr *is* the directory name (less likely for a file)
    if (relPathStr == lastDirRelStr)
      return true;
  }

  return false;
}

// Passes file_size to should_ignore_file, uses optimized is_last_file
// Returns pair of vectors containing ABSOLUTE paths
std::pair<std::vector<fs::path>, std::vector<fs::path>>
collect_files(const Config &config, std::atomic<bool> &should_stop) {
  std::vector<fs::path> normalFiles;   // Absolute paths
  std::vector<fs::path> lastFilesList; // Absolute paths
  // Use normalized absolute path strings for uniqueness tracking during
  // collection
  std::unordered_set<std::string> collected_abs_paths_set;

  if (!fs::is_directory(config.dirPath)) {
    std::cerr << "ERROR: collect_files called with a non-directory path: "
              << normalize_path(config.dirPath) << std::endl;
    return {{}, {}};
  }
  const fs::path base_abs_path = config.dirPath; // Base is already absolute

  // Preload gitignore rules for all relevant directories
  std::unordered_map<std::string, std::vector<std::string>>
      dir_gitignore_rules_map;
  if (!config.disableGitignore) {
    // accumulated_rules_cache.clear(); // Clear accumulated cache for this
    // specific run? - No, share across runs if possible

    try {
      // Iterate non-recursively first to get top-level .gitignore
      if (fs::exists(base_abs_path / ".gitignore")) {
        dir_gitignore_rules_map[normalize_path(base_abs_path)] =
            load_gitignore_rules(base_abs_path / ".gitignore");
      }
      // Then recursively find others if needed
      if (config.recursiveSearch) {
        fs::recursive_directory_iterator gitignore_it(
            base_abs_path, fs::directory_options::follow_directory_symlink |
                               fs::directory_options::skip_permission_denied);
        fs::recursive_directory_iterator end_gitignore_it;
        for (; gitignore_it != end_gitignore_it; ++gitignore_it) {
          try {
            const auto &current_path = gitignore_it->path();
            // Skip iterating into directories that are gitignored (basic check)
            if (gitignore_it->is_directory() &&
                is_path_ignored_by_gitignore(current_path, base_abs_path,
                                             dir_gitignore_rules_map)) {
              gitignore_it.disable_recursion_pending();
              continue;
            }
            if (!gitignore_it->is_directory() &&
                gitignore_it->path().filename() == ".gitignore") {
              std::string dir_key =
                  normalize_path(gitignore_it->path().parent_path());
              if (dir_gitignore_rules_map.find(dir_key) ==
                  dir_gitignore_rules_map
                      .end()) { // Avoid reloading if already done
                dir_gitignore_rules_map[dir_key] =
                    load_gitignore_rules(gitignore_it->path());
              }
            }
          } catch (const fs::filesystem_error &fs_err) {
            // Permissions error likely, report and continue if possible
            std::cerr
                << "WARNING: Filesystem error scanning for .gitignore near "
                << normalize_path(gitignore_it->path()) << ": " << fs_err.what()
                << std::endl;
            // If it's a directory causing the error, try to skip it
            if (gitignore_it.depth() > 0 && gitignore_it->is_directory()) {
              gitignore_it.disable_recursion_pending();
            } else if (gitignore_it.depth() == 0) {
              // Error at top level, might need to stop iteration depending on
              // error For now, just warn and continue
            }
          }
        }
      }
    } catch (const std::exception &e) {
      std::cerr << "WARNING: Error scanning for .gitignore files: " << e.what()
                << std::endl;
    }
  }

  // Helper lambda for skipping directories (uses cached gitignore)
  auto check_and_skip_directory = [&](const fs::path &absolute_dir_path) {
    return should_ignore_folder(absolute_dir_path, config.disableGitignore,
                                base_abs_path, config.ignoredFolders,
                                dir_gitignore_rules_map);
  };

  // --- Handle --only-last separately ---
  if (config.onlyLast) {
    // Collect explicitly listed files
    for (const auto &lastFileEntry : config.lastFiles) {
      fs::path absPath =
          base_abs_path / lastFileEntry; // Resolve relative to base
      std::string normAbsPathStr = normalize_path(absPath);

      if (fs::exists(absPath) && fs::is_regular_file(absPath)) {
        if (collected_abs_paths_set.find(normAbsPathStr) ==
            collected_abs_paths_set.end()) {
          lastFilesList.push_back(absPath);
          collected_abs_paths_set.insert(normAbsPathStr);
        }
      } else {
        std::cerr << "WARNING: --only-last specified file not found or not a "
                     "regular file: "
                  << normalize_path(lastFileEntry)
                  << " (resolved to: " << normAbsPathStr << ")\n";
      }
    }

    // Collect files from explicitly listed directories
    for (const auto &lastDirEntry : config.lastDirs) {
      fs::path absDirPath =
          base_abs_path / lastDirEntry; // Resolve relative to base
      if (!fs::exists(absDirPath) || !fs::is_directory(absDirPath)) {
        std::cerr << "WARNING: --only-last specified directory not found or "
                     "not a directory: "
                  << normalize_path(lastDirEntry)
                  << " (resolved to: " << normalize_path(absDirPath) << ")\n";
        continue;
      }

      try {
        auto dir_options = fs::directory_options::follow_directory_symlink |
                           fs::directory_options::skip_permission_denied;
        fs::recursive_directory_iterator dir_it(absDirPath, dir_options);
        fs::recursive_directory_iterator end;
        for (; dir_it != end && !should_stop; ++dir_it) {
          try {
            const auto &entry_path_abs = dir_it->path();

            // Skip subdirectories if they match ignore rules
            if (dir_it->is_directory() &&
                check_and_skip_directory(entry_path_abs)) {
              dir_it.disable_recursion_pending();
              continue;
            }

            if (fs::is_regular_file(entry_path_abs)) {
              std::string normEntryAbsPathStr = normalize_path(entry_path_abs);
              if (collected_abs_paths_set.count(normEntryAbsPathStr))
                continue; // Already added

              unsigned long long file_size = 0;
              std::error_code ec;
              file_size = fs::file_size(entry_path_abs, ec);
              if (ec) { /* ignore error, proceed with size 0 */
                file_size = 0;
              }

              // Apply filters even within --only-last directories
              if (is_file_extension_allowed(entry_path_abs,
                                            config.fileExtensions,
                                            config.excludedFileExtensions) &&
                  !should_ignore_file(entry_path_abs, file_size,
                                      config.disableGitignore, base_abs_path,
                                      config.maxFileSizeB, config.ignoredFiles,
                                      dir_gitignore_rules_map) &&
                  !matches_regex_filters(entry_path_abs, config.regexFilters) &&
                  matches_filename_regex_filters(entry_path_abs,
                                                 config.filenameRegexFilters)) {
                lastFilesList.push_back(entry_path_abs);
                collected_abs_paths_set.insert(normEntryAbsPathStr);
              }
            }
          } catch (const fs::filesystem_error &fs_err) {
            std::cerr << "WARNING: Filesystem error iterating --only-last "
                         "directory near "
                      << normalize_path(dir_it->path()) << ": " << fs_err.what()
                      << std::endl;
            if (dir_it.depth() > 0 && dir_it->is_directory()) {
              dir_it.disable_recursion_pending();
            }
          }
        }
      } catch (const std::exception &e) {
        std::cerr << "WARNING: Error iterating --only-last directory "
                  << normalize_path(lastDirEntry) << ": " << e.what() << '\n';
      }
    }
    return {{},
            lastFilesList}; // Return empty normal files, populated last files
  }

  // --- Normal processing (not --only-last) ---
  try {
    auto options = fs::directory_options::follow_directory_symlink |
                   fs::directory_options::skip_permission_denied;

    if (config.recursiveSearch) {
      fs::recursive_directory_iterator it(base_abs_path, options);
      fs::recursive_directory_iterator end;
      for (; it != end && !should_stop; ++it) {
        try {
          const auto &entry_path_abs = it->path(); // Already absolute

          // Skip directory if needed (before checking file properties)
          if (it->is_directory() && check_and_skip_directory(entry_path_abs)) {
            it.disable_recursion_pending();
            continue;
          }

          // Explicitly skip .gitignore files
          if (entry_path_abs.filename() == ".gitignore") {
            continue;
          }
          if (fs::is_regular_file(entry_path_abs)) {
            std::string normAbsPathStr = normalize_path(entry_path_abs);
            if (collected_abs_paths_set.count(normAbsPathStr))
              continue; // Already processed (e.g., as a 'last' file)

            unsigned long long file_size = 0;
            std::error_code ec_size;
            file_size = fs::file_size(entry_path_abs, ec_size);
            if (ec_size) {
              // std::cerr << "WARNING: Cannot get file size for " <<
              // normAbsPathStr << ": " << ec_size.message() << ". Skipping
              // file.\n";
              continue; // Skip file if size cannot be determined
            }

            // Apply all filters
            if (is_file_extension_allowed(entry_path_abs, config.fileExtensions,
                                          config.excludedFileExtensions) &&
                !should_ignore_file(entry_path_abs, file_size,
                                    config.disableGitignore, base_abs_path,
                                    config.maxFileSizeB, config.ignoredFiles,
                                    dir_gitignore_rules_map) &&
                !matches_regex_filters(entry_path_abs, config.regexFilters) &&
                matches_filename_regex_filters(entry_path_abs,
                                               config.filenameRegexFilters)) {
              // Check if it's a 'last' file (using optimized check)
              if (is_last_file(entry_path_abs, config)) {
                lastFilesList.push_back(entry_path_abs);
              } else {
                normalFiles.push_back(entry_path_abs);
              }
              collected_abs_paths_set.insert(
                  normAbsPathStr); // Mark as collected
            }
          }
        } catch (const fs::filesystem_error &fs_err) {
          std::cerr << "WARNING: Filesystem error during directory scan near "
                    << normalize_path(it->path()) << ": " << fs_err.what()
                    << std::endl;
          if (it.depth() > 0 && it->is_directory()) {
            it.disable_recursion_pending();
          }
        }
      }
    } else { // Non-recursive search
      for (fs::directory_iterator it(base_abs_path, options), end;
           it != end && !should_stop; ++it) {
        try {
          const auto &entry_path_abs = it->path();

          // Skip ignored directories explicitly
          if (fs::is_directory(entry_path_abs) &&
              check_and_skip_directory(entry_path_abs)) {
            continue;
          }

          if (fs::is_regular_file(entry_path_abs)) {
            std::string normAbsPathStr = normalize_path(entry_path_abs);
            if (collected_abs_paths_set.count(normAbsPathStr))
              continue;

            unsigned long long file_size = 0;
            std::error_code ec_size;
            file_size = fs::file_size(entry_path_abs, ec_size);
            if (ec_size) {
              // std::cerr << "WARNING: Cannot get file size for " <<
              // normAbsPathStr << ": " << ec_size.message() << ". Skipping
              // file.\n";
              continue;
            }

            if (is_file_extension_allowed(entry_path_abs, config.fileExtensions,
                                          config.excludedFileExtensions) &&
                !should_ignore_file(entry_path_abs, file_size,
                                    config.disableGitignore, base_abs_path,
                                    config.maxFileSizeB, config.ignoredFiles,
                                    dir_gitignore_rules_map) &&
                !matches_regex_filters(entry_path_abs, config.regexFilters) &&
                matches_filename_regex_filters(entry_path_abs,
                                               config.filenameRegexFilters)) {
              if (is_last_file(entry_path_abs, config)) {
                lastFilesList.push_back(entry_path_abs);
              } else {
                normalFiles.push_back(entry_path_abs);
              }
              collected_abs_paths_set.insert(normAbsPathStr);
            }
          }
        } catch (const fs::filesystem_error &fs_err) {
          std::cerr
              << "WARNING: Filesystem error during non-recursive scan near "
              << normalize_path(it->path()) << ": " << fs_err.what()
              << std::endl;
          // Cannot disable recursion here, just continue
        }
      }
    }

  } catch (const fs::filesystem_error &e) {
    std::cerr << "ERROR: Error scanning directory: "
              << normalize_path(config.dirPath) << ": " << e.what() << '\n';
  } catch (const std::exception &e) {
    std::cerr << "ERROR: Unexpected error during file collection: " << e.what()
              << '\n';
  }

  // Sort normal files alphabetically by absolute path for consistent processing
  // order
  std::sort(normalFiles.begin(), normalFiles.end());

  return {normalFiles, lastFilesList};
}

// --- File Processing ---

// Modified to store index and use tuple (Improvement 1)
void process_file_chunk(
    std::span<const fs::path> file_paths_chunk_abs, // Absolute paths
    size_t chunk_start_index,      // Starting index in the original sorted
                                   // `normalFiles`
    const Config &config,          // Pass config struct
    const fs::path &base_abs_path, // Base directory for relative path calcs
    std::vector<std::tuple<size_t, fs::path, std::string>>
        &thread_local_results, // Thread-local buffer (tuple: index, abs_path,
                               // content)
    std::atomic<size_t> &processed_files_counter,
    std::atomic<size_t> &total_bytes_counter,
    std::atomic<bool> &should_stop_flag) {
  thread_local_results.reserve(file_paths_chunk_abs.size()); // Pre-allocate

  for (size_t i = 0; i < file_paths_chunk_abs.size(); ++i) {
    if (should_stop_flag)
      break;

    const auto &absolute_path = file_paths_chunk_abs[i];
    size_t original_index =
        chunk_start_index + i; // Global index in sorted normalFiles

    try {
      std::string file_content_output =
          process_single_file(absolute_path, config, base_abs_path);

      // Check if file processing actually yielded output (non-empty, not just
      // header)
      bool content_exists = !file_content_output.empty();
      if (config.dryRun)
        content_exists = true; // In dry run, always keep the "processed" entry

      if (content_exists) {
        // Add file size to total only if not dry run
        if (!config.dryRun) {
          std::error_code ec_size;
          unsigned long long fsize = fs::file_size(absolute_path, ec_size);
          if (!ec_size) {
            total_bytes_counter += fsize;
          }
        }
        // Store tuple: original index, absolute path, formatted content
        thread_local_results.emplace_back(original_index, absolute_path,
                                          std::move(file_content_output));
      }
      processed_files_counter++; // Increment even if content is empty but
                                 // processing was attempted
    } catch (const std::exception &e) {
      // This error should be reported outside the thread or captured for later
      // display For now, just increment counter
      processed_files_counter++;
      // Optionally: Store error message associated with the index?
    } catch (...) {
      processed_files_counter++;
      // Optionally: Store generic error message?
    }
  }
}

// Uses stringstream buffering for output (Improvement 3)
// Expects absolute paths in last_files_list
void process_last_files(const std::vector<fs::path> &last_files_list_abs,
                        const Config &config, std::atomic<bool> &should_stop,
                        std::mutex &output_mutex, std::ostream &output_stream) {
  if (last_files_list_abs.empty())
    return;

  const fs::path base_abs_path = config.dirPath; // Base path is needed

  // Helper to get the sorting position based on --last arguments order
  auto get_sort_position = [&](const fs::path &absPath) -> int {
    fs::path relativePath;
    try {
      relativePath = fs::relative(absPath, base_abs_path);
    } catch (...) {
      return std::numeric_limits<int>::max();
    } // Put at end if error

    std::string relPathStr = normalize_path(relativePath);
    std::string filenameStr = normalize_path(absPath.filename());

    // Check explicit file list first (--last file.txt)
    for (size_t i = 0; i < config.lastFiles.size(); ++i) {
      std::string configEntryStr = normalize_path(config.lastFiles[i]);
      if (configEntryStr.find('/') != std::string::npos ||
          config.lastFiles[i].has_parent_path()) { // Matches relative path
        if (relPathStr == configEntryStr)
          return config.lastDirs.size() + i;
      } else { // Matches filename only
        if (filenameStr == configEntryStr)
          return config.lastDirs.size() + i;
      }
    }

    // Check explicit dir list (--last src/)
    for (size_t i = 0; i < config.lastDirs.size(); ++i) {
      std::string configDirRelStr = normalize_path(config.lastDirs[i]);
      // Ensure trailing slash for prefix matching dirs
      if (!configDirRelStr.empty() && configDirRelStr.back() != '/')
        configDirRelStr += '/';

      if (relPathStr.rfind(configDirRelStr, 0) == 0 ||
          relPathStr == normalize_path(config.lastDirs[i])) {
        return i; // Files within this dir get this group index
      }
    }
    // std::cerr << "WARNING: Could not determine sort position for last file: "
    // << normalize_path(absPath) << std::endl;
    return std::numeric_limits<int>::max(); // Should be found if collected
                                            // correctly, put at end otherwise
  };

  std::vector<fs::path> sorted_last_files = last_files_list_abs;
  std::sort(sorted_last_files.begin(), sorted_last_files.end(),
            [&](const fs::path &a, const fs::path &b) {
              int posA = get_sort_position(a);
              int posB = get_sort_position(b);
              if (posA != posB) {
                return posA < posB; // Sort by --last group index
              }
              // If in the same group, sort alphabetically by absolute path
              return normalize_path(a) < normalize_path(b);
            });

  // --- Improvement 3: Buffer output ---
  std::stringstream last_files_buffer;
  for (const auto &absolute_file_path : sorted_last_files) {
    if (should_stop)
      break;

    if (config.dryRun) {
      // In dry run, list relative path
      try {
        last_files_buffer << normalize_path(fs::relative(absolute_file_path,
                                                         base_abs_path))
                          << "\n";
      } catch (const std::exception &) {
        last_files_buffer << normalize_path(absolute_file_path.filename())
                          << " (relative path failed)\n";
      }
    } else {
      // Process the file and append its formatted output
      std::string file_output =
          process_single_file(absolute_file_path, config, base_abs_path);
      if (!file_output.empty()) {
        last_files_buffer << file_output;
      }
    }
  }

  // Write buffered output with a single lock
  if (last_files_buffer.tellp() > 0) { // Check if buffer has content
    std::lock_guard<std::mutex> lock(output_mutex);
    output_stream << last_files_buffer.str();
  }
  // --- End Improvement 3 ---
}

// --- Main Processing Functions ---

// Wrapper for single file processing used by main() if input is a file
bool process_single_file_entry(const Config &config,
                               std::ostream &output_stream) {
  if (!fs::is_regular_file(config.dirPath)) {
    std::cerr << "ERROR: process_single_file_entry called with non-file path: "
              << normalize_path(config.dirPath) << std::endl;
    return false;
  }
  const fs::path base_abs_path =
      config.dirPath.parent_path(); // Base is parent dir

  try {
    if (config.dryRun) {
      output_stream << "File to be processed:\n";
      // Output the original path provided by user? Or the absolute path? Let's
      // use absolute normalized.
      output_stream << normalize_path(config.dirPath) << "\n";
    } else {
      // Process the single file
      std::string file_content_output =
          process_single_file(config.dirPath, config, base_abs_path);
      if (!file_content_output.empty()) {
        output_stream << "# File generated by DirCat\n"; // Add header
        output_stream << file_content_output;
      } else {
        // File was skipped or empty, print message to cerr if outputting to
        // cout
        if (&output_stream == &std::cout) {
          std::cerr << "Input file processed but resulted in empty output."
                    << std::endl;
        }
      }
    }
  } catch (const std::exception &e) {
    std::cerr << "ERROR: Processing single file: "
              << normalize_path(config.dirPath) << ": " << e.what() << '\n';
    return false;
  }
  return true;
}

// Main function for processing a directory
// Uses optimized sort and chunk processing (Improvement 1)
bool process_directory(Config config, std::atomic<bool> &should_stop) {
  if (!fs::is_directory(config.dirPath)) {
    std::cerr << "ERROR: process_directory called with non-directory path: "
              << normalize_path(config.dirPath) << '\n';
    return false;
  }
  const fs::path base_abs_path = config.dirPath; // Already absolute

  // --- File Collection (uses optimized checks internally) ---
  auto [normalFilesAbs, lastFilesListAbs] = collect_files(config, should_stop);

  // --- Setup Output Stream ---
  std::ofstream outputFileStream;
  std::ostream *outputPtr = &std::cout;
  if (!config.outputFile.empty()) {
    fs::path absOutputPath = fs::absolute(config.outputFile);
    fs::path parentPath = absOutputPath.parent_path();
    if (!parentPath.empty() && !fs::exists(parentPath)) {
      try {
        fs::create_directories(parentPath);
        // Only print if outputting to console originally
        if (&std::cout == outputPtr) {
          std::cout << "Created output directory: "
                    << normalize_path(parentPath) << std::endl;
        }
      } catch (const std::exception &e) {
        std::cerr << "ERROR: Failed to create output directory "
                  << normalize_path(parentPath) << ": " << e.what() << '\n';
        return false;
      }
    }
    if (fs::exists(absOutputPath) && fs::is_directory(absOutputPath)) {
      std::cerr << "ERROR: Output path is an existing directory: "
                << normalize_path(absOutputPath) << '\n';
      return false;
    }
    // Open in binary mode and truncate
    outputFileStream.open(absOutputPath,
                          std::ios::binary | std::ios::out | std::ios::trunc);
    if (!outputFileStream.is_open()) {
      std::cerr << "ERROR: Could not open output file for writing: "
                << normalize_path(absOutputPath) << '\n';
      return false;
    }
    outputPtr = &outputFileStream;
  }
  std::ostream &output_stream = *outputPtr;

  // --- Dry Run Handling ---
  if (config.dryRun) {
    output_stream << "Files to be processed ("
                  << (normalFilesAbs.size() + lastFilesListAbs.size())
                  << " total):\n";
    output_stream << "--- Normal Files (" << normalFilesAbs.size() << ") ---\n";
    std::vector<std::string> normalRelativePaths;
    normalRelativePaths.reserve(normalFilesAbs.size());
    for (const auto &absFile : normalFilesAbs) {
      try {
        normalRelativePaths.push_back(
            normalize_path(fs::relative(absFile, base_abs_path)));
      } catch (...) {
        normalRelativePaths.push_back(normalize_path(absFile.filename()) +
                                      " (relative error)");
      }
    }
    std::sort(normalRelativePaths.begin(), normalRelativePaths.end());
    for (const auto &relPath : normalRelativePaths) {
      output_stream << relPath << "\n";
    }

    output_stream << "--- Last Files (" << lastFilesListAbs.size() << ") ---\n";
    std::mutex output_mutex_dry; // Dummy mutex needed for function signature
    process_last_files(lastFilesListAbs, config, should_stop, output_mutex_dry,
                       output_stream);
    return true; // Dry run finished
  }

  // --- Actual Processing ---
  if (normalFilesAbs.empty() && lastFilesListAbs.empty()) {
    // Print message to cerr if outputting to cout
    if (outputPtr == &std::cout) {
      std::cerr << "No matching files found in: "
                << normalize_path(config.dirPath) << "\n";
    }
    return true;
  }

  // --- Improvement 1: Use tuple for results (index, abs_path, content) ---
  std::vector<std::tuple<size_t, fs::path, std::string>> orderedResults;
  orderedResults.reserve(normalFilesAbs.size());
  // --- End Improvement 1 ---

  output_stream << "# File generated by DirCat\n";

  std::atomic<size_t> processedFiles{0};
  std::atomic<size_t> totalBytes{0};
  std::mutex output_mutex; // Mutex for final output stream writing AND for cerr
                           // in threads

  unsigned int num_threads = std::thread::hardware_concurrency();
  num_threads = std::max(
      1u, std::min(num_threads ? num_threads : 1u, 16u)); // Ensure >= 1, max 16

  std::vector<std::thread> threads;
  threads.reserve(num_threads);
  const size_t total_normal_files = normalFilesAbs.size();
  const size_t files_per_thread =
      (total_normal_files == 0)
          ? 0
          : (total_normal_files + num_threads - 1) / num_threads;

  // Vector to hold thread-local results before merging
  std::vector<std::vector<std::tuple<size_t, fs::path, std::string>>>
      thread_results(num_threads);

  for (unsigned int i = 0; i < num_threads && files_per_thread > 0; ++i) {
    const size_t start_index = i * files_per_thread;
    if (start_index >= total_normal_files)
      break;
    const size_t end_index =
        std::min(start_index + files_per_thread, total_normal_files);
    std::span<const fs::path> chunk_span = {normalFilesAbs.data() + start_index,
                                            end_index - start_index};

    threads.emplace_back(
        // FIX: Capture output_mutex by reference for cerr locking
        [&config, &base_abs_path, &processedFiles, &totalBytes, &should_stop,
         &results_buffer = thread_results[i], &output_mutex](
            std::span<const fs::path> chunk, size_t chunk_idx_start) {
          try {
            process_file_chunk(chunk, chunk_idx_start, config, base_abs_path,
                               results_buffer, processedFiles, totalBytes,
                               should_stop);
          } catch (const std::exception &e) {
            std::lock_guard<std::mutex> lock(output_mutex); // Lock cerr
            std::cerr << "ERROR: Exception in processing thread: " << e.what()
                      << '\n';
          } catch (...) {
            std::lock_guard<std::mutex> lock(output_mutex);
            std::cerr << "ERROR: Unknown exception in processing thread.\n";
          }
        },
        chunk_span, start_index);
  }

  // --- Join Threads and Merge Results ---
  for (auto &thread : threads) {
    if (thread.joinable())
      thread.join();
  }
  // Merge results into the main vector
  for (const auto &local_results : thread_results) {
    // Use move iterators for potentially better performance transferring
    // strings
    orderedResults.insert(orderedResults.end(),
                          std::make_move_iterator(local_results.begin()),
                          std::make_move_iterator(local_results.end()));
  }
  thread_results.clear(); // Free memory

  // --- Improvement 1: Sort based on original index ---
  std::sort(orderedResults.begin(), orderedResults.end(),
            [](const auto &a, const auto &b) {
              return std::get<0>(a) < std::get<0>(b);
            });
  // --- End Improvement 1 ---

  // --- Write Sorted Normal File Results ---
  {
    std::lock_guard<std::mutex> lock(
        output_mutex); // Lock for writing normal files batch
    for (const auto &result_tuple : orderedResults) {
      if (should_stop)
        break; // Check stop flag before writing each file's content
      output_stream << std::get<2>(
          result_tuple); // Write the formatted string content
    }
  }

  // --- Process and Write Last Files (handles own locking/buffering) ---
  if (!should_stop) {
    process_last_files(lastFilesListAbs, config, should_stop, output_mutex,
                       output_stream);
  }

  // --- Cleanup & Reporting ---
  std::string final_message;
  std::stringstream ss_msg;
  ss_msg << "Processed " << processedFiles.load() << " files (" << std::fixed
         << std::setprecision(2) << (totalBytes.load() / (1024.0 * 1024.0))
         << " MiB total).\n";

  if (outputFileStream.is_open()) {
    outputFileStream.close();
    if (!outputFileStream) { // Check for errors on close
      std::cerr << "ERROR: Failed to write to output file: "
                << normalize_path(config.outputFile) << std::endl;
      return false; // Indicate failure if output write failed
    }
    ss_msg << "Output written to: "
           << normalize_path(fs::absolute(config.outputFile)) << std::endl;
    final_message = ss_msg.str();
    // Print final message to console even if output went to file
    std::cout << final_message;

  } else {
    // Print stats to console if output was stdout
    // FIX: Use std::ios_base::end instead of std::ios_end
    ss_msg.seekp(0, std::ios_base::end); // Ensure writing at the end
    ss_msg << "Output sent to stdout." << std::endl;
    final_message = ss_msg.str();
    // Use cerr for the final status message if output was stdout, to avoid
    // mixing with content
    std::cerr << "\n---\n" << final_message;
  }

  return true;
}

// --- Signal Handling ---

std::atomic<bool> *globalShouldStop = nullptr;
void signalHandler(int signum) {
  if (globalShouldStop && !globalShouldStop->load()) {
    // Use cerr for signal message so it doesn't mix with potential stdout
    // output
    std::cerr << "\nInterrupt signal (" << signum
              << ") received, stopping gracefully...\n";
    *globalShouldStop = true;
  } else {
    std::cerr << "\nInterrupt signal (" << signum
              << ") received again, forcing exit.\n";
    std::exit(128 + signum); // Standard exit code for signals
  }
}

// --- Argument Parsing (Adds population of Config sets - Improvement 4) ---

Config parse_arguments(int argc, char *argv[]) {
  Config config;

  std::function<void()> print_usage;
  // (Keep the print_usage lambda from the thought process step)
  auto print_usage_internal = [&]() {
    std::cerr << "Usage: " << argv[0]
              << " <directory_path | file_path> [options]\n";
    std::cerr
        << "Concatenates files in a directory based on specified criteria.\n\n";
    std::cerr << "Options:\n";

    std::vector<std::pair<std::string, std::string>> options = {
        {"-m, --max-size <bytes>", "Exclude files larger than <bytes> (e.g., "
                                   "1048576, 1M, 1G). Default: no limit."},
        {"-n, --no-recursive", "Disable recursive directory search."},
        {"-e, --ext <ext...>", "Include only files with these extensions "
                               "(lowercase, no dot, e.g., -e cpp h hpp)."},
        {"-x, --exclude-ext <ext...>",
         "Exclude files with these extensions (lowercase, no dot, e.g., -x log "
         "tmp)."},
        {"-i, --ignore <item...>",
         "Ignore specific files or folders relative to the base directory "
         "(e.g., -i build node_modules/ secret.key). Folder ignores should end "
         "with '/'. Uses gitignore-style matching."},
        {"-r, --regex <pattern...>",
         "Exclude files whose *filename* matches any specified regex pattern "
         "(case-sensitive)."},
        {"-d, --filename-regex <pattern...>",
         "Include only files whose *filename* matches any specified regex "
         "pattern (case-sensitive)."},
        {"-c, --remove-comments",
         "Attempt to remove C-style comments (//, /* */)."},
        {"-l, --remove-empty-lines",
         "Remove empty lines (containing only whitespace) from output."},
        {"-f, --filename-only",
         "Show only filename (not relative path) in '## File:' headers."},
        {"-L, --line-numbers",
         "Prepend line numbers (1 | ...) to each line of file content."},
        {"-t, --no-gitignore", "Disable processing of .gitignore files."},
        {"-z, --last <item...>",
         "Process specified files/directories last. Order is preserved. Items "
         "are matched relative to the base directory (filename or relative "
         "path)."},
        {"-Z, --only-last", "Only process files/directories specified with "
                            "--last. Ignores all other files."},
        {"-o, --output <file>", "Write output to <file> instead of stdout."},
        {"-D, --dry-run",
         "List files that would be processed, without concatenating content."},
        {"-h, --help", "Show this help message."}};

    size_t max_option_length = 0;
    for (const auto &option : options) {
      max_option_length = std::max(max_option_length, option.first.length());
    }

    for (const auto &option : options) {
      std::cerr << "  " << std::left << std::setw(max_option_length + 2)
                << option.first << option.second << "\n";
    }
  };
  print_usage =
      print_usage_internal; // Assign internal lambda to outer variable

  if (argc < 2) {
    print_usage();
    exit(1);
  }
  for (int i = 1; i < argc; ++i) {
    if (std::string_view(argv[i]) == "-h" ||
        std::string_view(argv[i]) == "--help") {
      print_usage();
      exit(0);
    }
  }

  try {
    config.dirPath = fs::absolute(argv[1]); // Use absolute path internally
    if (!fs::exists(config.dirPath)) {
      std::cerr << "ERROR: Input path does not exist: "
                << normalize_path(config.dirPath) << '\n';
      exit(1);
    }
  } catch (const std::exception &e) {
    std::cerr << "ERROR: Invalid input path '" << argv[1] << "': " << e.what()
              << '\n';
    exit(1);
  }

  for (int i = 2; i < argc; ++i) {
    std::string_view arg = argv[i];
    std::function<void(std::vector<std::string> &)> parse_multi_arg;
    std::function<void(std::vector<fs::path> &)> parse_multi_path_arg;
    // (Keep the multi-arg helpers from thought process)
    auto parse_multi_arg_internal = [&](std::vector<std::string> &target_vec) {
      while (i + 1 < argc && argv[i + 1][0] != '-') {
        target_vec.emplace_back(argv[++i]);
      }
    };
    parse_multi_arg = parse_multi_arg_internal;
    auto parse_multi_path_arg_internal =
        [&](std::vector<fs::path> &target_vec) {
          while (i + 1 < argc && argv[i + 1][0] != '-') {
            target_vec.emplace_back(argv[++i]); // Store as path object
          }
        };
    parse_multi_path_arg = parse_multi_path_arg_internal;

    if ((arg == "-m" || arg == "--max-size") &&
        i + 1 < argc) { /* ... (Size parsing with K/M/G same as before) ... */
      std::string size_str = argv[++i];
      try {
        unsigned long long multiplier = 1;
        if (!size_str.empty()) {
          // Handle potential negative sign before suffix check
          bool negative = size_str[0] == '-';
          if (negative)
            throw std::invalid_argument("Size cannot be negative");

          char suffix = std::toupper(size_str.back());
          if (!std::isdigit(static_cast<unsigned char>(
                  suffix))) { // Check if last char is non-digit
            if (suffix == 'K') {
              multiplier = 1024ULL;
              size_str.pop_back();
            } else if (suffix == 'M') {
              multiplier = 1024ULL * 1024ULL;
              size_str.pop_back();
            } else if (suffix == 'G') {
              multiplier = 1024ULL * 1024ULL * 1024ULL;
              size_str.pop_back();
            } else {
              throw std::invalid_argument("Invalid size suffix (use K, M, G)");
            }
          }
        }

        if (!size_str.empty()) {
          // Use stoull for unsigned long long
          config.maxFileSizeB = std::stoull(size_str) * multiplier;
        } else if (multiplier > 1) { // Handle cases like "-m M" meaning 1M
          config.maxFileSizeB = multiplier;
        } else {
          throw std::invalid_argument("Empty size value");
        }
      } catch (const std::exception &e) {
        std::cerr << "ERROR: Invalid max-size value: '" << argv[i]
                  << "'. Use positive integer bytes or suffix K/M/G. Error: "
                  << e.what() << "\n";
        exit(1);
      }
    } else if (arg == "-n" || arg == "--no-recursive") {
      config.recursiveSearch = false;
    } else if (arg == "-e" || arg == "--ext") {
      std::vector<std::string> extensions;
      parse_multi_arg(extensions);
      for (auto &ext : extensions) {
        if (!ext.empty() && ext[0] == '.')
          ext.erase(0, 1);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (!ext.empty())
          config.fileExtensions.push_back(std::move(ext));
      }
    } else if (arg == "-x" || arg == "--exclude-ext") {
      std::vector<std::string> extensions;
      parse_multi_arg(extensions);
      for (auto &ext : extensions) {
        if (!ext.empty() && ext[0] == '.')
          ext.erase(0, 1);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (!ext.empty())
          config.excludedFileExtensions.push_back(std::move(ext));
      }
    } else if (arg == "-i" || arg == "--ignore") {
      std::vector<fs::path> items;
      parse_multi_path_arg(items);
      for (auto &item : items) {
        // Store raw paths, logic in should_ignore_* handles folder/file
        std::string norm_item = normalize_path(item);
        if (!norm_item.empty() && norm_item.back() == '/') {
          config.ignoredFolders.push_back(
              item.lexically_normal()); // Store normalized folder path
        } else {
          config.ignoredFiles.push_back(item); // Store file path/pattern
        }
      }
    } else if (arg == "-r" || arg == "--regex") {
      parse_multi_arg(config.regexFilters);
    } else if (arg == "-d" || arg == "--filename-regex") {
      parse_multi_arg(config.filenameRegexFilters);
    } else if (arg == "-c" || arg == "--remove-comments") {
      config.removeComments = true;
    } else if (arg == "-l" || arg == "--remove-empty-lines") {
      config.removeEmptyLines = true;
    } else if (arg == "-f" || arg == "--filename-only") {
      config.showFilenameOnly = true;
    } else if (arg == "-L" || arg == "--line-numbers") {
      config.showLineNumbers = true;
    } else if (arg == "-t" || arg == "--no-gitignore") {
      config.disableGitignore = true;
    } else if (arg == "-z" || arg == "--last") {
      std::vector<fs::path> items;
      parse_multi_path_arg(items);
      // Store both files and dirs in combined lists first
      for (auto &item : items) {
        std::string norm_item = normalize_path(item);
        if (!norm_item.empty() && norm_item.back() == '/') {
          config.lastDirs.push_back(item);
        } else {
          config.lastFiles.push_back(item);
        }
      }
    } else if (arg == "-Z" || arg == "--only-last") {
      config.onlyLast = true;
    } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
      config.outputFile = argv[++i];
    } else if (arg == "-D" || arg == "--dry-run") {
      config.dryRun = true;
    } else {
      std::cerr << "ERROR: Unknown or invalid option: " << arg << "\n\n";
      print_usage();
      exit(1);
    }
  }

  // --- Improvement 4: Populate sets for --last lookups ---
  // Populate sets regardless of whether input is file or dir, checked later
  for (const auto &p : config.lastFiles) {
    std::string normPathStr = normalize_path(p);
    // Heuristic: if it contains '/' treat as relative, else filename
    if (normPathStr.find('/') != std::string::npos || p.has_parent_path()) {
      config.lastFilesSetRel.insert(normPathStr);
    } else {
      config.lastFilesSetFilename.insert(normPathStr);
    }
  }
  for (const auto &p : config.lastDirs) {
    config.lastDirsSetRel.insert(normalize_path(p));
  }

  // --- Final Validation ---
  if (config.onlyLast && config.lastFiles.empty() && config.lastDirs.empty()) {
    std::cerr
        << "ERROR: --only-last specified, but no items provided via --last.\n";
    exit(1);
  }
  if (config.onlyLast && !fs::is_directory(config.dirPath)) {
    std::cerr << "ERROR: --only-last option requires the input path to be a "
                 "directory.\n";
    exit(1);
  }

  return config;
}