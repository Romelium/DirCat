#include "lib.cpp"
#include <cassert>
#include <functional>
#include <iostream> // Include for std::cerr, std::cout, std::endl

namespace fs = std::filesystem;

// --- Helper Functions for Testing ---

void create_test_file(const fs::path &path, const std::string &content) {
  std::ofstream file(path);
  if (!file.is_open()) {
    std::cerr << "Error creating test file: " << path << std::endl;
    return;
  }
  file << content;
}

void create_test_directory_structure() {
  fs::remove_all("test_dir");
  fs::create_directories("test_dir/subdir1");
  fs::create_directories("test_dir/subdir2");
  fs::create_directories("test_dir/.hidden_dir");
  fs::create_directories("test_dir/ignored_folder");
  fs::create_directories("test_dir/not_ignored_folder");

  create_test_file("test_dir/file1.cpp",
                   "// C++ file\nint main() { return 0; }");
  create_test_file("test_dir/file2.txt", "Text file content");
  create_test_file("test_dir/FILE3.HPP", "// Header file\n#define FILE3_HPP");
  create_test_file("test_dir/file4.excluded", "Excluded file");
  create_test_file("test_dir/file5", "No extension file");
  create_test_file("test_dir/subdir1/file6.cpp", "// Subdir file");
  create_test_file("test_dir/.hidden_file.cpp", "// Hidden file");
  create_test_file("test_dir/ignored_folder/file7.cpp",
                   "// Ignored folder file");
  create_test_file("test_dir/large_file.cpp", std::string(2049, 'L'));
  create_test_file("test_dir/.gitignore",
                   "*.txt\n.hidden_dir/\nignored_folder/");
  create_test_file("test_dir/not_ignored_folder/file8.cpp", "// Not ignored");
  create_test_file("test_dir/file_abc.cpp", "// abc file");
  create_test_file("test_dir/file_def.cpp", "// def file");
  create_test_file("test_dir/misc.txt", "misc text file");
}

void create_test_directory_gitignore_structure() {
  fs::remove_all("test_dir_gitignore");
  fs::create_directories("test_dir_gitignore/subdir1");
  fs::create_directories("test_dir_gitignore/subdir2");

  create_test_file("test_dir_gitignore/.gitignore", "*.level1");
  create_test_file("test_dir_gitignore/subdir1/.gitignore",
                   "*.level2\n!important.level2");
  create_test_file("test_dir_gitignore/file1.level1", "level1 file in root");
  create_test_file("test_dir_gitignore/subdir1/file2.level2",
                   "level2 file in subdir1");
  create_test_file("test_dir_gitignore/subdir1/important.level2",
                   "important level2 file in subdir1");
  create_test_file("test_dir_gitignore/subdir2/file3.level1",
                   "level1 file in subdir2");
}

void cleanup_test_directory() { fs::remove_all("test_dir"); }
void cleanup_test_directory_gitignore() {
  fs::remove_all("test_dir_gitignore");
}

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
  std::string formatted_output =
      format_file_output("test_dir/line_numbers_file.cpp", false, "test_dir",
                         content, false, true);
  std::string expected_output = "\n## File: line_numbers_file.cpp\n\n```cpp\n"
                                "1 | First line\n"
                                "2 | Second line\n"
                                "3 | Third line\n"
                                "\n```\n";
  assert(formatted_output == expected_output);
  std::cout << "Test: Format file output with line numbers passed\n";
}

void test_trim() {
  assert(trim("  hello  ") == "hello");
  assert(trim("\tworld\n") == "world");
  assert(trim("no whitespace") == "no whitespace");
  assert(trim("") == "");
  std::cout << "Test: Trim passed\n";
}

void test_load_gitignore_rules() {
  create_test_file("test_dir/.gitignore_test", "*.temp\n# comment\ndir/");
  std::vector<std::string> rules =
      load_gitignore_rules("test_dir/.gitignore_test");
  assert(rules.size() == 2);
  assert(rules[0] == "*.temp");
  assert(rules[1] == "dir/");
  fs::remove("test_dir/.gitignore_test");
  std::cout << "Test: Load gitignore rules passed\n";
}

void test_is_path_ignored_by_gitignore() {
  Config config;
  config.dirPath = "test_dir";
  std::unordered_map<std::string, std::vector<std::string>> dir_gitignore_rules;

  std::vector<std::string> rules = load_gitignore_rules("test_dir/.gitignore");
  dir_gitignore_rules[normalize_path(config.dirPath)] = rules;

  bool ignored = is_path_ignored_by_gitignore(
      "test_dir/file2.txt", config.dirPath, dir_gitignore_rules);
  if (!ignored) {
    std::cerr << "Assertion failed for test_dir/file2.txt, expected ignored by "
                 "gitignore\n";
  }
  assert(ignored == true);
  assert(is_path_ignored_by_gitignore("test_dir/file1.cpp", config.dirPath,
                                      dir_gitignore_rules) == false);
  assert(is_path_ignored_by_gitignore("test_dir/.hidden_dir/file.cpp",
                                      config.dirPath,
                                      dir_gitignore_rules) == true);
  assert(is_path_ignored_by_gitignore("test_dir/not_ignored_folder/file8.cpp",
                                      config.dirPath,
                                      dir_gitignore_rules) == false);
  assert(is_path_ignored_by_gitignore("test_dir/ignored_folder/file7.cpp",
                                      config.dirPath,
                                      dir_gitignore_rules) == true);

  assert(is_path_ignored_by_gitignore("test_dir/FILE2.TXT", config.dirPath,
                                      dir_gitignore_rules) == true);
  assert(is_path_ignored_by_gitignore("test_dir/file2.TxT", config.dirPath,
                                      dir_gitignore_rules) == true);
  assert(is_path_ignored_by_gitignore("test_dir/file2.tXt", config.dirPath,
                                      dir_gitignore_rules) == true);
  assert(is_path_ignored_by_gitignore("test_dir/file2.TXt", config.dirPath,
                                      dir_gitignore_rules) == true);

  std::cout << "Test: Is path ignored by single gitignore passed\n";
}

void test_is_path_ignored_by_gitignore_multi_level() {
  create_test_directory_gitignore_structure();
  Config config;
  config.dirPath = "test_dir_gitignore";
  std::unordered_map<std::string, std::vector<std::string>> dir_gitignore_rules;
  dir_gitignore_rules[normalize_path(config.dirPath)] =
      load_gitignore_rules(config.dirPath / ".gitignore");
  dir_gitignore_rules[normalize_path(config.dirPath / "subdir1")] =
      load_gitignore_rules(config.dirPath / "subdir1" / ".gitignore");

  auto check_ignored = [&](const fs::path &path) {
    return is_path_ignored_by_gitignore(path, config.dirPath,
                                        dir_gitignore_rules);
  };

  assert(check_ignored("test_dir_gitignore/file1.level1") == true);
  assert(check_ignored("test_dir_gitignore/subdir1/file2.level2") == true);
  assert(check_ignored("test_dir_gitignore/subdir1/important.level2") == false);
  assert(check_ignored("test_dir_gitignore/subdir2/file3.level1") == true);
  assert(check_ignored("test_dir_gitignore/subdir1/file4.txt") == false);

  cleanup_test_directory_gitignore();
  std::cout << "Test: Is path ignored by multi-level gitignore passed\n";
}

void test_is_file_size_valid() {
  create_test_file("test_dir/small_file.txt", "small");
  create_test_file("test_dir/medium_file.txt", std::string(1024, 'M'));
  create_test_file("test_dir/large_file.txt", std::string(2049, 'L'));

  assert(is_file_size_valid("test_dir/small_file.txt", 1024) == true);
  assert(is_file_size_valid("test_dir/medium_file.txt", 1024) == true);
  assert(is_file_size_valid("test_dir/large_file.txt", 2048) == false);
  assert(is_file_size_valid("test_dir/small_file.txt", 0) == true);
  fs::remove("test_dir/small_file.txt");
  fs::remove("test_dir/medium_file.txt");
  fs::remove("test_dir/large_file.txt");
  std::cout << "Test: Is file size valid passed\n";
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
                                   excluded_exts) == false);
  assert(is_file_extension_allowed("test_dir/file.noext", {}, excluded_exts) ==
         true);
  assert(is_file_extension_allowed("test_dir/file.noext", allowed_exts, {}) ==
         false);
  std::cout << "Test: Is file extension allowed passed\n";
}

void test_should_ignore_folder() {
  Config base_config;
  base_config.dirPath = "test_dir";
  std::unordered_map<std::string, std::vector<std::string>> dir_gitignore_rules;
  std::vector<std::string> rules = load_gitignore_rules("test_dir/.gitignore");
  dir_gitignore_rules[normalize_path(base_config.dirPath)] = rules;

  assert(should_ignore_folder("test_dir/.hidden_dir",
                              base_config.disableGitignore, base_config.dirPath,
                              base_config.ignoredFolders,
                              dir_gitignore_rules) == true);
  assert(should_ignore_folder("test_dir/ignored_folder",
                              base_config.disableGitignore, base_config.dirPath,
                              base_config.ignoredFolders,
                              dir_gitignore_rules) == true);
  assert(should_ignore_folder("test_dir/not_ignored_folder",
                              base_config.disableGitignore, base_config.dirPath,
                              base_config.ignoredFolders,
                              dir_gitignore_rules) == false);
  assert(should_ignore_folder("test_dir/subdir1", base_config.disableGitignore,
                              base_config.dirPath, {"subdir1"},
                              dir_gitignore_rules) == true);
  std::cout << "Test: Should ignore folder passed\n";
}

void test_should_ignore_file() {
  Config base_config;
  base_config.dirPath = "test_dir";
  base_config.maxFileSizeB = 2048;
  std::unordered_map<std::string, std::vector<std::string>> dir_gitignore_rules;
  std::vector<std::string> rules = load_gitignore_rules("test_dir/.gitignore");
  dir_gitignore_rules[normalize_path(base_config.dirPath)] = rules;

  create_test_file("test_dir/ignore_me.txt", "ignore");
  create_test_file("test_dir/large_ignore.cpp", std::string(4096, 'I'));

  assert(should_ignore_file("test_dir/file2.txt", base_config.disableGitignore,
                            base_config.dirPath, base_config.maxFileSizeB,
                            base_config.ignoredFiles,
                            dir_gitignore_rules) == true);
  assert(should_ignore_file("test_dir/ignore_me.txt",
                            base_config.disableGitignore, base_config.dirPath,
                            base_config.maxFileSizeB, {"ignore_me.txt"},
                            dir_gitignore_rules) == true);
  assert(should_ignore_file("test_dir/large_file.cpp",
                            base_config.disableGitignore, base_config.dirPath,
                            base_config.maxFileSizeB, base_config.ignoredFiles,
                            dir_gitignore_rules) == true);
  assert(should_ignore_file("test_dir/file1.cpp", base_config.disableGitignore,
                            base_config.dirPath, base_config.maxFileSizeB,
                            base_config.ignoredFiles,
                            dir_gitignore_rules) == false);
  fs::remove("test_dir/ignore_me.txt");
  fs::remove("test_dir/large_ignore.cpp");
  std::cout << "Test: Should ignore file passed\n";
}

void test_matches_regex_filters() {
  std::vector<std::string> regex_filters = {"file[0-9]\\.cpp", "\\.hpp$"};
  assert(matches_regex_filters("test_dir/file1.cpp", regex_filters) == true);
  assert(matches_regex_filters("test_dir/FILE3.HPP", regex_filters) == false);
  assert(matches_regex_filters("test_dir/file2.txt", regex_filters) == false);
  assert(matches_regex_filters("test_dir/no_match.txt", {}) == false);
  std::cout << "Test: Matches regex filters passed\n";
}

void test_matches_filename_regex_filters() {
  std::vector<std::string> filename_regex_filters = {"file_a.*\\.cpp",
                                                     "file_d.*"};
  assert(matches_filename_regex_filters("test_dir/file_abc.cpp",
                                        filename_regex_filters) == true);
  assert(matches_filename_regex_filters("test_dir/file_def.cpp",
                                        filename_regex_filters) == true);
  assert(matches_filename_regex_filters("test_dir/file_def.txt",
                                        filename_regex_filters) ==
         true); // matches file_d.*
  assert(matches_filename_regex_filters("test_dir/file1.cpp",
                                        filename_regex_filters) == false);
  assert(matches_filename_regex_filters("test_dir/misc.txt",
                                        filename_regex_filters) == false);
  assert(matches_filename_regex_filters("test_dir/no_match.txt", {}) == true);
  std::cout << "Test: Matches filename regex filters passed\n";
}

void test_remove_cpp_comments() {
  std::string code_with_comments = "// Line comment\nint /* block */ main() "
                                   "{\n  return 0; // End comment\n}";
  std::string code_no_comments = remove_cpp_comments(code_with_comments);
  assert(code_no_comments.find("//") == std::string::npos);
  assert(code_no_comments.find("/*") == std::string::npos);
  assert(code_no_comments.find("block") == std::string::npos);
  std::cout << "Test: Remove cpp comments passed\n";
}

void test_format_file_output() {
  std::string content = "File content lines\n\nSecond line.";
  std::string formatted_output = format_file_output(
      "test_dir/output_file.cpp", false, "test_dir", content, false, false);
  std::string expected_output = "\n## File: output_file.cpp\n\n```cpp\nFile "
                                "content lines\n\nSecond line.\n\n```\n";
  assert(formatted_output == expected_output);

  formatted_output = format_file_output("test_dir/output_file.cpp", true,
                                        "test_dir", content, true, false);
  expected_output = "\n## File: output_file.cpp\n\n```cpp\nFile content "
                    "lines\nSecond line.\n\n```\n";

  assert(formatted_output == expected_output);

  std::cout << "Test: Format file output passed\n";
}

void test_process_single_file() {
  create_test_file("test_dir/process_test.cpp",
                   "// Test file\nint main() { return 1; }");
  std::string output =
      process_single_file("test_dir/process_test.cpp", 0, false, false,
                          "test_dir", false, false, false);
  assert(output.find("Test file") != std::string::npos);
  assert(output.find("```cpp") != std::string::npos);

  std::string output_no_comments =
      process_single_file("test_dir/process_test.cpp", 0, true, false,
                          "test_dir", false, false, false);
  assert(output_no_comments.find("Test file") == std::string::npos);

  fs::remove("test_dir/process_test.cpp");
  std::cout << "Test: Process single file passed\n";
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
  std::cout << "Test: Is last file passed\n";
}

void test_collect_files_normal() {
  Config config;
  config.dirPath = "test_dir";
  std::atomic<bool> should_stop{false};
  auto [normal_files, last_files] = collect_files(config, should_stop);

  int normal_count = 0;
  for (const auto &file : normal_files) {
    if (file.filename() != "file2.txt" &&
        file.filename() != ".hidden_file.cpp" &&
        file.filename() != ".gitignore" &&
        file.string().find("ignored_folder") == std::string::npos &&
        file.string().find(".hidden_dir") == std::string::npos) {
      normal_count++;
    }
  }
  assert(normal_count == 7);

  std::cout << "Test: Collect files normal passed\n";
}

void test_collect_files_filename_regex_filter() {
  Config config;
  config.dirPath = "test_dir";
  config.filenameRegexFilters = {"file_a.*\\.cpp", "file_d.*"};
  std::atomic<bool> should_stop{false};
  auto [normal_files, last_files] = collect_files(config, should_stop);

  assert(normal_files.size() == 2);
  bool found_abc = false;
  bool found_def = false;
  for (const auto &file : normal_files) {
    if (file.filename() == "file_abc.cpp")
      found_abc = true;
    if (file.filename() == "file_def.cpp")
      found_def = true;
  }
  assert(found_abc);
  assert(found_def);
  std::cout << "Test: Collect files with filename regex filter passed\n";
}

void test_collect_files_only_last() {
  Config config;
  config.dirPath = "test_dir";
  config.onlyLast = true;
  config.lastFiles = {"file2.txt", "file1.cpp"};
  config.disableGitignore = true;

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

  std::cout << "Test: Collect files only last passed\n";
}

void test_process_file_chunk_ordered() {
  Config config;
  config.dirPath = "test_dir";

  std::vector<fs::path> files_to_process = {"test_dir/file1.cpp",
                                            "test_dir/FILE3.HPP"};
  std::vector<std::pair<fs::path, std::string>> results;
  std::atomic<size_t> processed_files{0};
  std::atomic<size_t> total_bytes{0};
  std::mutex console_mutex;
  std::mutex ordered_results_mutex;
  std::atomic<bool> should_stop{false};
  std::vector<std::pair<fs::path, std::string>> thread_local_results;

  capture_stdout([&]() {
    process_file_chunk(
        std::span{files_to_process.begin(), files_to_process.end()},
        config.removeComments, config.maxFileSizeB, config.showFilenameOnly,
        config.dirPath, config.removeEmptyLines, results, processed_files,
        total_bytes, console_mutex, ordered_results_mutex, should_stop,
        std::cout, false, false, thread_local_results);
  });

  assert(processed_files == 2);
  assert(total_bytes > 0);
  assert(results.size() == 2); // Results are expected in ordered case
  assert(results[0].first.filename() == "file1.cpp");
  assert(results[1].first.filename() == "FILE3.HPP");
  std::cout << "Test: Process file chunk ordered passed\n";
}

void test_output_to_file() {
  Config config;
  config.dirPath = "test_dir";
  config.outputFile = "test_output.txt";
  std::atomic<bool> should_stop{false};

  std::string expected_content_part = "File: file1.cpp";

  capture_stdout([&]() { process_directory(config, should_stop); });

  std::ifstream outputFileStream("test_output.txt");
  std::string output_file_content(
      (std::istreambuf_iterator<char>(outputFileStream)),
      std::istreambuf_iterator<char>());

  assert(outputFileStream.is_open());
  assert(output_file_content.find(expected_content_part) != std::string::npos);

  outputFileStream.close();
  fs::remove("test_output.txt");
  std::cout << "Test: Output to file passed\n";
}

void test_output_file_creation() {
  Config config;
  config.dirPath = "test_dir";
  config.outputFile = "non_existent_output_file.txt";
  std::atomic<bool> should_stop{false};

  // Ensure the output file does not exist before the test
  fs::remove("non_existent_output_file.txt");
  assert(!fs::exists("non_existent_output_file.txt"));

  std::string expected_content_part = "File: file1.cpp";

  capture_stdout([&]() { process_directory(config, should_stop); });

  // Check if the output file is created
  assert(fs::exists("non_existent_output_file.txt"));

  std::ifstream outputFileStream("non_existent_output_file.txt");
  std::string output_file_content(
      (std::istreambuf_iterator<char>(outputFileStream)),
      std::istreambuf_iterator<char>());

  assert(outputFileStream.is_open());
  assert(output_file_content.find(expected_content_part) != std::string::npos);

  outputFileStream.close();
  fs::remove("non_existent_output_file.txt");
  std::cout << "Test: Output to non-existent file creates file passed\n";
}

void test_dry_run_mode() {
  Config config;
  config.dirPath = "test_dir";
  config.dryRun = true;
  std::atomic<bool> should_stop{false};

  std::string output =
      capture_stdout([&]() { process_directory(config, should_stop); });

  assert(output.find("Files to be processed:") != std::string::npos);
  assert(output.find("file1.cpp") != std::string::npos);
  assert(output.find("FILE3.HPP") != std::string::npos);
  assert(output.find("file5") ==
         std::string::npos); // files with no extentions are currently ignored
  assert(output.find("subdir1/file6.cpp") != std::string::npos);
  assert(output.find("not_ignored_folder/file8.cpp") != std::string::npos);
  assert(output.find("file2.txt") == std::string::npos); // gitignore
  assert(output.find(".hidden_file.cpp") !=
         std::string::npos); // .hidden_file.cpp is not a dot folder
  assert(output.find("ignored_folder/file7.cpp") ==
         std::string::npos); // ignored folder and gitignore
  assert(output.find("file4.excluded") !=
         std::string::npos); // .excluded is not excluded
  assert(output.find("## File:") ==
         std::string::npos); // No file content formatting

  // Test filename regex filter in dry run mode
  Config regex_config = config;
  regex_config.filenameRegexFilters = {"file_a.*\\.cpp", "file_d.*"};
  std::string regex_output =
      capture_stdout([&]() { process_directory(regex_config, should_stop); });
  assert(regex_output.find("file_abc.cpp") != std::string::npos);
  assert(regex_output.find("file_def.cpp") != std::string::npos);
  assert(regex_output.find("file1.cpp") ==
         std::string::npos); // Should be filtered out

  std::cout << "Dry run mode test passed\n";
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
    test_matches_filename_regex_filters();
    test_remove_cpp_comments();
    test_format_file_output();
    test_process_single_file();
    test_is_last_file();
    test_collect_files_normal();
    test_collect_files_filename_regex_filter();
    test_collect_files_only_last();
    test_process_file_chunk_ordered(); // Renamed and corrected test
    test_output_to_file();
    test_output_file_creation();
    test_format_file_output_line_numbers();
    test_is_path_ignored_by_gitignore_multi_level();
    test_dry_run_mode();

    cleanup_test_directory();
    std::cout << "\nAll tests passed successfully!\n";
    return 0;

  } catch (const std::exception &e) {
    std::cerr << "Test failed: " << e.what() << std::endl;
    cleanup_test_directory();
    return 1;
  }
}
