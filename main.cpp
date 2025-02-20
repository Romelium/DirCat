#include "./lib.cpp"
#include <csignal>

int main(int argc, char *argv[]) {
  Config config = parse_arguments(argc, argv);
  std::atomic<bool> shouldStop{false};
  globalShouldStop = &shouldStop;
  std::signal(SIGINT, signalHandler);

  return process_directory(std::move(config), shouldStop) ? 0 : 1;
}
