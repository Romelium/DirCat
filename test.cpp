#include <cassert>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <thread>
#include <chrono>

#include "lib.cpp"

namespace fs = std::filesystem;

// Helper functions for testing
void create_test_file(const fs::path& path, const std::string& content) {
    std::ofstream file(path);
    file << content;
}

void create_test_directory_structure() {
    // Create test directory structure
    fs::create_directories("test_dir/subdir1");
    fs::create_directories("test_dir/subdir2");
    fs::create_directories("test_dir/.hidden_dir");
    
    // Create test files
    create_test_file("test_dir/file1.cpp", "int main() { return 0; } // Comment\n");
    create_test_file("test_dir/file2.txt", "Plain text file\n");
    create_test_file("test_dir/subdir1/file3.cpp", "void func() { /* Comment */ }\n");
    create_test_file("test_dir/.hidden_file.cpp", "Hidden file\n");
    create_test_file("test_dir/.gitignore", "*.txt\n.hidden_dir/\n");
}

void cleanup_test_directory() {
    fs::remove_all("test_dir");
}

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
    config.fileExtensions = {"cpp"};
    
    std::atomic<bool> should_stop{false};
    auto [normal_files, last_files] = collect_files(config, should_stop);
    
    bool found_cpp = false;
    bool found_txt = false;
    for (const auto& file : normal_files) {
        if (file.extension() == ".cpp") found_cpp = true;
        if (file.extension() == ".txt") found_txt = true;
    }
    
    assert(found_cpp == true);
    assert(found_txt == false);
    std::cout << "File extension filtering test passed\n";
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
    
    fs::path test_txt_file = "test_dir/ignored.txt";
    fs::path test_cpp_file = "test_dir/not_ignored.cpp";
    
    assert(is_path_ignored_by_gitignore(test_txt_file, config.gitignoreRules, config.dirPath) == true);
    assert(is_path_ignored_by_gitignore(test_cpp_file, config.gitignoreRules, config.dirPath) == false);
    std::cout << "Gitignore rules test passed\n";
}

void test_last_files_processing() {
    Config config;
    config.dirPath = "test_dir";
    config.lastFiles = {"file2.txt"};
    
    std::atomic<bool> should_stop{false};
    auto [normal_files, last_files] = collect_files(config, should_stop);
    
    bool found_in_last = false;
    for (const auto& file : last_files) {
        if (file.filename() == "file2.txt") found_in_last = true;
    }
    
    assert(found_in_last == true);
    std::cout << "Last files processing test passed\n";
}

void test_regex_filtering() {
    Config config;
    config.dirPath = "test_dir";
    config.regexFilters = {"^file[0-9]"};
    
    std::atomic<bool> should_stop{false};
    auto [normal_files, last_files] = collect_files(config, should_stop);
    
    for (const auto& file : normal_files) {
        assert(!std::regex_match(file.filename().string(), std::regex("^file[0-9]")));
    }
    std::cout << "Regex filtering test passed\n";
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
    
    process_directory(config, should_stop);
    
    interrupt_thread.join();
    assert(should_stop == true);
    std::cout << "Interrupt handling test passed\n";
}

int main() {
    try {
        // Setup test environment
        cleanup_test_directory();  // Clean up any existing test directory
        create_test_directory_structure();
        
        // Run tests
        test_basic_configuration();
        test_file_extension_filtering();
        test_comment_removal();
        test_gitignore_rules();
        test_last_files_processing();
        test_regex_filtering();
        test_interrupt_handling();
        
        // Cleanup
        cleanup_test_directory();
        
        std::cout << "All tests passed successfully!\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << '\n';
        cleanup_test_directory();
        return 1;
    }
}