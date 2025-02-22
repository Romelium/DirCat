#include "./lib.cpp"
#include <csignal>
#include <iostream>

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
    return process_file(config.dirPath, config, std::cout) ? 0 : 1;
  } else if (fs::is_directory(config.dirPath)) {
    return process_directory(config, shouldStop) ? 0 : 1;
  } else {
    std::cerr << "ERROR: Invalid path type: " << config.dirPath.string()
              << ". Expecting file or directory.\n";
    return 1;
  }
}
