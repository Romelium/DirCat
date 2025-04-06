#include "lib.cpp" // Include the implementation directly for testing private/static functions if needed (though mostly using public interface now)

#include <cassert>
#include <filesystem> // Already included via lib.cpp but good practice
#include <functional> // For std::function
#include <iostream>   // For std::cerr, std::cout, std::endl
#include <sstream>    // For std::stringstream
#include <string>
#include <vector>

// Define TEST_DIR globally or pass it around
const std::string TEST_DIR_NAME = "test_dir_dircat"; // Use a unique name
const fs::path TEST_DIR_PATH = fs::absolute(TEST_DIR_NAME);

const std::string TEST_GITIGNORE_DIR_NAME = "test_dir_gitignore_dircat";
const fs::path TEST_GITIGNORE_DIR_PATH = fs::absolute(TEST_GITIGNORE_DIR_NAME);

// --- Helper Functions for Testing ---

// Cleans up test directories
void cleanup_test_directories() {
  std::error_code ec;
  fs::remove_all(TEST_DIR_PATH, ec);
  fs::remove_all(TEST_GITIGNORE_DIR_PATH, ec);
  fs::remove("test_output.txt", ec); // Clean up potential output files
  fs::remove("dry_run_output.txt", ec);
  fs::remove("single_file_output.txt", ec);
  // Clear static caches between test runs if necessary (can affect gitignore
  // tests)
  gitignore_rules_cache.clear();
  accumulated_rules_cache.clear();
  regex_cache.clear();
}

// Creates a test file, ensuring parent directory exists
void create_test_file(const fs::path &absolute_path,
                      const std::string &content) {
  try {
    if (absolute_path.has_parent_path()) {
      fs::create_directories(absolute_path.parent_path());
    }
    std::ofstream file(absolute_path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
      std::cerr << "Error creating test file: " << normalize_path(absolute_path)
                << std::endl;
      return;
    }
    file << content;
  } catch (const std::exception &e) {
    std::cerr << "Exception creating test file "
              << normalize_path(absolute_path) << ": " << e.what() << std::endl;
  }
}

// Creates the main test directory structure
void create_test_directory_structure() {
  cleanup_test_directories(); // Clean first
  fs::create_directories(TEST_DIR_PATH / "subdir1");
  fs::create_directories(TEST_DIR_PATH / "subdir2");
  fs::create_directories(TEST_DIR_PATH / ".hidden_dir");
  fs::create_directories(TEST_DIR_PATH / "ignored_folder");
  fs::create_directories(TEST_DIR_PATH / "not_ignored_folder");

  create_test_file(TEST_DIR_PATH / "file1.cpp",
                   "// C++ file\nint main() { return 0; }");
  create_test_file(TEST_DIR_PATH / "file2.txt",
                   "Text file content\n"); // Ignored by gitignore
  create_test_file(TEST_DIR_PATH / "FILE3.HPP",
                   "// Header file\n#define FILE3_HPP\n");
  create_test_file(TEST_DIR_PATH / "file4.excluded",
                   "Excluded ext file\n"); // Matches -x excluded
  create_test_file(TEST_DIR_PATH / "file5",
                   "No extension file\n"); // No extension
  create_test_file(TEST_DIR_PATH / "subdir1" / "file6.cpp", "// Subdir file\n");
  create_test_file(TEST_DIR_PATH / ".hidden_file.cpp",
                   "// Hidden file\n"); // Should not be ignored by default
  create_test_file(
      TEST_DIR_PATH / "ignored_folder" / "file7.cpp",
      "// Ignored folder file\n"); // Ignored by folder name and gitignore
  create_test_file(TEST_DIR_PATH / "large_file.cpp",
                   std::string(2049, 'L')); // > 2048 bytes
  create_test_file(TEST_DIR_PATH / ".gitignore",
                   "*.txt\n.hidden_dir/\nignored_folder/\nlarge_file.cpp\n");
  create_test_file(TEST_DIR_PATH / "not_ignored_folder" / "file8.cpp",
                   "// Not ignored\n");
  create_test_file(TEST_DIR_PATH / "file_abc.cpp",
                   "// abc file\n"); // For filename regex tests
  create_test_file(TEST_DIR_PATH / "file_def.cpp",
                   "// def file\n"); // For filename regex tests
  create_test_file(TEST_DIR_PATH / "misc.data",
                   "misc data file\n"); // Another extension
}

// Creates directory structure for multi-level gitignore tests
void create_test_directory_gitignore_structure() {
  cleanup_test_directories(); // Clean first
  fs::create_directories(TEST_GITIGNORE_DIR_PATH / "subdir1");
  fs::create_directories(TEST_GITIGNORE_DIR_PATH / "subdir2");
  fs::create_directories(TEST_GITIGNORE_DIR_PATH / "subdir1" / "subsubdir");

  create_test_file(TEST_GITIGNORE_DIR_PATH / ".gitignore", "*.level1\n");
  create_test_file(TEST_GITIGNORE_DIR_PATH / "subdir1" / ".gitignore",
                   "*.level2\n!important.level2\nsubsubdir/\n");
  create_test_file(TEST_GITIGNORE_DIR_PATH / "file_root.level0",
                   "level0 file in root\n");
  create_test_file(TEST_GITIGNORE_DIR_PATH / "file_root.level1",
                   "level1 file in root\n"); // Ignored by root
  create_test_file(TEST_GITIGNORE_DIR_PATH / "subdir1" / "file_sub1.level1",
                   "level1 file in subdir1\n"); // Ignored by root
  create_test_file(TEST_GITIGNORE_DIR_PATH / "subdir1" / "file_sub1.level2",
                   "level2 file in subdir1\n"); // Ignored by subdir1
  create_test_file(
      TEST_GITIGNORE_DIR_PATH / "subdir1" / "important.level2",
      "important level2 file in subdir1\n"); // Explicitly not ignored
  create_test_file(TEST_GITIGNORE_DIR_PATH / "subdir2" / "file_sub2.level1",
                   "level1 file in subdir2\n"); // Ignored by root
  create_test_file(
      TEST_GITIGNORE_DIR_PATH / "subdir2" / "file_sub2.level2",
      "level2 file in subdir2 (no subdir gitignore)\n"); // Not ignored
  create_test_file(
      TEST_GITIGNORE_DIR_PATH / "subdir1" / "subsubdir" / "file_subsub.txt",
      "ignored dir content\n"); // Ignored by subdir1 ignore dir rule
}

// Captures stdout
std::string capture_stdout(const std::function<void()> &func) {
  std::stringstream buffer;
  std::streambuf *oldCout = std::cout.rdbuf();
  std::cout.rdbuf(buffer.rdbuf());
  func();
  std::cout.rdbuf(oldCout);
  return buffer.str();
}

// Helper to create a default Config for tests
Config get_default_config(const fs::path &base_path) {
  Config config;
  config.dirPath = base_path; // Expect absolute path
  // Set other defaults matching lib.cpp initial values if needed
  config.recursiveSearch = true;
  config.disableGitignore = false;
  //... etc.
  return config;
}

// Helper to build the gitignore rule map for a directory
std::unordered_map<std::string, std::vector<std::string>>
build_gitignore_map(const fs::path &base_abs_path) {
  std::unordered_map<std::string, std::vector<std::string>> rules_map;
  // Simulate the map creation done in collect_files
  try {
    if (fs::exists(base_abs_path / ".gitignore")) {
      rules_map[normalize_path(base_abs_path)] =
          load_gitignore_rules(base_abs_path / ".gitignore");
    }
    fs::recursive_directory_iterator gitignore_it(
        base_abs_path, fs::directory_options::skip_permission_denied);
    fs::recursive_directory_iterator end_gitignore_it;
    for (; gitignore_it != end_gitignore_it; ++gitignore_it) {
      if (!gitignore_it->is_directory() &&
          gitignore_it->path().filename() == ".gitignore") {
        std::string dir_key =
            normalize_path(gitignore_it->path().parent_path());
        // Load only if not already loaded (e.g. base path)
        if (rules_map.find(dir_key) == rules_map.end()) {
          rules_map[dir_key] = load_gitignore_rules(gitignore_it->path());
        }
      }
    }
  } catch (const std::exception &e) {
    std::cerr << "WARNING (Test Helper): Error building gitignore map for "
              << normalize_path(base_abs_path) << ": " << e.what() << std::endl;
  }
  return rules_map;
}

// --- Test Functions ---

void test_trim() {
  std::cout << "Test: Trim..." << std::flush;
  assert(trim("  hello  ") == "hello");
  assert(trim("\tworld\n") == "world");
  assert(trim("no whitespace") == "no whitespace");
  assert(trim("") == "");
  std::cout << " Passed\n";
}

void test_load_gitignore_rules() {
  std::cout << "Test: Load gitignore rules..." << std::flush;
  fs::path temp_gitignore = TEST_DIR_PATH / ".gitignore_test_load";
  create_test_file(temp_gitignore,
                   "*.temp\n# comment\ndir/\n!important.temp\n");
  std::vector<std::string> rules = load_gitignore_rules(temp_gitignore);
  assert(rules.size() == 3);
  assert(rules[0] == "*.temp");
  assert(rules[1] == "dir/");
  assert(rules[2] == "!important.temp");
  fs::remove(temp_gitignore);
  std::cout << " Passed\n";
}

void test_is_path_ignored_by_gitignore() {
  std::cout << "Test: Is path ignored by single gitignore..." << std::flush;
  create_test_directory_structure(); // Ensure files exist
  fs::path base_abs = TEST_DIR_PATH;
  auto rules_map = build_gitignore_map(base_abs);

  // Test cases
  assert(is_path_ignored_by_gitignore(base_abs / "file2.txt", base_abs,
                                      rules_map) == true); // *.txt
  assert(is_path_ignored_by_gitignore(base_abs / "file1.cpp", base_abs,
                                      rules_map) == false); // Not ignored
  assert(is_path_ignored_by_gitignore(base_abs / ".hidden_dir" / "somefile.txt",
                                      base_abs,
                                      rules_map) == true); // .hidden_dir/
  assert(is_path_ignored_by_gitignore(base_abs / "ignored_folder" / "file7.cpp",
                                      base_abs,
                                      rules_map) == true); // ignored_folder/
  assert(is_path_ignored_by_gitignore(
             base_abs / "not_ignored_folder" / "file8.cpp", base_abs,
             rules_map) == false); // Not ignored
  assert(is_path_ignored_by_gitignore(base_abs / "large_file.cpp", base_abs,
                                      rules_map) ==
         true); // Explicitly ignored file
  assert(is_path_ignored_by_gitignore(base_abs / ".git/config", base_abs,
                                      rules_map) ==
         true); // Implicitly ignore .git

  std::cout << " Passed\n";
}

void test_is_path_ignored_by_gitignore_multi_level() {
  std::cout << "Test: Is path ignored by multi-level gitignore..."
            << std::flush;
  create_test_directory_gitignore_structure();
  fs::path base_abs = TEST_GITIGNORE_DIR_PATH;
  auto rules_map = build_gitignore_map(base_abs); // Builds map by scanning

  auto check_ignored = [&](const fs::path &relative_path_from_base) {
    return is_path_ignored_by_gitignore(base_abs / relative_path_from_base,
                                        base_abs, rules_map);
  };

  assert(check_ignored("file_root.level1") ==
         true); // Ignored by root .gitignore
  assert(check_ignored("subdir1/file_sub1.level1") ==
         true); // Ignored by root .gitignore
  assert(check_ignored("subdir1/file_sub1.level2") ==
         true); // Ignored by subdir1 .gitignore
  assert(check_ignored("subdir1/important.level2") ==
         false); // Negated by subdir1 .gitignore
  assert(check_ignored("subdir2/file_sub2.level1") ==
         true); // Ignored by root .gitignore
  assert(check_ignored("subdir2/file_sub2.level2") == false); // No rule matches
  assert(check_ignored("subdir1/subsubdir/file_subsub.txt") ==
         true); // subsubdir/ rule in subdir1

  std::cout << " Passed\n";
}

void test_is_file_size_valid() {
  std::cout << "Test: Is file size valid..." << std::flush;
  // Function signature changed, test needs to provide size
  assert(is_file_size_valid(5, 1024) == true);
  assert(is_file_size_valid(1024, 1024) == true);
  assert(is_file_size_valid(1025, 1024) == false);
  assert(is_file_size_valid(5, 0) == true); // max_size 0 means no limit
  std::cout << " Passed\n";
}

void test_is_file_extension_allowed() {
  std::cout << "Test: Is file extension allowed..." << std::flush;
  std::vector<std::string> allowed_exts = {"cpp", "hpp"}; // lowercase, no dot
  std::vector<std::string> excluded_exts = {"excluded"};  // lowercase, no dot

  assert(is_file_extension_allowed(TEST_DIR_PATH / "file.cpp", allowed_exts,
                                   excluded_exts) == true);
  assert(is_file_extension_allowed(TEST_DIR_PATH / "file.CPP", allowed_exts,
                                   excluded_exts) ==
         true); // Case insensitive check
  assert(is_file_extension_allowed(TEST_DIR_PATH / "file.hpp", allowed_exts,
                                   excluded_exts) == true);
  assert(is_file_extension_allowed(TEST_DIR_PATH / "file.txt", allowed_exts,
                                   excluded_exts) == false);
  assert(is_file_extension_allowed(TEST_DIR_PATH / "file.excluded",
                                   allowed_exts, excluded_exts) ==
         false); // Explicitly excluded
  assert(is_file_extension_allowed(TEST_DIR_PATH / "file.excluded", {}, {}) ==
         true); // Allowed if no rules
  assert(is_file_extension_allowed(TEST_DIR_PATH / "file", allowed_exts,
                                   excluded_exts) ==
         false); // No extension, but allowed_exts is not empty
  assert(is_file_extension_allowed(TEST_DIR_PATH / "file", {}, excluded_exts) ==
         true); // No extension, allowed_exts is empty
  assert(is_file_extension_allowed(TEST_DIR_PATH / "file.", allowed_exts,
                                   excluded_exts) ==
         false); // Only dot, no extension

  std::cout << " Passed\n";
}

void test_should_ignore_folder() {
  std::cout << "Test: Should ignore folder..." << std::flush;
  create_test_directory_structure();
  fs::path base_abs = TEST_DIR_PATH;
  auto rules_map = build_gitignore_map(base_abs);
  Config config = get_default_config(base_abs); // Use default config

  // Gitignored folders
  assert(should_ignore_folder(base_abs / ".hidden_dir", config.disableGitignore,
                              base_abs, config.ignoredFolders,
                              rules_map) == true);
  assert(should_ignore_folder(base_abs / "ignored_folder",
                              config.disableGitignore, base_abs,
                              config.ignoredFolders, rules_map) == true);
  // Not ignored folder
  assert(should_ignore_folder(base_abs / "not_ignored_folder",
                              config.disableGitignore, base_abs,
                              config.ignoredFolders, rules_map) == false);
  // Manually ignored folder (relative path)
  config.ignoredFolders.push_back("subdir1"); // Add relative path
  assert(should_ignore_folder(base_abs / "subdir1", config.disableGitignore,
                              base_abs, config.ignoredFolders,
                              rules_map) == true);
  // Nested under manually ignored folder
  assert(should_ignore_folder(base_abs / "subdir1" / "subsub",
                              config.disableGitignore, base_abs,
                              config.ignoredFolders, rules_map) == true);

  std::cout << " Passed\n";
}

void test_should_ignore_file() {
  std::cout << "Test: Should ignore file..." << std::flush;
  create_test_directory_structure();
  fs::path base_abs = TEST_DIR_PATH;
  auto rules_map = build_gitignore_map(base_abs);
  Config config = get_default_config(base_abs);
  config.maxFileSizeB = 2048; // Set max size

  // Get file sizes manually for the test
  unsigned long long size_file1_cpp = fs::file_size(base_abs / "file1.cpp");
  unsigned long long size_file2_txt = fs::file_size(base_abs / "file2.txt");
  unsigned long long size_large_file = fs::file_size(
      base_abs / "large_file.cpp"); // This one is ignored by gitignore anyway

  fs::path ignore_me_rel = "subdir1/ignore_me.txt";
  create_test_file(base_abs / ignore_me_rel, "ignore this content");
  unsigned long long size_ignore_me = fs::file_size(base_abs / ignore_me_rel);

  fs::path large_ignore_rel = "subdir1/large_ignore.cpp";
  create_test_file(base_abs / large_ignore_rel, std::string(4096, 'I'));
  unsigned long long size_large_ignore =
      fs::file_size(base_abs / large_ignore_rel);

  // Gitignored file (*.txt)
  assert(should_ignore_file(base_abs / "file2.txt", size_file2_txt,
                            config.disableGitignore, base_abs,
                            config.maxFileSizeB, config.ignoredFiles,
                            rules_map) == true);
  // Manually ignored file (relative path)
  config.ignoredFiles.push_back(ignore_me_rel);
  assert(should_ignore_file(base_abs / ignore_me_rel, size_ignore_me,
                            config.disableGitignore, base_abs,
                            config.maxFileSizeB, config.ignoredFiles,
                            rules_map) == true);
  // Manually ignored file (filename only)
  config.ignoredFiles.clear();
  config.ignoredFiles.push_back("ignore_me.txt"); // filename
  assert(should_ignore_file(base_abs / ignore_me_rel, size_ignore_me,
                            config.disableGitignore, base_abs,
                            config.maxFileSizeB, config.ignoredFiles,
                            rules_map) == true);
  config.ignoredFiles.clear();
  // File exceeding max size
  assert(should_ignore_file(base_abs / large_ignore_rel, size_large_ignore,
                            config.disableGitignore, base_abs,
                            config.maxFileSizeB, config.ignoredFiles,
                            rules_map) == true);
  // Normal file, not ignored
  assert(should_ignore_file(base_abs / "file1.cpp", size_file1_cpp,
                            config.disableGitignore, base_abs,
                            config.maxFileSizeB, config.ignoredFiles,
                            rules_map) == false);

  std::cout << " Passed\n";
}

void test_matches_regex_filters() {
  std::cout << "Test: Matches regex filters (exclude)..." << std::flush;
  std::vector<std::string> regex_filters = {"file[0-9]\\.txt", "^large_"};
  assert(matches_regex_filters(TEST_DIR_PATH / "file2.txt", regex_filters) ==
         true);
  assert(matches_regex_filters(TEST_DIR_PATH / "large_file.cpp",
                               regex_filters) == true);
  assert(matches_regex_filters(TEST_DIR_PATH / "file1.cpp", regex_filters) ==
         false);
  assert(matches_regex_filters(TEST_DIR_PATH / "no_match.txt", {}) ==
         false); // No filters = no match
  std::cout << " Passed\n";
}

void test_matches_filename_regex_filters() {
  std::cout << "Test: Matches filename regex filters (include)..."
            << std::flush;
  std::vector<std::string> filename_regex_filters = {
      ".*\\.cpp", "FILE.*"}; // Match all cpp or starting with FILE
  assert(matches_filename_regex_filters(TEST_DIR_PATH / "file1.cpp",
                                        filename_regex_filters) == true);
  assert(matches_filename_regex_filters(TEST_DIR_PATH / "subdir1/file6.cpp",
                                        filename_regex_filters) == true);
  assert(matches_filename_regex_filters(TEST_DIR_PATH / "FILE3.HPP",
                                        filename_regex_filters) == true);
  assert(matches_filename_regex_filters(TEST_DIR_PATH / "file2.txt",
                                        filename_regex_filters) == false);
  assert(matches_filename_regex_filters(TEST_DIR_PATH / "file5",
                                        filename_regex_filters) ==
         false); // No extension
  assert(matches_filename_regex_filters(TEST_DIR_PATH / "file1.cpp", {}) ==
         true); // No filters = include all
  std::cout << " Passed\n";
}

void test_remove_cpp_comments() {
  std::cout << "Test: Remove cpp comments..." << std::flush;
  std::string code_with_comments =
      "// Line comment\nint /* block */ main(/*arg*/) {\n std::cout << \"//Not "
      "a comment /* neither */\"; // End comment\n}";
  std::string expected =
      "\nint  main() {\n std::cout << \"//Not a comment /* neither */\"; \n}";
  std::string result = remove_cpp_comments(code_with_comments);
  // std::cout << "\nExpected: " << expected << "\nActual:   " << result <<
  // std::endl; // Debug print
  assert(result == expected);
  // Check that specific commented-out text is gone
  assert(result.find("Line comment") == std::string::npos);
  assert(result.find("block") == std::string::npos); // This one is valid
  assert(result.find("End comment") == std::string::npos);
  // Check that string literal content remains
  assert(result.find("\"//Not a comment /* neither */\"") != std::string::npos);
  std::cout << " Passed\n";
}

void test_format_file_output() {
  std::cout << "Test: Format file output..." << std::flush;
  fs::path base_abs = TEST_DIR_PATH;
  fs::path file_abs = base_abs / "subdir1" / "output_test.cpp";
  std::string content = "Line 1\nLine 2\r\n\nLine 4";
  std::string formatted_output =
      format_file_output(file_abs, false, base_abs, content, false, false);
  std::string expected_output =
      "\n## File: subdir1/output_test.cpp\n\n```cpp\nLine 1\nLine 2\n\nLine "
      "4\n```\n";
  // std::cout << "\nExpected:\n" << expected_output << "\nActual:\n" <<
  // formatted_output << std::endl; // Debug
  assert(formatted_output == expected_output);

  // Test filename only
  formatted_output =
      format_file_output(file_abs, true, base_abs, content, false, false);
  expected_output =
      "\n## File: output_test.cpp\n\n```cpp\nLine 1\nLine 2\n\nLine 4\n```\n";
  // std::cout << "\nExpected:\n" << expected_output << "\nActual:\n" <<
  // formatted_output << std::endl; // Debug
  assert(formatted_output == expected_output);

  // Test remove empty lines
  formatted_output =
      format_file_output(file_abs, false, base_abs, content, true, false);
  expected_output = "\n## File: subdir1/output_test.cpp\n\n```cpp\nLine "
                    "1\nLine 2\nLine 4\n```\n";
  // std::cout << "\nExpected:\n" << expected_output << "\nActual:\n" <<
  // formatted_output << std::endl; // Debug
  assert(formatted_output == expected_output);

  std::cout << " Passed\n";
}

void test_format_file_output_line_numbers() {
  std::cout << "Test: Format file output with line numbers..." << std::flush;
  fs::path base_abs = TEST_DIR_PATH;
  fs::path file_abs = base_abs / "line_numbers_file.cpp";
  std::string content = "First line\nSecond line\nThird line";
  std::string formatted_output = format_file_output(
      file_abs, false, base_abs, content, false, true); // showLineNumbers=true
  std::string expected_output =
      "\n## File: line_numbers_file.cpp\n\n```cpp\n1 | First line\n2 | Second "
      "line\n3 | Third line\n```\n";
  assert(formatted_output == expected_output);
  std::cout << " Passed\n";
}

void test_process_single_file() {
  std::cout << "Test: Process single file..." << std::flush;
  create_test_directory_structure();
  fs::path base_abs = TEST_DIR_PATH;
  fs::path file_abs = base_abs / "subdir1" / "process_test.cpp";
  create_test_file(file_abs, "// Test file\nint main() { return 1; }\n");

  Config config = get_default_config(base_abs);
  config.removeComments = false;

  std::string output = process_single_file(file_abs, config, base_abs);
  // std::cout << "\nOutput:\n" << output << std::endl; // Debug
  assert(output.find("## File: subdir1/process_test.cpp") != std::string::npos);
  assert(output.find("// Test file") != std::string::npos);
  assert(output.find("```cpp") != std::string::npos);

  // Test with remove comments
  config.removeComments = true;
  std::string output_no_comments =
      process_single_file(file_abs, config, base_abs);
  // std::cout << "\nOutput No Comments:\n" << output_no_comments << std::endl;
  // // Debug
  assert(output_no_comments.find("## File: subdir1/process_test.cpp") !=
         std::string::npos);
  assert(output_no_comments.find("// Test file") == std::string::npos);
  assert(output_no_comments.find("int main()") !=
         std::string::npos); // Check remaining code

  std::cout << " Passed\n";
}

void test_is_last_file() {
  std::cout << "Test: Is last file..." << std::flush;
  create_test_directory_structure();
  fs::path base_abs = TEST_DIR_PATH;
  Config config = get_default_config(base_abs);

  // Populate config as if parsed
  config.lastFiles.push_back("FILE3.HPP");         // Filename only
  config.lastFiles.push_back("subdir1/file6.cpp"); // Relative path
  config.lastDirs.push_back("subdir2");            // Relative path

  // Manually populate the sets (normally done by parse_arguments)
  config.lastFilesSetFilename.insert(normalize_path("FILE3.HPP"));
  config.lastFilesSetRel.insert(normalize_path("subdir1/file6.cpp"));
  config.lastDirsSetRel.insert(normalize_path("subdir2"));

  assert(is_last_file(base_abs / "FILE3.HPP", config) ==
         true); // Match filename
  assert(is_last_file(base_abs / "subdir1/file6.cpp", config) ==
         true); // Match relative path
  assert(is_last_file(base_abs / "subdir2/some_other_file.xyz", config) ==
         true); // Match directory
  assert(is_last_file(base_abs / "file1.cpp", config) == false); // No match
  assert(is_last_file(base_abs / "subdir1/another.cpp", config) ==
         false); // No match

  std::cout << " Passed\n";
}

// Helper to check results of collect_files
void check_collect_results(
    const std::vector<fs::path> &normalFilesAbs,
    const std::vector<fs::path> &lastFilesListAbs, size_t expectedNormal,
    size_t expectedLast,
    const std::vector<std::string>
        &expectedNormalFilenames, // Just filenames for easier check
    const std::vector<std::string> &expectedLastFilenames) {
  assert(normalFilesAbs.size() == expectedNormal);
  assert(lastFilesListAbs.size() == expectedLast);

  std::vector<std::string> actualNormalFilenames;
  for (const auto &p : normalFilesAbs)
    actualNormalFilenames.push_back(p.filename().string());
  std::sort(actualNormalFilenames.begin(), actualNormalFilenames.end());

  std::vector<std::string> sortedExpectedNormal = expectedNormalFilenames;
  std::sort(sortedExpectedNormal.begin(), sortedExpectedNormal.end());
  assert(actualNormalFilenames == sortedExpectedNormal);

  std::vector<std::string> actualLastFilenames;
  for (const auto &p : lastFilesListAbs)
    actualLastFilenames.push_back(p.filename().string());
  std::sort(actualLastFilenames.begin(), actualLastFilenames.end());

  std::vector<std::string> sortedExpectedLast = expectedLastFilenames;
  std::sort(sortedExpectedLast.begin(), sortedExpectedLast.end());
  assert(actualLastFilenames == sortedExpectedLast);
}

void test_collect_files_normal() {
  std::cout << "Test: Collect files normal..." << std::flush;
  create_test_directory_structure();
  fs::path base_abs = TEST_DIR_PATH;
  Config config = get_default_config(base_abs);
  std::atomic<bool> stop_flag{false};

  auto [normal_files, last_files] = collect_files(config, stop_flag);

  // Expected (excluding gitignored *.txt, large_file.cpp, ignored folders,
  // .hidden dir contents) file1.cpp, FILE3.HPP, file4.excluded, file5,
  // subdir1/file6.cpp, .hidden_file.cpp, not_ignored_folder/file8.cpp,
  // file_abc.cpp, file_def.cpp, misc.data
  check_collect_results(normal_files, last_files, 10, 0,
                        {"file1.cpp", "FILE3.HPP", "file4.excluded", "file5",
                         "file6.cpp", ".hidden_file.cpp", "file8.cpp",
                         "file_abc.cpp", "file_def.cpp", "misc.data"},
                        {});

  std::cout << " Passed\n";
}

void test_collect_files_with_filters() {
  std::cout << "Test: Collect files with filters..." << std::flush;
  create_test_directory_structure();
  fs::path base_abs = TEST_DIR_PATH;
  Config config = get_default_config(base_abs);
  std::atomic<bool> stop_flag{false};

  // Filter: include only .cpp and .hpp, exclude 'excluded', filename matches
  // file_*, max size 100
  config.fileExtensions = {"cpp", "hpp"};
  config.excludedFileExtensions = {"excluded"};
  config.filenameRegexFilters = {
      "file_.*"};            // Only include files starting with "file_"
  config.maxFileSizeB = 100; // Will exclude large_file.cpp and file1.cpp

  auto [normal_files, last_files] = collect_files(config, stop_flag);

  // Expected: file_abc.cpp, file_def.cpp (satisfy all conditions)
  // file1.cpp > 100 bytes
  // FILE3.HPP name doesn't match file_.*
  // file4.excluded has excluded extension
  // file5 no extension
  // subdir1/file6.cpp name doesn't match file_.*
  // .hidden_file.cpp name doesn't match file_.*
  // not_ignored_folder/file8.cpp name doesn't match file_.*
  // large_file.cpp ignored by gitignore and size
  check_collect_results(normal_files, last_files, 2, 0,
                        {"file_abc.cpp", "file_def.cpp"}, {});

  std::cout << " Passed\n";
}

void test_collect_files_last() {
  std::cout << "Test: Collect files with --last..." << std::flush;
  create_test_directory_structure();
  fs::path base_abs = TEST_DIR_PATH;
  Config config = get_default_config(base_abs);
  std::atomic<bool> stop_flag{false};

  // Specify last files/dirs
  config.lastFiles.push_back("FILE3.HPP"); // filename
  config.lastDirs.push_back("subdir1");    // directory
  // Manually populate sets
  config.lastFilesSetFilename.insert(normalize_path("FILE3.HPP"));
  config.lastDirsSetRel.insert(normalize_path("subdir1"));

  auto [normal_files, last_files] = collect_files(config, stop_flag);

  // Expected Last: FILE3.HPP, subdir1/file6.cpp
  // Expected Normal: file1.cpp, file4.excluded, file5, .hidden_file.cpp,
  // not_ignored_folder/file8.cpp, file_abc.cpp, file_def.cpp, misc.data
  // Excluded: file2.txt, large_file.cpp (gitignore), ignored_folder/*,
  // .hidden_dir/*
  check_collect_results(normal_files, last_files, 8, 2,
                        {"file1.cpp", "file4.excluded", "file5",
                         ".hidden_file.cpp", "file8.cpp", "file_abc.cpp",
                         "file_def.cpp", "misc.data"},
                        {"FILE3.HPP", "file6.cpp"});

  std::cout << " Passed\n";
}

void test_collect_files_only_last() {
  std::cout << "Test: Collect files --only-last..." << std::flush;
  create_test_directory_structure();
  fs::path base_abs = TEST_DIR_PATH;
  Config config = get_default_config(base_abs);
  std::atomic<bool> stop_flag{false};

  config.onlyLast = true;
  config.disableGitignore =
      true; // Disable gitignore to test collection purely based on --last
  config.lastFiles.push_back("file2.txt");     // File (normally gitignored)
  config.lastDirs.push_back("ignored_folder"); // Directory (normally ignored)

  // Manually populate sets
  config.lastFilesSetRel.insert(
      normalize_path("file2.txt")); // Treat as relative if no slash? Let's
                                    // assume filename match intent here.
  config.lastFilesSetFilename.insert(normalize_path("file2.txt"));
  config.lastDirsSetRel.insert(normalize_path("ignored_folder"));

  auto [normal_files, last_files] = collect_files(config, stop_flag);

  // Expected Last: file2.txt, ignored_folder/file7.cpp
  // Expected Normal: None
  check_collect_results(normal_files, last_files, 0, 2, {},
                        {"file2.txt", "file7.cpp"});

  std::cout << " Passed\n";
}

void test_process_directory_output_order() {
  std::cout << "Test: Process directory output order (incl --last)..."
            << std::flush;
  create_test_directory_structure();
  fs::path base_abs = TEST_DIR_PATH;
  Config config = get_default_config(base_abs);
  std::atomic<bool> stop_flag{false};

  // Make FILE3.HPP and subdir1/ contents last
  config.lastFiles.push_back("FILE3.HPP");
  config.lastDirs.push_back("subdir1");
  // Manually populate sets
  config.lastFilesSetFilename.insert(normalize_path("FILE3.HPP"));
  config.lastDirsSetRel.insert(normalize_path("subdir1"));

  // Capture output
  std::stringstream output_buffer;
  bool success = process_directory(config, stop_flag); // Use buffer directly
  assert(success);

  std::string output =
      output_buffer
          .str(); // If process_directory wrote to cout, capture it instead. For
                  // now assume it writes internally to the stream passed

  // We need to capture stdout if process_directory writes there
  std::string captured_output = capture_stdout([&]() {
    bool success_stdout = process_directory(config, stop_flag);
    assert(success_stdout);
  });

  // Check order: Normal files (alphabetical), then last files/dirs (in
  // specified order)
  size_t pos_file1 = captured_output.find("## File: file1.cpp");
  size_t pos_file_abc = captured_output.find("## File: file_abc.cpp");
  size_t pos_file_def = captured_output.find("## File: file_def.cpp");
  size_t pos_file4 = captured_output.find("## File: file4.excluded");
  size_t pos_file5 = captured_output.find("## File: file5");
  size_t pos_hidden_file = captured_output.find("## File: .hidden_file.cpp");
  size_t pos_misc = captured_output.find("## File: misc.data");
  size_t pos_file8 =
      captured_output.find("## File: not_ignored_folder/file8.cpp");

  size_t pos_last_dir_file6 = captured_output.find(
      "## File: subdir1/file6.cpp"); // From last dir subdir1
  size_t pos_last_file3 =
      captured_output.find("## File: FILE3.HPP"); // From last file FILE3.HPP

  // Check all files are present
  assert(pos_file1 != std::string::npos);
  assert(pos_file_abc != std::string::npos);
  assert(pos_file_def != std::string::npos);
  assert(pos_file4 != std::string::npos);
  assert(pos_file5 != std::string::npos);
  assert(pos_hidden_file != std::string::npos);
  assert(pos_misc != std::string::npos);
  assert(pos_file8 != std::string::npos);
  assert(pos_last_dir_file6 != std::string::npos);
  assert(pos_last_file3 != std::string::npos);

  // Check order: All normal files should appear before last files.
  // Within normal files, check relative order (alphabetical)
  assert(pos_hidden_file < pos_file1); // .hidden_file.cpp < file1.cpp
  assert(pos_file1 < pos_file4);       // file1.cpp < file4.excluded
  assert(pos_file4 < pos_file5);       // file4.excluded < file5
  assert(pos_file5 < pos_file_abc);    // file5 < file_abc.cpp
  assert(pos_file_abc < pos_file_def); // file_abc.cpp < file_def.cpp
  assert(pos_file_def < pos_misc);     // file_def.cpp < misc.data
  assert(pos_misc <
         pos_file8); // misc.data < not_ignored_folder/file8.cpp (Path sort)

  // Check order: Last files appear after normal files.
  // Order of last files/dirs: subdir1 first (-z subdir1), then FILE3.HPP (-z
  // FILE3.HPP)
  assert(pos_file8 <
         pos_last_dir_file6); // Last normal file before first last item
  assert(pos_last_dir_file6 <
         pos_last_file3); // subdir1 content before FILE3.HPP

  std::cout << " Passed\n";
}

void test_output_to_file() {
  std::cout << "Test: Output to file..." << std::flush;
  create_test_directory_structure();
  fs::path base_abs = TEST_DIR_PATH;
  Config config = get_default_config(base_abs);
  std::atomic<bool> stop_flag{false};

  config.outputFile = "test_output.txt";
  bool success =
      process_directory(config, stop_flag); // This should write to the file
  assert(success);

  // Check if file exists and has content
  assert(fs::exists(config.outputFile));
  std::ifstream outputFileStream(config.outputFile, std::ios::binary);
  std::string output_file_content(
      (std::istreambuf_iterator<char>(outputFileStream)),
      std::istreambuf_iterator<char>());
  outputFileStream.close();

  assert(output_file_content.find("# File generated by DirCat") !=
         std::string::npos);
  assert(output_file_content.find("## File: file1.cpp") != std::string::npos);
  assert(output_file_content.find("## File: file2.txt") ==
         std::string::npos); // Should be gitignored
  fs::remove("test_output.txt");
  std::cout << " Passed\n";
}

void test_output_file_creation() {
  std::cout << "Test: Output to non-existent file creates file..."
            << std::flush;
  create_test_directory_structure();
  fs::path base_abs = TEST_DIR_PATH;
  Config config = get_default_config(base_abs);
  std::atomic<bool> stop_flag{false};

  config.outputFile = "test_output_created.txt";
  fs::remove(config.outputFile); // Ensure it doesn't exist
  assert(!fs::exists(config.outputFile));

  bool success =
      process_directory(config, stop_flag); // Should create and write
  assert(success);

  assert(fs::exists(config.outputFile)); // Check creation
  std::ifstream outputFileStream(config.outputFile);
  std::string output_file_content(
      (std::istreambuf_iterator<char>(outputFileStream)),
      std::istreambuf_iterator<char>());
  outputFileStream.close();
  assert(output_file_content.find("## File: file1.cpp") != std::string::npos);

  fs::remove(config.outputFile);
  std::cout << " Passed\n";
}

void test_dry_run_mode() {
  std::cout << "Test: Dry run mode..." << std::flush;
  create_test_directory_structure();
  fs::path base_abs = TEST_DIR_PATH;
  Config config = get_default_config(base_abs);
  config.dryRun = true;
  std::atomic<bool> stop_flag{false};

  // Expected normal files (relative paths, sorted)
  std::vector<std::string> expected_normal = {
      ".hidden_file.cpp", "FILE3.HPP", "file1.cpp",
      "file4.excluded",   "file5",     "file_abc.cpp",
      "file_def.cpp",     "misc.data", "not_ignored_folder/file8.cpp",
      "subdir1/file6.cpp"};
  std::sort(expected_normal.begin(), expected_normal.end());

  std::string output = capture_stdout([&]() {
    bool success = process_directory(config, stop_flag);
    assert(success);
  });
  // std::cout << "\nDry Run Output:\n" << output << std::endl; // Debug

  assert(output.find("Files to be processed") != std::string::npos);
  assert(output.find("--- Normal Files (10) ---") != std::string::npos);
  assert(output.find("--- Last Files (0) ---") != std::string::npos);

  // Check for presence of expected relative paths
  for (const auto &expected_path : expected_normal) {
    assert(output.find(expected_path) != std::string::npos);
  }

  // Check excluded files are not present
  assert(output.find("file2.txt") == std::string::npos);      // gitignore
  assert(output.find("large_file.cpp") == std::string::npos); // gitignore
  assert(output.find("ignored_folder/file7.cpp") ==
         std::string::npos); // ignored folder
  assert(output.find(".hidden_dir") ==
         std::string::npos); // hidden folder contents
  assert(output.find("## File:") ==
         std::string::npos); // No file content formatting

  std::cout << " Passed\n";
}

void test_dry_run_mode_output_file() {
  std::cout << "Test: Dry run mode with output file..." << std::flush;
  create_test_directory_structure();
  fs::path base_abs = TEST_DIR_PATH;
  Config config = get_default_config(base_abs);
  config.dryRun = true;
  config.outputFile = "dry_run_output.txt";
  std::atomic<bool> stop_flag{false};

  fs::remove(config.outputFile);
  bool success = process_directory(config, stop_flag);
  assert(success);

  assert(fs::exists(config.outputFile));
  std::ifstream outputFileStream(config.outputFile);
  std::string output_file_content(
      (std::istreambuf_iterator<char>(outputFileStream)),
      std::istreambuf_iterator<char>());
  outputFileStream.close();
  fs::remove(config.outputFile);

  // std::cout << "\nDry Run File Output:\n" << output_file_content <<
  // std::endl; // Debug
  assert(output_file_content.find("Files to be processed") !=
         std::string::npos);
  assert(output_file_content.find("file1.cpp") !=
         std::string::npos); // Check one file
  assert(output_file_content.find("subdir1/file6.cpp") !=
         std::string::npos); // Check nested file
  assert(output_file_content.find("file2.txt") ==
         std::string::npos); // Check ignored file absent
  assert(output_file_content.find("## File:") ==
         std::string::npos); // No content formatting

  std::cout << " Passed\n";
}

void test_single_file_input() {
  std::cout << "Test: Single file input mode..." << std::flush;
  create_test_directory_structure();
  fs::path file_path = TEST_DIR_PATH / "file1.cpp"; // Use an existing file

  // Simulate command line: dircat test_dir/file1.cpp
  std::string file_path_str = file_path.string();
  char *argv[] = {
      (char *)"dircat",
      file_path_str
          .data()}; // Use data() or const_cast<char*>(file_path_str.c_str()) if
                    // needed, but prefer data() for non-const ptr
  int argc = 2;

  Config config = parse_arguments(argc, argv);
  std::atomic<bool> stop_flag{false};
  globalShouldStop = &stop_flag; // Setup global stopper for consistency

  // Test actual processing
  std::string output = capture_stdout([&]() {
    std::ofstream dummy_stream; // Not used when outputting to cout
    bool success = process_single_file_entry(config, std::cout); // Pass cout
    assert(success);
  });

  // std::cout << "\nSingle File Output:\n" << output << std::endl; // Debug
  assert(output.find("# File generated by DirCat") != std::string::npos);
  assert(output.find("## File: file1.cpp") !=
         std::string::npos); // Filename only for single file
  assert(output.find("int main()") !=
         std::string::npos); // Check content present

  // Test dry run for single file
  // FIX: Store string result to avoid dangling pointer from temporary
  std::string file_path_str_dry =
      file_path.string(); // Re-store (or reuse file_path_str)
  char *argv_dry[] = {(char *)"dircat", file_path_str_dry.data(), (char *)"-D"};
  int argc_dry = 3;
  Config config_dry = parse_arguments(argc_dry, argv_dry);
  std::string output_dry = capture_stdout([&]() {
    std::ofstream dummy_stream;
    bool success = process_single_file_entry(config_dry, std::cout);
    assert(success);
  });
  // std::cout << "\nSingle File Dry Output:\n" << output_dry << std::endl; //
  // Debug
  assert(output_dry.find("File to be processed:") != std::string::npos);
  assert(output_dry.find(normalize_path(file_path)) !=
         std::string::npos); // Should list the absolute path
  assert(output_dry.find("## File:") == std::string::npos);

  std::cout << " Passed\n";
}

void test_single_file_input_output_file() {
  std::cout << "Test: Single file input mode with output file..." << std::flush;
  create_test_directory_structure();
  fs::path file_path = TEST_DIR_PATH / "file1.cpp";
  fs::path output_file = "single_file_output.txt";
  fs::remove(output_file);

  // Simulate command line: dircat test_dir/file1.cpp -o single_file_output.txt
  // FIX: Store string results to avoid dangling pointers from temporaries
  std::string file_path_str_out = file_path.string();
  std::string output_file_str = output_file.string();
  char *argv[] = {(char *)"dircat", file_path_str_out.data(), (char *)"-o",
                  output_file_str.data()};
  int argc = 4;

  Config config = parse_arguments(argc, argv);
  std::atomic<bool> stop_flag{false};
  globalShouldStop = &stop_flag;

  // Setup output stream as main does
  std::ofstream outputFileStream;
  std::ostream *outputPtr = &std::cout;
  outputFileStream.open(config.outputFile,
                        std::ios::binary | std::ios::out | std::ios::trunc);
  assert(outputFileStream.is_open());
  outputPtr = &outputFileStream;

  bool success = process_single_file_entry(config, *outputPtr);
  assert(success);
  outputFileStream.close();

  // Check file content
  assert(fs::exists(output_file));
  std::ifstream ifs(output_file);
  std::string content((std::istreambuf_iterator<char>(ifs)),
                      std::istreambuf_iterator<char>());
  ifs.close();
  fs::remove(output_file);

  assert(content.find("# File generated by DirCat") != std::string::npos);
  assert(content.find("## File: file1.cpp") != std::string::npos);
  assert(content.find("int main()") != std::string::npos);

  std::cout << " Passed\n";
}

int main() {
  try {
    // Setup common resources once if needed, or per test
    create_test_directory_structure(); // Create base structure for most tests

    // Run tests
    test_trim();
    test_load_gitignore_rules();
    test_is_path_ignored_by_gitignore();             // Uses TEST_DIR_PATH
    test_is_path_ignored_by_gitignore_multi_level(); // Uses
                                                     // TEST_GITIGNORE_DIR_PATH
    test_is_file_size_valid();
    test_is_file_extension_allowed();
    test_should_ignore_folder(); // Uses TEST_DIR_PATH
    test_should_ignore_file();   // Uses TEST_DIR_PATH
    test_matches_regex_filters();
    test_matches_filename_regex_filters();
    test_remove_cpp_comments();
    test_format_file_output();              // Uses TEST_DIR_PATH
    test_format_file_output_line_numbers(); // Uses TEST_DIR_PATH
    test_process_single_file();             // Uses TEST_DIR_PATH
    test_is_last_file();                    // Uses TEST_DIR_PATH
    test_collect_files_normal();            // Uses TEST_DIR_PATH
    test_collect_files_with_filters();      // Uses TEST_DIR_PATH
    test_collect_files_last();              // Uses TEST_DIR_PATH
    test_collect_files_only_last();         // Uses TEST_DIR_PATH
    test_process_directory_output_order();  // Uses TEST_DIR_PATH
    test_output_to_file();                  // Uses TEST_DIR_PATH
    test_output_file_creation();            // Uses TEST_DIR_PATH
    test_dry_run_mode();                    // Uses TEST_DIR_PATH
    test_dry_run_mode_output_file();        // Uses TEST_DIR_PATH
    test_single_file_input();               // Uses TEST_DIR_PATH
    test_single_file_input_output_file();   // Uses TEST_DIR_PATH

    // Cleanup after all tests
    cleanup_test_directories();
    std::cout << "\nAll tests passed successfully!\n";
    return 0;

  } catch (const std::exception &e) {
    std::cerr << "\n\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n";
    std::cerr << "Test failed with exception: " << e.what() << std::endl;
    std::cerr << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n\n";
    cleanup_test_directories(); // Attempt cleanup even on failure
    return 1;
  } catch (...) {
    std::cerr << "\n\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n";
    std::cerr << "Test failed with unknown exception!" << std::endl;
    std::cerr << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n\n";
    cleanup_test_directories();
    return 1;
  }
}