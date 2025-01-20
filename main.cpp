#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <mutex>
#include <span>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

class DirectoryProcessor {
public:
  struct Config {
    fs::path dirPath;
    size_t maxFileSizeMB = 10;
    bool recursiveSearch = true;
    std::vector<std::string> fileExtensions;
  };

private:
  const Config config;
  std::atomic<bool> shouldStop{false};
  std::atomic<size_t> processedFiles{0};
  std::atomic<size_t> totalBytes{0};
  mutable std::mutex consoleMutex;
  const std::chrono::steady_clock::time_point startTime{
      std::chrono::steady_clock::now()};
  const unsigned int threadCount;

  class FileProcessor {
    DirectoryProcessor &processor;
    const fs::path &path;
    std::stringstream content;

    bool readFile() {
      std::ifstream file(path, std::ios::binary);
      if (!file)
        return false;

      content << "\n### File: " << path.filename().string() << "\n```";
      if (path.has_extension()) {
        content << path.extension().string().substr(1);
      }
      content << "\n";

      // Read file content in chunks to handle large files better
      constexpr size_t BUFFER_SIZE = 8192;
      char buffer[BUFFER_SIZE];
      bool firstLine = true;

      while (file.good() && !processor.shouldStop) {
        file.read(buffer, BUFFER_SIZE);
        const std::string_view chunk(buffer, file.gcount());

        size_t pos = 0, next;
        while ((next = chunk.find('\n', pos)) != std::string::npos) {
          if (!firstLine)
            content << '\n';
          content.write(chunk.data() + pos, next - pos);
          firstLine = false;
          pos = next + 1;
        }
        if (pos < chunk.size()) {
          if (!firstLine)
            content << '\n';
          content.write(chunk.data() + pos, chunk.size() - pos);
          firstLine = false;
        }
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
        if (!processor.isFileSizeValid(path) || !readFile()) {
          processor.logError("Failed to process: " + path.string());
          return false;
        }
        processor.printToConsole(content.str());
        return true;
      } catch (const std::exception &e) {
        processor.logError(
            std::format("Error processing {}: {}", path.string(), e.what()));
        return false;
      }
    }
  };

  bool isFileSizeValid(const fs::path &path) const {
    try {
      return fs::file_size(path) <= (config.maxFileSizeMB * 1024 * 1024);
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

  void printToConsole(const std::string &message) const {
    std::lock_guard<std::mutex> lock(consoleMutex);
    std::cout << message;
  }

  void logError(const std::string &message) const {
    std::lock_guard<std::mutex> lock(consoleMutex);
    std::cerr << "ERROR: " << message << '\n';
  }

  std::vector<fs::path> collectFiles() const {
    std::vector<fs::path> files;
    try {
      if (config.recursiveSearch) {
        for (const auto &entry :
             fs::recursive_directory_iterator(config.dirPath, fs::directory_options::skip_permission_denied)) {
          if (shouldStop)
            break;
          if (fs::is_regular_file(entry) && isFileExtensionAllowed(entry)) {
            files.push_back(entry.path());
          }
        }
      } else {
        for (const auto &entry :
             fs::directory_iterator(config.dirPath, fs::directory_options::skip_permission_denied)) {
          if (shouldStop)
            break;
          if (fs::is_regular_file(entry) && isFileExtensionAllowed(entry)) {
            files.push_back(entry.path());
          }
        }
      }
      std::sort(files.begin(), files.end());
    } catch (const fs::filesystem_error &e) {
      logError("Error scanning directory: " + std::string(e.what()));
    }
    return files;
  }

  void processFileChunk(std::span<const fs::path> chunk) {
    for (const auto &path : chunk) {
      if (shouldStop)
        break;
      if (FileProcessor(*this, path).process()) {
        ++processedFiles;
      }
    }
  }

public:
  DirectoryProcessor(Config cfg)
      : config(std::move(cfg)),
        threadCount(std::min(std::thread::hardware_concurrency(),
                             static_cast<unsigned int>(8))) {
  } // Limit max threads
  void stop() { shouldStop = true; }

  bool process() {
    if (!fs::exists(config.dirPath) || !fs::is_directory(config.dirPath)) {
      logError("Invalid directory path: " + config.dirPath.string());
      return false;
    }

    auto files = collectFiles();
    if (files.empty()) {
      std::cout << "No matching files found in: " << config.dirPath << '\n';
      return true;
    }

    std::vector<std::thread> threads;
    const size_t filesPerThread =
        (files.size() + threadCount - 1) / threadCount;

    for (size_t i = 0; i < threadCount && i * filesPerThread < files.size();
         ++i) {
      const size_t start = i * filesPerThread;
      const size_t end = std::min(start + filesPerThread, files.size());

      threads.emplace_back([this, &files, start, end] {
        processFileChunk(std::span(files.begin() + start, files.begin() + end));
      });
    }

    for (auto &thread : threads) {
      thread.join();
    }

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
        << "  -m, --max-size <MB>    Maximum file size in MB (default: 10)\n"
        << "  -n, --no-recursive     Disable recursive search\n"
        << "  -e, --ext <ext>        Process only files with given extension\n";
    return 1;
  }

  try {
    DirectoryProcessor::Config config;
    config.dirPath = argv[1];

    // Parse command line options
    for (int i = 2; i < argc; ++i) {
      std::string_view arg = argv[i];
      if ((arg == "-m" || arg == "--max-size") && i + 1 < argc) {
        config.maxFileSizeMB = std::stoull(argv[++i]);
      } else if (arg == "-n" || arg == "--no-recursive") {
        config.recursiveSearch = false;
      } else if ((arg == "-e" || arg == "--ext") && i + 1 < argc) {
        config.fileExtensions.push_back(argv[++i]);
      }
    }

    DirectoryProcessor processor(std::move(config));
    globalProcessor = &processor;

    // Register signal handler
    std::signal(SIGINT, signalHandler);

    return processor.process() ? 0 : 1;

  } catch (const std::exception &e) {
    std::cerr << "Fatal error: " << e.what() << '\n';
  }
}