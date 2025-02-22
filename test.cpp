#include <cassert>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "lib.cpp" // Assuming the refactored lib.cpp is in the same directory

namespace fs = std::filesystem;

// --- Helper Functions for Testing ---

// Creates a test file with given path and content.
void create_test_file(const fs::path &path, const std::string &content) {
  std::ofstream file(path);
  if (!file.is_open()) {
    std::cerr << "Error creating test file: " << path << std::endl;
    return;
  }
  file << content;
}

// Creates a test directory structure in "test_dir".
void create_test_directory_structure() {
  fs::remove_all("test_dir"); // Ensure clean state
  fs::create_directories("test_dir/subdir1");
  fs::create_directories("test_dir/subdir2");
  fs::create_directories("test_dir/.hidden_dir");
  fs::create_directories("test_dir/ignored_folder");
  fs::create_directories("test_dir/not_ignored_folder");

  create_test_file("test_dir/file1.cpp",
                   "// C++ file\nint main() { return 0; }");
  create_test_file("test_dir/file2.txt", "Text file content");
  create_test_file("test_dir/FILE3.HPP",
                   "// Header file\n#define FILE3_HPP"); // Uppercase extension
  create_test_file("test_dir/file4.excluded", "Excluded file");
  create_test_file("test_dir/file5", "No extension file");
  create_test_file("test_dir/subdir1/file6.cpp", "// Subdir file");
  create_test_file("test_dir/.hidden_file.cpp", "// Hidden file");
  create_test_file("test_dir/ignored_folder/file7.cpp",
                   "// Ignored folder file");
  create_test_file("test_dir/large_file.cpp", std::string(2049, 'L')); // > 2KB
  create_test_file("test_dir/.gitignore",
                   "*.txt\n.hidden_dir/\nignored_folder/");
  create_test_file("test_dir/not_ignored_folder/file8.cpp", "// Not ignored");
}

// Cleans up the "test_dir" directory.
void cleanup_test_directory() { fs::remove_all("test_dir"); }

// Captures stdout output for testing console output.
std::string capture_stdout(const std::function<void()> &func) {
  std::stringstream buffer;
  std::streambuf *oldCout = std::cout.rdbuf();
  std::cout.rdbuf(buffer.rdbuf());
  func();
  std::cout.rdbuf(oldCout);
  return buffer.str();
}

// --- Test Functions ---

void test_format_file_output_line_numbers() {
  std::string content = "First line\nSecond line\nThird line";
  std::string formatted_output = format_file_output(
      "test_dir/line_numbers_file.cpp", false, false, "test_dir", content,
      false, true); // showLineNumbers = true
  std::string expected_output = "\n## File: line_numbers_file.cpp\n\n```cpp\n"
                                "1 | First line\n"
                                "2 | Second line\n"
                                "3 | Third line\n"
                                "\n```\n";
  assert(formatted_output == expected_output);
  std::cout << "Format file output with line numbers test passed\n";
}

void test_trim() {
  assert(trim("  hello  ") == "hello");
  assert(trim("\tworld\n") == "world");
  assert(trim("no whitespace") == "no whitespace");
  assert(trim("") == "");
  std::cout << "Trim test passed\n";
}

void test_load_gitignore_rules() {
  create_test_file("test_dir/.gitignore_test", "*.temp\n# comment\ndir/");
  std::vector<std::string> rules =
      load_gitignore_rules("test_dir/.gitignore_test");
  assert(rules.size() == 2);
  assert(rules[0] == "*.temp");
  assert(rules[1] == "dir/");
  fs::remove("test_dir/.gitignore_test");
  std::cout << "Load gitignore rules test passed\n";
}

void test_is_path_ignored_by_gitignore() {
  Config config;
  config.dirPath = "test_dir";
  config.gitignorePath = "test_dir/.gitignore";
  config.gitignoreRules = load_gitignore_rules(config.gitignorePath);

  assert(is_path_ignored_by_gitignore("test_dir/file2.txt",
                                      config.gitignoreRules,
                                      config.dirPath) == true);
  assert(is_path_ignored_by_gitignore("test_dir/file1.cpp",
                                      config.gitignoreRules,
                                      config.dirPath) == false);
  // assert(is_path_ignored_by_gitignore("test_dir/.hidden_dir/file.cpp",
  //                                     config.gitignoreRules,
  //                                     config.dirPath) == true);
  // assert(is_path_ignored_by_gitignore("test_dir/not_ignored_folder/file8.cpp",
  //                                     config.gitignoreRules,
  //                                     config.dirPath) == false); // Negation
  std::cout << "Is path ignored by gitignore test passed\n";
}

void test_is_file_size_valid() {
  create_test_file("test_dir/small_file.txt", "small");
  create_test_file("test_dir/medium_file.txt", std::string(1024, 'M')); // 1KB
  create_test_file("test_dir/large_file.txt", std::string(2049, 'L'));  // > 2KB

  assert(is_file_size_valid("test_dir/small_file.txt", 1024) == true);
  assert(is_file_size_valid("test_dir/medium_file.txt", 1024) == true);
  assert(is_file_size_valid("test_dir/large_file.txt", 2048) == false);
  assert(is_file_size_valid("test_dir/small_file.txt", 0) == true); // No limit
  fs::remove("test_dir/small_file.txt");
  fs::remove("test_dir/medium_file.txt");
  fs::remove("test_dir/large_file.txt");
  std::cout << "Is file size valid test passed\n";
}

void test_is_file_extension_allowed() {
  std::vector<std::string> allowed_exts = {"cpp", "hpp"};
  std::vector<std::string> excluded_exts = {"excluded"};

  assert(is_file_extension_allowed("test_dir/file.cpp", allowed_exts,
                                   excluded_exts) == true);
  assert(is_file_extension_allowed("test_dir/file.hpp", allowed_exts,
                                   excluded_exts) == true);
  assert(is_file_extension_allowed("test_dir/file.txt", allowed_exts,
                                   excluded_exts) == false);
  assert(is_file_extension_allowed("test_dir/file.excluded", allowed_exts,
                                   excluded_exts) == false); // Excluded
  assert(is_file_extension_allowed("test_dir/file.noext", {}, excluded_exts) ==
         true); // No allowed exts, not excluded
  assert(is_file_extension_allowed("test_dir/file.noext", allowed_exts, {}) ==
         false); // Allowed exts specified, not in list
  std::cout << "Is file extension allowed test passed\n";
}

void test_should_ignore_folder() {
  Config base_config; // Using base config to avoid modifying defaults in tests
  base_config.dirPath = "test_dir";
  base_config.gitignorePath = "test_dir/.gitignore";
  base_config.gitignoreRules = load_gitignore_rules(base_config.gitignorePath);

  assert(
      should_ignore_folder("test_dir/.hidden_dir", base_config.disableGitignore,
                           base_config.gitignoreRules, base_config.dirPath,
                           base_config.ignoreDotFolders,
                           base_config.ignoredFolders) == true); // Dot folder
  // assert(should_ignore_folder("test_dir/ignored_folder",
  //                             base_config.disableGitignore,
  //                             base_config.gitignoreRules,
  //                             base_config.dirPath,
  //                             base_config.ignoreDotFolders,
  //                             base_config.ignoredFolders) == true); //
  //                             Gitignore
  assert(should_ignore_folder(
             "test_dir/not_ignored_folder", base_config.disableGitignore,
             base_config.gitignoreRules, base_config.dirPath,
             base_config.ignoreDotFolders,
             base_config.ignoredFolders) == false); // Not ignored
  assert(should_ignore_folder("test_dir/subdir1", base_config.disableGitignore,
                              base_config.gitignoreRules, base_config.dirPath,
                              base_config.ignoreDotFolders,
                              {"subdir1"}) == true); // Ignored folder list
  std::cout << "Should ignore folder test passed\n";
}

void test_should_ignore_file() {
  Config base_config;
  base_config.dirPath = "test_dir";
  base_config.gitignorePath = "test_dir/.gitignore";
  base_config.gitignoreRules = load_gitignore_rules(base_config.gitignorePath);
  base_config.maxFileSizeB = 2048;

  create_test_file("test_dir/ignore_me.txt", "ignore");
  create_test_file("test_dir/large_ignore.cpp", std::string(4096, 'I'));

  assert(should_ignore_file("test_dir/file2.txt", base_config.disableGitignore,
                            base_config.gitignoreRules, base_config.dirPath,
                            base_config.maxFileSizeB,
                            base_config.ignoredFiles) == true); // Gitignore
  assert(should_ignore_file("test_dir/ignore_me.txt",
                            base_config.disableGitignore,
                            base_config.gitignoreRules, base_config.dirPath,
                            base_config.maxFileSizeB,
                            {"ignore_me.txt"}) == true); // Ignored files list
  assert(should_ignore_file("test_dir/large_file.cpp",
                            base_config.disableGitignore,
                            base_config.gitignoreRules, base_config.dirPath,
                            base_config.maxFileSizeB,
                            base_config.ignoredFiles) == true); // Size limit
  assert(should_ignore_file("test_dir/file1.cpp", base_config.disableGitignore,
                            base_config.gitignoreRules, base_config.dirPath,
                            base_config.maxFileSizeB,
                            base_config.ignoredFiles) == false); // Not ignored
  fs::remove("test_dir/ignore_me.txt");
  fs::remove("test_dir/large_ignore.cpp");
  std::cout << "Should ignore file test passed\n";
}

void test_matches_regex_filters() {
  std::vector<std::string> regex_filters = {"file[0-9]\\.cpp", "\\.hpp$"};
  assert(matches_regex_filters("test_dir/file1.cpp", regex_filters) == true);
  assert(matches_regex_filters("test_dir/FILE3.HPP", regex_filters) == false);
  assert(matches_regex_filters("test_dir/file2.txt", regex_filters) == false);
  assert(matches_regex_filters("test_dir/no_match.txt", {}) ==
         false); // No filters
  std::cout << "Matches regex filters test passed\n";
}

void test_remove_cpp_comments() {
  std::string code_with_comments = "// Line comment\nint /* block */ main() "
                                   "{\n  return 0; // End comment\n}";
  std::string code_no_comments = remove_cpp_comments(code_with_comments);
  assert(code_no_comments.find("//") == std::string::npos);
  assert(code_no_comments.find("/*") == std::string::npos);
  assert(code_no_comments.find("block") == std::string::npos);
  std::cout << "Remove cpp comments test passed\n";
}

void test_format_file_output() {
  std::string content = "File content lines\nSecond line.";
  std::string formatted_output =
      format_file_output("test_dir/output_file.cpp", false, false, "test_dir",
                         content, false, false);
  std::string expected_output = "\n## File: output_file.cpp\n\n```cpp\nFile "
                                "content lines\nSecond line.\n\n```\n";
  assert(formatted_output == expected_output);

  formatted_output = format_file_output("test_dir/output_file.cpp", true, true,
                                        "test_dir", content, true, false);
  expected_output = "\n### File: output_file.cpp\n```cpp\nFile content "
                    "lines\nSecond line.\n\n```\n";

  assert(formatted_output == expected_output);

  std::cout << "Format file output test passed\n";
}

void test_process_single_file() {
  create_test_file("test_dir/process_test.cpp",
                   "// Test file\nint main() { return 1; }");
  std::string output =
      process_single_file("test_dir/process_test.cpp", 0, false, false, false,
                          "test_dir", false, false);
  assert(output.find("Test file") != std::string::npos); // Content is included
  assert(output.find("```cpp") != std::string::npos); // Language tag included

  std::string output_no_comments =
      process_single_file("test_dir/process_test.cpp", 0, true, false, false,
                          "test_dir", false, false);
  assert(output_no_comments.find("Test file") ==
         std::string::npos); // Comment removed

  fs::remove("test_dir/process_test.cpp");
  std::cout << "Process single file test passed\n";
}

void test_is_last_file() {
  Config config;
  config.dirPath = "test_dir";
  config.lastFiles = {"file3.hpp", "last_file.txt"};
  config.lastDirs = {"subdir2", "last_dir"};

  assert(is_last_file("test_dir/file3.hpp", config.dirPath, config.lastFiles,
                      config.lastDirs) == true);
  assert(is_last_file("test_dir/subdir2/file.cpp", config.dirPath,
                      config.lastFiles, config.lastDirs) == true);
  assert(is_last_file("test_dir/file1.cpp", config.dirPath, config.lastFiles,
                      config.lastDirs) == false);
  std::cout << "Is last file test passed\n";
}

void test_collect_files_normal() {
  Config config;
  config.dirPath = "test_dir";
  std::atomic<bool> should_stop{false};
  auto [normal_files, last_files] = collect_files(config, should_stop);

  int normal_count = 0;
  for (const auto &file : normal_files) {
    if (file.filename() != "file2.txt" &&        // gitignored
        file.filename() != ".hidden_file.cpp" && // dot file but not ignored yet
        file.filename() != ".gitignore" &&
        file.string().find("ignored_folder") ==
            std::string::npos && // gitignored folder
        file.string().find(".hidden_dir") ==
            std::string::npos) // gitignored folder
    {
      normal_count++;
    }
  }
  assert(normal_count ==
         5); // FILE3.HPP, file1.cpp, file4.excluded, large_file.cpp, file6.cpp

  std::cout << "Collect files normal test passed\n";
}

void test_collect_files_only_last() {
  Config config;
  config.dirPath = "test_dir";
  config.onlyLast = true;
  config.lastFiles = {"file2.txt",
                      "file1.cpp"}; // file2.txt is gitignored but should still
                                    // be processed because --only-last
  config.disableGitignore = true;   // Disable gitignore for this test
  config.gitignoreRules.clear();

  std::atomic<bool> should_stop{false};
  auto [normal_files, last_files] = collect_files(config, should_stop);

  assert(normal_files.empty());
  assert(last_files.size() == 2);
  bool found_file1 = false;
  bool found_file2 = false;
  for (const auto &file : last_files) {
    if (file.filename() == "file1.cpp")
      found_file1 = true;
    if (file.filename() == "file2.txt")
      found_file2 = true;
  }
  assert(found_file1);
  assert(found_file2);

  std::cout << "Collect files only last test passed\n";
}

void test_process_file_chunk_unordered() {
  Config config;
  config.dirPath = "test_dir";
  config.unorderedOutput = true; // Unordered output

  std::vector<fs::path> files_to_process = {"test_dir/file1.cpp",
                                            "test_dir/FILE3.HPP"};
  std::vector<std::pair<fs::path, std::string>> results;
  std::atomic<size_t> processed_files{0};
  std::atomic<size_t> total_bytes{0};
  std::mutex console_mutex;
  std::mutex ordered_results_mutex;
  std::atomic<bool> should_stop{false};

  std::string output = capture_stdout([&]() {
    process_file_chunk(
        std::span{files_to_process.begin(), files_to_process.end()},
        config.unorderedOutput, config.removeComments, config.maxFileSizeB,
        config.disableMarkdownlintFixes, config.showFilenameOnly,
        config.dirPath, config.removeEmptyLines, results, processed_files,
        total_bytes, console_mutex, ordered_results_mutex, should_stop,
        std::cout, false);
  });

  assert(processed_files == 2);
  assert(total_bytes > 0);
  assert(
      results.empty()); // Unordered, results should be empty, output to stdout
  assert(output.find("File: file1.cpp") != std::string::npos ||
         output.find("File: FILE3.HPP") !=
             std::string::npos); // Check for filenames in output
  assert(output.find("File: file1.cpp") != std::string::npos &&
         output.find("File: FILE3.HPP") !=
             std::string::npos); // Check for both filenames in output
  std::cout << "Process file chunk unordered test passed\n";
}

void test_process_file_chunk_ordered() {
  Config config;
  config.dirPath = "test_dir";
  config.unorderedOutput = false; // Ordered output

  std::vector<fs::path> files_to_process = {"test_dir/file1.cpp",
                                            "test_dir/FILE3.HPP"};
  std::vector<std::pair<fs::path, std::string>> results;
  std::atomic<size_t> processed_files{0};
  std::atomic<size_t> total_bytes{0};
  std::mutex console_mutex;
  std::mutex ordered_results_mutex;
  std::atomic<bool> should_stop{false};

  capture_stdout([&]() { // Capture to avoid stdout in ordered test
    process_file_chunk(
        std::span{files_to_process.begin(), files_to_process.end()},
        config.unorderedOutput, config.removeComments, config.maxFileSizeB,
        config.disableMarkdownlintFixes, config.showFilenameOnly,
        config.dirPath, config.removeEmptyLines, results, processed_files,
        total_bytes, console_mutex, ordered_results_mutex, should_stop,
        std::cout, false);
  });

  assert(processed_files == 2);
  assert(total_bytes > 0);
  assert(results.size() == 2); // Ordered, results should have data
  assert(results[0].first.filename() == "file1.cpp");
  assert(results[1].first.filename() == "FILE3.HPP");
  std::cout << "Process file chunk ordered test passed\n";
}

void test_output_to_file() {
  Config config;
  config.dirPath = "test_dir";
  config.outputFile = "test_output.txt"; // Set output file
  std::atomic<bool> should_stop{false};

  std::string expected_content_part =
      "File: file1.cpp"; // Part of expected output

  capture_stdout([&]() { // Capture stdout to prevent polluting test output
    process_directory(config, should_stop);
  });

  std::ifstream outputFileStream("test_output.txt");
  std::string output_file_content(
      (std::istreambuf_iterator<char>(outputFileStream)),
      std::istreambuf_iterator<char>());

  assert(outputFileStream.is_open()); // Check if file was created
  assert(output_file_content.find(expected_content_part) !=
         std::string::npos); // Check content

  outputFileStream.close();      // Close the input file stream
  fs::remove("test_output.txt"); // Cleanup output file
  std::cout << "Output to file test passed\n";
}

int main() {
  try {
    cleanup_test_directory();
    create_test_directory_structure();

    test_trim();
    test_load_gitignore_rules();
    test_is_path_ignored_by_gitignore();
    test_is_file_size_valid();
    test_is_file_extension_allowed();
    test_should_ignore_folder();
    test_should_ignore_file();
    test_matches_regex_filters();
    test_remove_cpp_comments();
    test_format_file_output();
    test_process_single_file();
    test_is_last_file();
    test_collect_files_normal();
    test_collect_files_only_last();
    test_process_file_chunk_unordered();
    test_process_file_chunk_ordered();
    test_output_to_file();
    test_format_file_output_line_numbers(); // Added line numbers test

    cleanup_test_directory();
    std::cout << "\nAll tests passed successfully!\n";
    return 0;

  } catch (const std::exception &e) {
    std::cerr << "Test failed: " << e.what() << std::endl;
    cleanup_test_directory();
    return 1;
  }
}
