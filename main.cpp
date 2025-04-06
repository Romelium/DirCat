#include "./lib.cpp" // Include all definitions from lib.cpp

// Standard Headers needed by main itself
#include <atomic>   // For shouldStop
#include <csignal>  // For signal handling
#include <fstream>  // For std::ofstream
#include <iostream> // For std::cout, std::cerr

// Filesystem included via lib.cpp, but good practice to include if used
// directly
#include <filesystem>
// Alias namespace locally if needed, lib.cpp already does this
// namespace fs = std::filesystem;

int main(int argc, char *argv[]) {
  // 1. Parse Arguments
  Config config = parse_arguments(argc, argv);

  // 2. Setup Signal Handling
  std::atomic<bool> shouldStop{false};
  globalShouldStop = &shouldStop;     // globalShouldStop is defined in lib.cpp
  std::signal(SIGINT, signalHandler); // signalHandler is defined in lib.cpp

  // 3. Setup Output Stream (Handled within processing functions now)
  //    We still need to handle the *initial* setup if -o is used,
  //    but the actual writing logic is passed the stream.
  std::ofstream outputFileStream;
  std::ostream *outputPtr = &std::cout; // Default to stdout

  if (!config.outputFile.empty()) {
    // Resolve potential relative path for output file
    fs::path absOutputPath = fs::absolute(config.outputFile);
    fs::path parentPath = absOutputPath.parent_path();

    // Create output directory if it doesn't exist
    if (!parentPath.empty() && !fs::exists(parentPath)) {
      try {
        fs::create_directories(parentPath);
        // Optionally notify user (only if initial output was console)
        if (outputPtr == &std::cout) {
          std::cout << "Info: Created output directory: "
                    << normalize_path(parentPath) << std::endl;
        }
      } catch (const std::exception &e) {
        std::cerr << "ERROR: Failed to create output directory "
                  << normalize_path(parentPath) << ": " << e.what() << '\n';
        return 1; // Exit if cannot create directory
      }
    }

    // Check if output path is an existing directory
    if (fs::exists(absOutputPath) && fs::is_directory(absOutputPath)) {
      std::cerr << "ERROR: Output path is an existing directory: "
                << normalize_path(absOutputPath) << '\n';
      return 1;
    }

    // Open the file stream (binary mode for consistent cross-platform newline
    // handling, truncate)
    outputFileStream.open(absOutputPath,
                          std::ios::binary | std::ios::out | std::ios::trunc);
    if (!outputFileStream.is_open()) {
      std::cerr << "ERROR: Could not open output file for writing: "
                << normalize_path(absOutputPath) << '\n';
      return 1; // Exit if file cannot be opened
    }
    outputPtr = &outputFileStream; // Point to the file stream
  }
  // Use a reference for convenience - processing functions will use this
  std::ostream &output_stream = *outputPtr;

  // 4. Check Input Path (already checked for existence in parse_arguments)
  bool success = false;
  try {
    if (fs::is_regular_file(config.dirPath)) {
      // Call processing function for a single file, passing the output stream
      success = process_single_file_entry(config, output_stream);
    } else if (fs::is_directory(config.dirPath)) {
      // Call processing function for a directory.
      // process_directory now handles its own output stream setup based on
      // config.outputFile
      success = process_directory(config, shouldStop);
    } else {
      // Handle other file types (symlinks, sockets, etc.) if necessary, or
      // error out
      std::cerr << "ERROR: Invalid input path type: "
                << normalize_path(config.dirPath)
                << ". Expecting a regular file or directory.\n";
      return 1;
    }
  } catch (const std::exception &e) {
    // Catch potential exceptions from processing functions (though they should
    // handle internal errors)
    std::cerr << "ERROR: Unhandled exception during processing: " << e.what()
              << std::endl;
    success = false;
  } catch (...) {
    std::cerr << "ERROR: Unknown unhandled exception during processing."
              << std::endl;
    success = false;
  }

  // 5. Cleanup and Return Status
  //    The process_directory function now handles closing the file and
  //    reporting success/failure messages, including the output file path.
  //    We only need to handle the case where the stream was opened here but
  //    processing failed *before* process_directory/process_single_file_entry
  //    could handle it (unlikely with current structure, but good practice).
  if (outputFileStream.is_open() && !success) {
    outputFileStream.close();
  }

  return success ? 0 : 1; // Return 0 on success, 1 on failure
}