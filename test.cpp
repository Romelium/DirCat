#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <regex>
#include <string>
#include <thread>

#include "lib.cpp"

namespace fs = std::filesystem;

// Helper functions for testing
void create_test_file(const fs::path &path, const std::string &content) {
  std::ofstream file(path);
  file << content;
}

void create_test_directory_structure() {
  // Create test directory structure
  fs::create_directories("test_dir/subdir1");
  fs::create_directories("test_dir/subdir2");
  fs::create_directories("test_dir/.hidden_dir");
  fs::create_directories("test_dir/ignored_folder");

  // Create test files
  create_test_file("test_dir/file1.cpp",
                   "int main() { return 0; } // Comment\n");
  create_test_file("test_dir/file2.txt", "Plain text file\n");
  create_test_file(
      "test_dir/file3.hpp",
      "#ifndef FILE3_HPP\n#define FILE3_HPP\n// Header file\n#endif");
  create_test_file("test_dir/file4.excluded", "Excluded file content\n");
  create_test_file("test_dir/file5",
                   "No extension file\n"); // File with no extension
  create_test_file("test_dir/subdir1/file6.cpp",
                   "void func() { /* Comment */ }\n");
  create_test_file("test_dir/.hidden_file.cpp", "Hidden file\n");
  create_test_file("test_dir/ignored_folder/file7.cpp",
                   "Ignored folder file\n");
  create_test_file("test_dir/large_file.cpp",
                   std::string(2049, 'A')); // Slightly larger than 2KB
  create_test_file(
      "test_dir/.gitignore",
      "*.txt\n.hidden_dir/\nignored_folder/\n!not_ignored_folder/\n");
  create_test_file("test_dir/not_ignored_folder/file8.cpp",
                   "Not ignored by negation\n");
}

void cleanup_test_directory() { fs::remove_all("test_dir"); }

// Test cases
void test_basic_configuration() {
  Config config;
  config.dirPath = "test_dir";
  assert(config.recursiveSearch == true);
  assert(config.maxFileSizeB == 0);
  assert(config.ignoreDotFolders == true);
  std::cout << "Basic configuration test passed\n";
}

void test_file_extension_filtering() {
  Config config;
  config.dirPath = "test_dir";
  config.fileExtensions = {"cpp", "hpp"};

  std::atomic<bool> should_stop{false};
  auto [normal_files, last_files] = collect_files(config, should_stop);

  int cpp_count = 0;
  int hpp_count = 0;
  int txt_count = 0;
  for (const auto &file : normal_files) {
    if (file.extension() == ".cpp")
      cpp_count++;
    if (file.extension() == ".hpp")
      hpp_count++;
    if (file.extension() == ".txt")
      txt_count++;
  }

  assert(cpp_count ==
         5); // .hidden_file.cpp, file1.cpp, ignored_folder/file7.cpp,
             // large_file.cpp, subdir1/file6.cpp
  assert(hpp_count == 1); // file3.hpp
  assert(txt_count == 0);
  std::cout << "File extension filtering test passed\n";
}

void test_excluded_file_extension_filtering() {
  Config config;
  config.dirPath = "test_dir";
  config.excludedFileExtensions = {
      "excluded", ""}; // Exclude .excluded and no extension files

  std::atomic<bool> should_stop{false};
  auto [normal_files, last_files] = collect_files(config, should_stop);

  bool found_excluded = false;
  bool found_no_ext = false;
  for (const auto &file : normal_files) {
    if (file.extension() == ".excluded")
      found_excluded = true;
    if (file.extension().empty())
      found_no_ext = true;
  }

  assert(found_excluded == false);
  assert(found_no_ext == false);
  std::cout << "Excluded file extension filtering test passed\n";
}

void test_comment_removal() {
  std::string code = "int main() { // Line comment\n"
                     "  /* Block comment */\n"
                     "  return 0;\n"
                     "}\n";
  std::string cleaned = remove_cpp_comments(code);
  assert(cleaned.find("//") == std::string::npos);
  assert(cleaned.find("/*") == std::string::npos);
  std::cout << "Comment removal test passed\n";
}

void test_gitignore_rules() {
  Config config;
  config.dirPath = "test_dir";
  config.gitignorePath = "test_dir/.gitignore";
  config.gitignoreRules = load_gitignore_rules(config.gitignorePath);

  fs::path test_txt_file = "test_dir/file2.txt";
  fs::path test_cpp_file = "test_dir/file1.cpp";
  fs::path hidden_dir_file = "test_dir/.hidden_dir/some_file.cpp";
  fs::path ignored_folder_file = "test_dir/ignored_folder/file7.cpp";
  fs::path not_ignored_folder_file = "test_dir/not_ignored_folder/file8.cpp";

  assert(is_path_ignored_by_gitignore(test_txt_file, config.gitignoreRules,
                                      config.dirPath) == true);
  assert(is_path_ignored_by_gitignore(test_cpp_file, config.gitignoreRules,
                                      config.dirPath) == false);
  assert(is_path_ignored_by_gitignore(not_ignored_folder_file,
                                      config.gitignoreRules, config.dirPath) ==
         false); // Negation check

  std::cout << "Gitignore rules test passed\n";
}

void test_last_files_processing() {
  Config config;
  config.dirPath = "test_dir";
  config.lastFiles = {"file2.txt", "file3.hpp"};

  std::atomic<bool> should_stop{false};
  auto [normal_files, last_files] = collect_files(config, should_stop);

  bool found_txt_in_last = false;
  bool found_hpp_in_last = false;
  for (const auto &file : last_files) {
    if (file.filename() == "file2.txt")
      found_txt_in_last = true;
    if (file.filename() == "file3.hpp")
      found_hpp_in_last = true;
  }

  assert(found_txt_in_last == true);
  assert(found_hpp_in_last == true);
  std::cout << "Last files processing test passed\n";
}

void test_only_last_option_no_gitignore() {
  Config config;
  config.dirPath = "test_dir";
  config.onlyLast = true;
  config.disableGitignore = true; // Disable gitignore
  config.lastFiles = {
      "file1.cpp",
      "file2.txt"}; // file2.txt is now included because gitignore is disabled

  std::atomic<bool> should_stop{false};
  auto [normal_files, last_files] = collect_files(config, should_stop);

  assert(normal_files.empty());
  assert(last_files.size() == 2); // Both files should be in last_files
  bool found_cpp_in_last = false;
  bool found_txt_in_last = false;
  for (const auto &file : last_files) {
    if (file.filename() == "file1.cpp")
      found_cpp_in_last = true;
    if (file.filename() == "file2.txt")
      found_txt_in_last = true;
  }
  assert(found_cpp_in_last);
  assert(found_txt_in_last);

  std::cout << "Only last option no gitignore test passed\n";
}

void test_regex_filtering() {
  Config config;
  config.dirPath = "test_dir";
  config.regexFilters = {
      "^file[0-9]\\.(cpp|hpp)$"}; // Regex to match file1.cpp, file3.hpp,
                                  // file6.cpp etc.

  std::atomic<bool> should_stop{false};
  auto [normal_files, last_files] = collect_files(config, should_stop);

  int filtered_count = 0;
  for (const auto &file : normal_files) {
    if (std::regex_search(file.filename().string(),
                          std::regex("^file[0-9]\\.(cpp|hpp)$"))) {
      filtered_count++;
    }
  }
  assert(filtered_count ==
         0); // No files should match because regex filters *exclude* files
  std::cout << "Regex filtering test passed\n";
}

void test_ignore_folders_and_files() {
  Config config;
  config.dirPath = "test_dir";
  config.ignoredFolders = {"subdir1", "ignored_folder"};
  config.ignoredFiles = {"file2.txt", ".hidden_file.cpp"};

  std::atomic<bool> should_stop{false};
  auto [normal_files, last_files] = collect_files(config, should_stop);

  bool found_subdir1_file = false;
  bool found_ignored_folder_file = false;
  bool found_file2_txt = false;
  bool found_hidden_file_cpp = false;

  for (const auto &file : normal_files) {
    if (file.string().find("subdir1") != std::string::npos)
      found_subdir1_file = true;
    if (file.string().find("ignored_folder") != std::string::npos)
      found_ignored_folder_file = true;
    if (file.filename() == "file2.txt")
      found_file2_txt = true;
    if (file.filename() == ".hidden_file.cpp")
      found_hidden_file_cpp = true;
  }

  assert(found_subdir1_file == false);
  assert(found_ignored_folder_file == false);
  assert(found_file2_txt == false);
  assert(found_hidden_file_cpp == false);
  std::cout << "Ignore folders and files test passed\n";
}

void test_disable_gitignore() {
  Config config;
  config.dirPath = "test_dir";
  config.disableGitignore = true;

  std::atomic<bool> should_stop{false};
  auto [normal_files, last_files] = collect_files(config, should_stop);

  bool found_txt = false; // file2.txt is normally gitignored
  for (const auto &file : normal_files) {
    if (file.extension() == ".txt")
      found_txt = true;
  }
  assert(found_txt ==
         true); // Should find .txt file because gitignore is disabled
  std::cout << "Disable gitignore test passed\n";
}

void test_max_file_size_limit() {
  Config config;
  config.dirPath = "test_dir";
  config.maxFileSizeB = 2048; // 2KB limit

  std::atomic<bool> should_stop{false};
  auto [normal_files, last_files] = collect_files(config, should_stop);

  bool found_large_file = false;
  for (const auto &file : normal_files) {
    if (file.filename() == "large_file.cpp")
      found_large_file = true;
  }
  assert(found_large_file == false); // Large file should be excluded
  std::cout << "Max file size limit test passed\n";
}

void test_non_recursive_search() {
  Config config;
  config.dirPath = "test_dir";
  config.recursiveSearch = false;

  std::atomic<bool> should_stop{false};
  auto [normal_files, last_files] = collect_files(config, should_stop);

  bool found_subdir_file = false;
  for (const auto &file : normal_files) {
    if (file.string().find("subdir1") != std::string::npos)
      found_subdir_file = true;
  }
  assert(found_subdir_file == false); // No files from subdir should be found
  std::cout << "Non-recursive search test passed\n";
}

std::string capture_stdout(const std::function<void()> &func) {
  std::stringstream buffer;
  std::streambuf *oldCout = std::cout.rdbuf();
  std::cout.rdbuf(buffer.rdbuf());
  func();
  std::cout.rdbuf(oldCout);
  return buffer.str();
}

void test_interrupt_handling() {
  Config config;
  config.dirPath = "test_dir";

  std::atomic<bool> should_stop{false};
  globalShouldStop = &should_stop;

  // Simulate interrupt after a short delay
  std::thread interrupt_thread([&should_stop]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    should_stop = true;
  });

  std::string _ =
      capture_stdout([&]() { process_directory(config, should_stop); });

  interrupt_thread.join();
  assert(should_stop == true);
  std::cout << "Interrupt handling test passed\n";
}

int main() {
  try {
    // Setup test environment
    cleanup_test_directory(); // Clean up any existing test directory
    create_test_directory_structure();

    // Run tests
    test_basic_configuration();
    test_file_extension_filtering();
    test_excluded_file_extension_filtering();
    test_comment_removal();
    test_gitignore_rules();
    test_last_files_processing();
    test_only_last_option_no_gitignore();
    test_regex_filtering();
    test_ignore_folders_and_files();
    test_disable_gitignore();
    test_max_file_size_limit();
    test_non_recursive_search();
    test_interrupt_handling();

    // Cleanup
    cleanup_test_directory();

    std::cout << "All tests passed successfully!\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Test failed with exception: " << e.what() << '\n';
    cleanup_test_directory();
    return 1;
  }
}