# DirCat

DirCat is a high-performance C++ utility that concatenates and displays the contents of files in a directory, similar to the Unix `cat` command but for entire directories. It supports multi-threaded processing, recursive directory traversal, file filtering, and various output formatting options.

## Usage

```bash
./dircat <directory_path> [options]
```

### Options

- `-m, --max-size <bytes>`: Maximum file size in bytes. Files larger than this size will be ignored. Default: no limit.
- `-n, --no-recursive`: Disable recursive directory search. Only process files in the top-level directory.
- `-e, --ext <ext>`: Process only files with the specified extension. Can be used multiple times to include multiple extensions, grouped together (e.g., `-e cpp h`).
- `-x, --exclude-ext <ext>`: Exclude files with the specified extension. Can be used multiple times to exclude multiple extensions, grouped together (e.g., `-x tmp log`).
- `-d, --dot-folders`: Include folders starting with a dot (e.g., `.git`, `.vscode`), which are ignored by default.
- `-i, --ignore <item>`: Ignore specific folders or files. Paths are relative to the input directory. Can be used multiple times to ignore multiple items, grouped together (e.g., `-i build temp.txt`). For directories, all files and subdirectories within the ignored directory will be skipped.
- `-r, --regex <pattern>`: Exclude files where the filename matches the given regex pattern.  Regex is applied to the filename only, not the full path. Can be used multiple times to specify multiple regex patterns, grouped together (e.g., `-r "\.tmp$" "backup"`).
- `-c, --remove-comments`: Remove C++ style comments (`//` and `/* ... */`) from the output code. Applicable to code files.
- `-l, --remove-empty-lines`: Remove empty lines from the output.
- `-f, --filename-only`: Show only the filename in file headers. By default, the relative path from the input directory is shown.
- `-u, --unordered`: Output files as they are processed. This can be faster, especially for large directories, but the order of files in the output is not guaranteed (except for files specified with `-z`). This is the default behavior if `-z` or `-Z` are not used.
- `-z, --last <item>`: Process specified file or directory last. The order of multiple `-z` options is strictly preserved. When a directory is specified, all files within it (and its subdirectories if recursion is enabled) will be processed after other files, maintaining the relative order specified by multiple `-z` arguments. You can specify a directory path, an exact filename (relative path), or just the filename itself. Paths are relative to the input directory.
- `-Z, --only-last`: **Exclusive mode:** Only process the files and directories specified by the `-z` option. All other files in the input directory will be ignored. Implies `-u` (unordered output for normal files, but maintains order for `--last` files). Requires at least one `-z` argument to be effective. If no `-z` arguments are provided with `-Z`, it will result in an error.
- `-w, --no-markdownlint-fixes`: Disable fixes for Markdown linting. By default, the output is formatted to be compatible with Markdown linters, which adds a top-level heading and slightly different file headers.
- `-t, --no-gitignore`: Disable `.gitignore` rule processing. By default, DirCat respects `.gitignore` rules in the input directory and its parent directories.
- `-g, --gitignore <path>`: Use `.gitignore` rules from a specific path instead of the default directory or its parents. If the specified path does not exist or is not a valid `.gitignore` file, a warning will be shown, and gitignore processing might not work as expected.
- `-o, --output <file>`: Output to the specified file instead of standard output (stdout).

### Examples

Display all files in the current directory:

```bash
./dircat .
```

Process only C++ files (`.cpp`, `.h`, `.hpp`) up to 1MB in size, recursively, and remove C++ comments:

```bash
./dircat . -m 1048576 -e cpp h hpp -c
```

Process files in the current directory without recursion, excluding `.txt` files:

```bash
./dircat . --no-recursive -x txt
```

Ignore `build` folder and `temp.log` file, and folders named `cache` anywhere in the path:

```bash
./dircat . --ignore build temp.log cache
```

Include folders starting with a dot (like `.git` or `.vscode`):

```bash
./dircat . --dot-folders
```

Exclude all files ending with `.tmp` or `.log` using regex:

```bash
./dircat . -r "\.tmp$" "\.log$"
```

Exclude files containing the word "backup" in their filename using regex:

```bash
./dircat . -r backup
```

Process only files with extensions `cpp`, `c`, and `h`:

```bash
./dircat . -e cpp c h
```

Exclude `.min.js` files, but include other `.js` files:

```bash
./dircat . -e js -x min.js
```

Remove C++ comments and empty lines from the output:

```bash
./dircat . -c -l
```

Show only filenames in the output (no paths):

```bash
./dircat . -f
```

Output files in an unordered fashion (may be faster for large directories):

```bash
./dircat . -u
```

Process `important_config.h` and the `docs` directory last, in that order:

```bash
./dircat . -z important_config.h docs
```

Process all files normally, but ensure `README.md` is always at the very end of the output:

```bash
./dircat . -z README.md
```

*Only* process `main.cpp`, `utils.h`, and all files within the `src/` directory, excluding everything else:

```bash
./dircat . -Z -z main.cpp utils.h src/
```

Disable Markdown linting fixes for plain output:

```bash
./dircat . -w
```

Disable gitignore processing entirely:

```bash
./dircat . -t
```

Use gitignore rules from a specific file located at `/path/to/custom.gitignore`:

```bash
./dircat . -g /path/to/custom.gitignore
```

Output the result to a file named `output.md` instead of printing to the console:

```bash
./dircat . -o output.md
```

Example of usage in large C# projects, recursively processing `.cs` files, removing empty lines and comments, and processing specific dependency files last:

```bash
./dircat ./working-folder -e cs -r "\.Designer\.cs$" -l -c -z working-file-deps.cs working-file.cs
```

### Output Format

Files are output in the following format when Markdown linting fixes are enabled (default):

````md
#

## File: relative/path/to/filename.ext

```ext
[file contents]
````

If `-f` (filename only) is used, only the filename is shown in the header:

````md
#

## File: filename.ext

```ext
[file contents]
````

If `-w` (no Markdown linting fixes) is used, the output format is slightly different, omitting the top-level heading and using a level 3 heading for files:

````md
### File: relative/path/to/filename.ext

```ext
[file contents]
```
````

In all cases, the file extension (if available) is used to indicate the code block's language for syntax highlighting in Markdown.

## Features

- **High-performance multi-threaded processing:** Significantly reduces processing time by concurrently handling multiple files.
- **Recursive directory traversal:**  Optionally explores subdirectories to process files in nested folders.
- **Flexible file extension filtering:**  Include or exclude files based on specific extensions to target desired file types.
- **Maximum file size limiting:**  Skip processing very large files to improve efficiency and manage output size.
- **Customizable ignore rules:**  Exclude specific folders and files from processing, enhancing control over the output.
- **Regular expression based filename filtering:** Exclude files based on complex filename patterns using regular expressions.
- **Code comment removal:**  Clean code outputs by stripping C++ style comments (`//` and `/* ... */`).
- **Empty line removal:**  Improve readability by removing blank lines from the concatenated output.
- **Filename-only output option:**  Simplify file headers by displaying only filenames instead of full relative paths.
- **Unordered output mode:**  Process and output files as quickly as possible, potentially increasing speed for large directories when order is not critical (default if `--last` options are not used).
- **Prioritized "process last" feature:**  Process specific files and directories at the end, in a defined order, useful for controlling output order (e.g., placing README or important configuration files at the end).
- **Exclusive "only last" processing:**  Focus solely on the files and directories specified with `--last`, ignoring all other directory content for highly selective processing.
- **Markdown linting compatibility:**  Output is formatted with Markdown-friendly headers and code blocks, ensuring seamless integration with Markdown rendering and linting tools.
- **`.gitignore` support:**  Automatically respects `.gitignore` rules, preventing accidental inclusion of unwanted files and directories commonly excluded in Git repositories.
- **Customizable `.gitignore` path:**  Allows specifying a custom `.gitignore` file path for projects with non-standard gitignore configurations.
- **Output redirection to file:**  Save the concatenated output directly to a file instead of displaying it in the console.
- **Graceful interrupt handling:**  Stops processing and exits cleanly upon receiving an interrupt signal (e.g., Ctrl+C).
- **Memory-efficient design:**  Processes files in a streaming manner to handle large files without excessive memory usage.

## Requirements

- C++20 compatible compiler (like g++ >= 10, clang++ >= 10, MSVC >= 2019)
- CMake 3.10 or higher for building
- Standard C++ libraries (should be available on any system with a C++ compiler)

## Building

1. **Clone the repository:**

    ```bash
    git clone https://github.com/romelium/dircat.git
    cd dircat
    ```

2. **Create a build directory:**

    ```bash
    mkdir build
    cd build
    ```

3. **Configure with CMake (Release build recommended for performance):**

    ```bash
    cmake .. -DCMAKE_BUILD_TYPE=Release
    ```

4. **Build the executable:**

    ```bash
    cmake --build . --config Release
    ```

    The `dircat` executable will be created in the `build` directory (or `build/Release` on Windows for Release build). You can then run it from the `build` directory or copy it to a location in your system's PATH.

## Implementation Details

- Implemented using modern C++20 features, including `<filesystem>` for path manipulation, `<thread>` for multi-threading, `<atomic>` for thread-safe operations, and `<regex>` for regular expression matching.
- Limits the maximum number of threads used for file processing to the hardware concurrency or 8, whichever is lower, to balance performance and system load.
- Processes files in chunks and streams file content to efficiently handle very large files without loading them entirely into memory.
- Implements comment removal by parsing the code and tracking comment contexts (single-line and multi-line), string literals, and character literals to avoid accidental removal of code-like comment markers within strings or characters.
- Employs buffered reading for efficient file input and output operations.
- Supports flexible ordering of output, including the ability to process specific files or directories last, maintaining a user-defined order for these "last" items.
- Implements `.gitignore` rule matching logic, supporting wildcards (`*`, `?`), directory patterns (`dir/`), negation (`!rule`), and correctly handles the precedence of rules as defined in the gitignore specification.

## Error Handling

- Gracefully handles file system errors, such as permission denied errors when accessing directories or files, skipping inaccessible items and continuing processing.
- Skips files that exceed the specified maximum file size, preventing excessive memory usage and output.
- Provides thread-safe error logging to `std::cerr`, ensuring that error messages from multiple threads are displayed correctly without interleaving.
- Implements clean interrupt handling using signals, allowing users to stop the program gracefully with Ctrl+C without data corruption or abrupt termination.
- Validates regular expressions provided by the user and reports errors for invalid regex patterns, preventing program crashes due to regex exceptions.
- Uses robust mutex-based thread synchronization to protect shared resources and prevent race conditions, ensuring data integrity and program stability in multi-threaded execution.
- Includes checks for common user errors, such as using `--only-last` without any `--last` arguments, or specifying non-existent files or directories with `--last` or `--ignore`, providing informative error messages and exiting gracefully in such cases.
- Offers clear and informative error messages for invalid command-line arguments and options, guiding users to correct usage.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
