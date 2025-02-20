#include "./lib.cpp"
#include <csignal>

int main(int argc, char *argv[]) {
  Config config = parse_arguments(argc, argv);
  std::atomic<bool> shouldStop{false};
  globalShouldStop = &shouldStop;
  std::signal(SIGINT, signalHandler);

  if (!fs::exists(config.dirPath)) {
    std::cerr << "ERROR: Path does not exist: " << config.dirPath.string()
              << '\n';
    return 1;
  }

  if (fs::is_regular_file(config.dirPath)) {
    config.showFilenameOnly = true;
    std::string file_content = process_single_file(config.dirPath, config);
    if (!file_content.empty()) {
      std::cout << file_content;
    }
    return 0;
  } else if (fs::is_directory(config.dirPath)) {
    if (process_directory(config, shouldStop)) {
      return 0;
    } else {
      return 1;
    }
  } else {
    std::cerr << "ERROR: Invalid path type: " << config.dirPath.string()
              << ". Expecting file or directory.\n";
    return 1;
  }
}
