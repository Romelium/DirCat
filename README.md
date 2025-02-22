# DirCat

DirCat is a high-performance C++ utility that concatenates and displays the contents of files within a directory, functioning as a directory-aware version of the Unix `cat` command. Engineered for efficiency and flexibility, DirCat leverages multi-threading, recursive directory traversal, and comprehensive file filtering options to streamline content aggregation from directories. It's ideal for developers, writers, and anyone needing to combine multiple text-based files into a single output.

## Usage

```bash
./dircat <directory_path> [options]
```

### Options

- `-m, --max-size <bytes>`: Sets the maximum file size in bytes to process. Files exceeding this limit are ignored. Default: no limit.
- `-n, --no-recursive`: Disables recursive directory searching. Only files in the specified top-level directory will be processed.
- `-e, --ext <ext>`: Specifies file extensions to include in processing. Can be used multiple times to include several extensions (e.g., `-e cpp h`).
- `-x, --exclude-ext <ext>`: Specifies file extensions to exclude from processing.  Use multiple times to exclude several extensions (e.g., `-x tmp log`).
- `-d, --dot-folders`: Includes folders starting with a dot (e.g., `.git`, `.vscode`), which are ignored by default.
- `-i, --ignore <item>`: Ignores specific folders or files. Paths are relative to the input directory. Can be used multiple times to ignore multiple items (e.g., `-i build temp.txt`). For directories, all content within is skipped. Specify folder names (like `build`), or relative paths (like `subdir/temp.txt`).
- `-r, --regex <pattern>`: Excludes files where the filename matches the given regex pattern. Regex is applied to the filename only. Use multiple times for several patterns (e.g., `-r "\.tmp$" "backup"`).
- `-c, --remove-comments`: Removes C++ style comments (`//` and `/* ... */`) from code files in the output.
- `-l, --remove-empty-lines`: Removes empty lines from the output, enhancing readability.
- `-f, --filename-only`: Displays only the filename in file headers, omitting the relative path from the input directory.
- `-u, --unordered`: Outputs files as they are processed, potentially faster for large directories, but output order is not guaranteed (except for `-z` files). Default behavior if `-z` or `-Z` are not used.
- `-z, --last <item>`: Processes specified files or directories last, in the order given. Useful for controlling output order. Specify directory paths, exact filenames (relative paths), or just filenames. Paths are relative to the input directory.
- `-Z, --only-last`: **Exclusive mode**: Only processes files and directories specified with `-z`, ignoring all other directory content. Implies `-u` and requires at least one `-z` argument.
- `-w, --no-markdownlint-fixes`: Disables Markdown linting compatibility fixes, providing a plain output format.
- `-t, --no-gitignore`: Disables `.gitignore` rule processing. By default, DirCat respects `.gitignore` rules.
- `-g, --gitignore <path>`: Specifies a custom `.gitignore` file path. If the path is invalid, a warning is shown, and gitignore processing might be affected.
- `-o, --output <file>`: Redirects output to the specified file instead of stdout.
- `-L, --line-numbers`: Prepends line numbers to each line in the output.

### Examples

Display all files in the current directory:

```bash
./dircat .
```

Process C++ source files recursively, up to 1MB each, removing comments:

```bash
./dircat . -m 1048576 -e cpp h hpp -c
```

Process non-recursively, excluding `.txt` files:

```bash
./dircat . --no-recursive -x txt
```

Ignore `build`, `temp.log`, and any `cache` folders:

```bash
./dircat . --ignore build temp.log cache
```

Include dot folders:

```bash
./dircat . --dot-folders
```

Exclude `.tmp` and `.log` files using regex:

```bash
./dircat . -r "\.tmp$" "\.log$"
```

Exclude files with "backup" in the filename:

```bash
./dircat . -r backup
```

Process `.cpp`, `.c`, and `.h` files only:

```bash
./dircat . -e cpp c h
```

Process `.js` files, excluding `.min.js`:

```bash
./dircat . -e js -x min.js
```

Remove comments and empty lines:

```bash
./dircat . -c -l
```

Show filenames only in headers:

```bash
./dircat . -f
```

Unordered output for speed:

```bash
./dircat . -u
```

Process `important_config.h` and `docs` directory last:

```bash
./dircat . -z important_config.h docs
```

Ensure `README.md` is always at the end:

```bash
./dircat . -z README.md
```

*Only* process `main.cpp`, `utils.h`, and `src/` directory:

```bash
./dircat . -Z -z main.cpp utils.h src/
```

Disable Markdown linting fixes for plain text output:

```bash
./dircat . -w
```

Disable gitignore processing:

```bash
./dircat . -t
```

Use custom gitignore from `/path/to/custom.gitignore`:

```bash
./dircat . -g /path/to/custom.gitignore
```

Output to `output.md` file:

```bash
./dircat . -o output.md
```

Process `.js` and `.html`, remove empty lines, output to `web_files.txt`:

```bash
./dircat ./web-project -e js html -l -o web_files.txt
```

Process all except `.log` and `.tmp` in `logs`, ignore gitignore:

```bash
./dircat . -x log tmp -i logs -t
```

Process `.md` files, filename headers, output to `docs_combined.md`:

```bash
./dircat ./documentation -e md -f -o docs_combined.md
```

Process Python and Javascript, remove comments/empty lines, output to `code_bundle.txt`:

```bash
./dircat ./project -e py js -c -l -o code_bundle.txt
```

Ignore files > 50KB, and `node_modules` directory:

```bash
./dircat . -m 51200 -i node_modules
```

Combine `.css` and `.scss`, process `style.css` last, output to `styles.md`:

```bash
./dircat ./styles -e css scss -z style.css -o styles.md
```

Example for C# projects, process `.cs`, exclude Designer files, remove empty lines/comments, process deps last:

```bash
./dircat ./working-folder -e cs -r "\.Designer\.cs$" -l -c -z working-file-deps.cs working-file.cs
```

Create Markdown from documentation, include dot folders, show line numbers, output to `documentation.md`:

```bash
./dircat ./docs -e md --dot-folders -L -o documentation.md
```

Use a custom gitignore from parent directory:

```bash
./dircat ./src -g ../.custom_gitignore
```

### Output Format

Default Markdown-friendly output:

````md
#

## File: relative/path/to/filename.ext

```ext
[file contents]
````

Filename-only headers (`-f`):

````md
#

## File: filename.ext

```ext
[file contents]
````

Plain output (`-w`):

````md
### File: relative/path/to/filename.ext

```ext
[file contents]
```
````

With line numbers (`-L`):

````md
#

## File: relative/path/to/filename.ext

```ext
1 | [first line of file contents]
2 | [second line of file contents]
...
````

Multiple files are concatenated sequentially. Unordered output (`-u` or `-Z`) may alter the order, except for `-z` files which are always last and in order.

## Features

- **High-Performance Multi-threading:** Utilizes CPU cores for parallel file processing, ensuring speed even in large directories.
- **Recursive Directory Traversal:** Explores subdirectories to process entire project structures or documentation trees.
- **Flexible File Extension Filtering:** Includes or excludes files based on extensions, targeting specific file types.
- **Maximum File Size Limiting:** Skips large files to enhance efficiency and control output size.
- **Customizable Ignore Rules:** Excludes specified folders and files, tailoring the output content.
- **Regex-Based Filename Filtering:** Advanced filtering using regular expressions for complex filename patterns.
- **Code Comment Removal:** Cleans code output by stripping C++ style comments.
- **Empty Line Removal:** Improves readability by removing blank lines from the output.
- **Filename-Only Output Option:** Simplifies headers by displaying only filenames.
- **Unordered Output Mode:** Processes and outputs files quickly, ideal for large directories when order is not critical.
- **Prioritized "Process Last" Feature:** Processes specific files/directories last, controlling output order for key files like READMEs.
- **Exclusive "Only Last" Processing:** Focuses solely on `--last` specified files/directories, ignoring other content.
- **Markdown Linting Compatibility:** Formats output with Markdown-friendly headers and code blocks for easy integration with Markdown tools.
- **`.gitignore` Support:** Respects `.gitignore` rules, recursively checking parent directories to prevent inclusion of unwanted files.
- **Customizable `.gitignore` Path:** Allows specifying a custom `.gitignore` file for non-standard project setups.
- **Line Numbers in Output:** Option to prepend line numbers for easier referencing of content.
- **Output Redirection to File:** Saves output directly to a file instead of console display.
- **Graceful Interrupt Handling:** Stops processing cleanly upon interrupt signals (Ctrl+C).
- **Memory-Efficient Design:** Streams file content to handle large files without excessive memory usage.

## Requirements

- C++20 compatible compiler (g++ >= 10, clang++ >= 10, MSVC >= 2019)
- CMake 3.10 or higher
- Standard C++ libraries

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

3. **Configure CMake (Release build recommended):**

    ```bash
    cmake .. -DCMAKE_BUILD_TYPE=Release
    ```

4. **Build the executable:**

    ```bash
    cmake --build . --config Release
    ```

    The `dircat` executable is created in the `build` directory (or `build/Release` on Windows). Run it from `build` or copy it to your system's PATH.

## Implementation Details

- C++20 features: `<filesystem>`, `<thread>`, `<atomic>`, `<regex>`.
- Thread limit: Hardware concurrency or 8 threads, whichever is lower.
- Streaming file processing for memory efficiency.
- Robust C++ comment removal logic.
- Buffered I/O for performance.
- Flexible output ordering, including "process last" prioritization.
- Comprehensive `.gitignore` rule matching, with recursive parent directory checks.

## Error Handling

- Graceful handling of file system errors (permission denied, etc.).
- Skips files exceeding max size.
- Thread-safe error logging to `std::cerr`.
- Clean interrupt handling with signals.
- Regex validation to prevent crashes.
- Mutex-based thread synchronization for data integrity.
- User error checks with informative messages (e.g., `--only-last` without `--last`).
- Clear command-line argument error messages.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
