> **⚠️ Project Archived & Deprecated**
>
> This C++ version of DirCat is now **archived and deprecated**. It has been replaced by a more modern, feature-rich, and actively maintained version written in Rust.
>
> Please use the new version: **[dircat-rust](https://github.com/romelium/dircat-rust)**

---

# DirCat

DirCat is a high-performance C++ utility that concatenates and displays the contents of files within a directory, functioning as a directory-aware version of the Unix `cat` command but for directories. Engineered for efficiency and flexibility, DirCat leverages multi-threading, recursive directory traversal, and comprehensive file filtering options to streamline content aggregation from directories. It's ideal for developers, writers, and anyone needing to combine multiple text-based files into a single output for documentation, analysis, or code review.

## Usage

```bash
./dircat <directory_path | file_path> [options]
```

### Options

- `-m, --max-size <bytes>`: Sets the maximum file size in bytes to process. Files exceeding this limit are ignored. Useful for very large directories or when you want to skip processing extremely large files. Default: no limit.
- `-n, --no-recursive`: Disables recursive directory searching. Only files in the specified top-level directory will be processed, and subdirectories will be skipped.
- `-e, --ext <ext...>`: Specifies file extensions to **include** in processing. Only files with these extensions will be processed. Can be used multiple times to include several extensions (e.g., `-e cpp h` to process `.cpp` and `.h` files).
- `-d, --filename-regex <pattern...>`: Include only files where the **filename** matches the given regex pattern. The regex is applied to the filename only (not the full path). Use multiple times for several patterns to include files matching any of the provided patterns (e.g., `-d "file_a.*\.cpp" "file_d.*"` to include files starting with "file_a" or "file_d"). Regular expressions use the ECMAScript syntax.
- `-x, --exclude-ext <ext...>`: Specifies file extensions to **exclude** from processing. Files with these extensions will be skipped. Use multiple times to exclude several extensions (e.g., `-x tmp log` to exclude `.tmp` and `.log` files).
- `-i, --ignore <item...>`: Ignores specific folders or files. Paths are relative to the input directory provided as the first argument. Can be used multiple times to ignore multiple items (e.g., `-i build temp.txt`). For directories, all content within is skipped. Specify folder names (like `build`), relative paths to folders (like `subdir/data`), relative paths to files (like `temp.txt`), or folder/file names.
- `-r, --regex <pattern...>`: Excludes files where the **filename** matches the given regex pattern. The regex is applied to the filename only (not the full path). Use multiple times for several patterns (e.g., `-r "\.tmp$" "backup"` to exclude files ending with `.tmp` or containing "backup" in their name). Regular expressions use the ECMAScript syntax.
- `-c, --remove-comments`: Removes C++ style comments (`//` for single-line and `/* ... */` for multi-line comments) from code files in the output. This is useful for cleaner output when concatenating code files.
- `-l, --remove-empty-lines`: Removes empty lines from the output, enhancing readability by reducing vertical space.
- `-f, --filename-only`: Displays only the filename in file headers, omitting the relative path from the input directory. This simplifies headers and is useful when the directory structure is not important in the output.
- `-z, --last <item...>`: Processes specified files or directories **last**, in the order given on the command line. This is useful for controlling the output order, ensuring certain files (like `README.md` or configuration files) appear at the end of the concatenated output. Specify directory paths, exact filenames (relative paths), or just filenames. Paths are relative to the input directory. Multiple `-z` options are processed in the order they appear.
- `-Z, --only-last`: **Exclusive mode**: Only processes files and directories specified with `-z`, ignoring all other directory content. This mode focuses solely on the items provided with `--last` options and is useful when you only want to combine a specific set of files. Requires at least one `-z` argument to be effective.
- `-t, --no-gitignore`: Disables `.gitignore` rule processing. By default, DirCat respects `.gitignore` rules found in the directory and its parent directories, preventing the inclusion of files and folders listed in `.gitignore`. This option forces DirCat to ignore these rules and process all files that match other criteria.
- `-o, --output <file>`: Redirects the combined output to the specified file instead of the standard output (stdout). This is essential for saving the concatenated content to a file for further use.
- `-L, --line-numbers`: Prepends line numbers to each line in the output. This is helpful for referencing specific lines in the concatenated files, especially in code or documentation.
- `-D, --dry-run`: Dry-run mode: lists the files that would be processed based on the given arguments, without actually concatenating or outputting their content. This is useful for previewing which files will be included before running the full command. Paths are listed relative to the input directory.
- `-b, --backticks`: Encloses file paths in backticks (\`\`) in headers (## File: \`path/to/file.ext\`), dry-run output, and the summary list (if enabled with `-s`).
- `-s, --summary`: Appends a summary list of all processed relative file paths at the end of the output (only in normal run, not dry-run).

### Examples

Display all files in the current directory:

```bash
./dircat .
```

Process C++ source and header files recursively, up to 1MB each, removing comments:

```bash
./dircat . -m 1048576 -e cpp h hpp -c
```

Process files in the current directory non-recursively, excluding `.txt` and `.log` files:

```bash
./dircat . --no-recursive -x txt log
```

Ignore `build` and `temp` folders, and the file `notes.txt` within the input directory:

```bash
./dircat . --ignore build temp notes.txt
```

Exclude `.tmp` and `.log` files using regex patterns:

```bash
./dircat . -r "\.tmp$" "\.log$"
```

Exclude files with "backup" or "temp" in their filename:

```bash
./dircat . -r backup temp
```

Exclude files with *either* `*_test.cpp` suffix *or* `test_*.cpp` prefix::

```bash
./dircat . -r "^test_.*\\.cpp$|.*_test\\.cpp$"
```

Process only `.cpp`, `.c`, and `.h` files:

```bash
./dircat . -e cpp c h
```

Process `.js` files, excluding `.min.js` files:

```bash
./dircat . -e js -x min.js
```

Only include files with *either* `*_test.cpp` suffix *or* `test_*.cpp` prefix::

```bash
./dircat . -d "^test_.*\\.cpp$|.*_test\\.cpp$"
```

Remove comments and empty lines from the output:

```bash
./dircat . -c -l
```

Show only filenames in headers, omitting relative paths:

```bash
./dircat . -f
```

Process `important_config.h` and the `docs` directory last, ensuring they appear at the end of the output:

```bash
./dircat . -z important_config.h docs
```

Ensure `README.md` is always the very last file in the output:

```bash
./dircat . -z README.md
```

*Only* process `main.cpp`, `utils.h`, and all files within the `src/` directory, ignoring all other files in the input directory:

```bash
./dircat . -Z -z main.cpp utils.h src/
```

Disable gitignore rule processing, processing all files regardless of `.gitignore` rules:

```bash
./dircat . -t
```

Output the concatenated content to `output.md` file instead of printing to the console:

```bash
./dircat . -o output.md
```

Process `.js` and `.html` files, remove empty lines, and output the result to `web_files.txt`:

```bash
./dircat ./web-project -e js html -l -o web_files.txt
```

Process all files except those with `.log` and `.tmp` extensions within the `logs` directory, and ignore gitignore rules:

```bash
./dircat . -x log tmp -i logs -t
```

Process `.md` files, use filename-only headers, and output to `docs_combined.md`:

```bash
./dircat ./documentation -e md -f -o docs_combined.md
```

Process Python and Javascript files, remove comments and empty lines, and output to `code_bundle.txt`:

```bash
./dircat ./project -e py js -c -l -o code_bundle.txt
```

Ignore files larger than 50KB and the entire `node_modules` directory:

```bash
./dircat . -m 51200 -i node_modules
```

Combine `.css` and `.scss` files, process `style.css` last, and output to `styles.md`:

```bash
./dircat ./styles -e css scss -z style.css -o styles.md
```

Example for C# projects: process `.cs` files, exclude Designer files using regex, remove empty lines and comments, and process `working-file-deps.cs` and `working-file.cs` last in that specific order:

```bash
./dircat ./working-folder -e cs -r "\.Designer\.cs$" -l -c -z working-file-deps.cs working-file.cs
```

Create Markdown output from documentation files, include dot folders, show line numbers, and output to `documentation.md`:

```bash
./dircat ./docs -e md -L -o documentation.md
```

Dry run, listing files with paths enclosed in backticks:

```bash
./dircat . -D -b
```

Process files and append a summary list of processed files at the end:

```bash
./dircat . -e cpp h -s -o output.txt
```

Process files, use backticks for headers, and append a summary list with backticked paths:

```bash
./dircat . -e cpp h -b -s -o output.txt
```

### Output Format

DirCat produces a Markdown-friendly output by default, suitable for documentation and combining code snippets.

**Default Markdown Output:**

````md
# File generated by DirCat

## File: relative/path/to/filename.ext

```ext
[file contents]
```

````

- Each file's content is enclosed within a Markdown code block, identified by the file extension for syntax highlighting (if supported by Markdown renderers).
- A level 2 heading (`## File: ...`) precedes each file's content, indicating the file path relative to the input directory.
- A top-level heading `# File generated by DirCat` is added at the very beginning of the output.

**Filename-only headers (`-f` option):**

````md
# File generated by DirCat

## File: filename.ext

```ext
[file contents]
```

````

- When using `-f`, the headers are simplified to only include the filename, omitting the relative path.

**With line numbers (`-L` option):**

````md
# File generated by DirCat

## File: relative/path/to/filename.ext

```ext
1 | [first line of file contents]
2 | [second line of file contents]
...
```

````

- If `-L` is used, each line within the code block is prepended with its line number, followed by `|`.

**With backticks (`-b` option):**

````md
# File generated by DirCat

## File: `relative/path/to/filename.ext`

```ext
[file contents]
```

````

- If `-b` is used, the file path in the header is enclosed in backticks.

**With summary (`-s` option, normal run only):**

````md
# File generated by DirCat

## File: relative/path/to/file1.ext

```ext
[file1 contents]
```

## File: relative/path/to/file2.ext

```ext
[file2 contents]
```

---
Processed Files (2):
relative/path/to/file1.ext
relative/path/to/file2.ext
````

- If `-s` is used (and not `-D`), a summary section is appended after all file content. It lists the relative paths of all processed files in the order they appeared in the output.

**With summary and backticks (`-s -b` options, normal run only):**

````md
# File generated by DirCat

## File: `relative/path/to/file1.ext`

```ext
[file1 contents]
```

## File: `relative/path/to/file2.ext`

```ext
[file2 contents]
```

---
Processed Files (2):
`relative/path/to/file1.ext`
`relative/path/to/file2.ext`
````

- If both `-s` and `-b` are used, the summary list also uses backticks for the paths.

Multiple files are concatenated sequentially in the output. The order is determined by directory traversal (alphabetical by default unless `--last` or `--only-last` options are used to modify processing order). Files specified with `-z` options are always processed and appended to the output last, in the order they were specified.

## Features

- **High-Performance Multi-threading:** Utilizes multiple CPU cores for parallel file processing, significantly speeding up content aggregation, especially in large directories. The number of threads is automatically adjusted based on hardware concurrency, up to a maximum of 16 threads.
- **Recursive Directory Traversal:** Explores directories recursively to process files in subdirectories, enabling comprehensive content gathering from entire project structures or documentation trees. Can be disabled with the `-n` option for processing only the top-level directory.
- **Flexible File Extension Filtering:** Offers include (`-e`) and exclude (`-x`) options to precisely target specific file types based on their extensions, allowing you to process only relevant files (e.g., source code files, documentation files).
- **Maximum File Size Limiting:** The `-m` option allows setting a maximum file size limit, skipping files that exceed this size. This is useful for ignoring very large files that are not relevant or could slow down processing.
- **Customizable Ignore Rules:** The `-i` option enables ignoring specific folders and files by name or relative path, tailoring the output content by excluding unnecessary or irrelevant items.
- **Regex-Based Filename Filtering:** Provides advanced filtering capabilities using regular expressions (`-r` to exclude, `-d` to include) to filter files based on complex filename patterns. Regular expressions are applied to filenames, allowing for sophisticated inclusion/exclusion rules.
- **Code Comment Removal:** Cleans up code output by automatically stripping C++ style comments (`-c` option), resulting in a more concise and focused output when dealing with code files.
- **Empty Line Removal:** Improves the readability of the combined output by removing blank lines (`-l` option), making the content denser and easier to follow.
- **Filename-Only Output Option:** Simplifies file headers by displaying just the filename (`-f` option), which is useful when the directory structure is not important in the final output.
- **Prioritized "Process Last" Feature:** The `-z` option allows specifying files and directories to be processed last, in a defined order. This feature is crucial for controlling the final output order, ensuring important files like READMEs or configuration files are placed at the end of the concatenated output.
- **Exclusive "Only Last" Processing:** The `-Z` option provides an exclusive mode where only the files and directories specified with `-z` are processed, ignoring all other content in the input directory. This is ideal for focusing on a specific subset of files.
- **`.gitignore` Support:** Respects `.gitignore` rules by default, recursively checking for `.gitignore` files in the input directory and its parent directories. This prevents the tool from including files and folders that are intentionally excluded in version control, ensuring cleaner and more relevant output. Can be disabled with `-t`.
- **Line Numbers in Output:** Option to prepend line numbers to each line in the output (`-L`), making it easier to reference and discuss specific parts of the concatenated files.
- **Output Redirection to File:** Saves the concatenated output directly to a specified file using the `-o` option, enabling easy saving and sharing of the combined content.
- **Graceful Interrupt Handling:** Stops processing cleanly when an interrupt signal is received (e.g., Ctrl+C), preventing abrupt termination and data corruption.
- **Memory-Efficient Design:** Processes files using streaming techniques to handle large files efficiently without loading entire file contents into memory, reducing memory footprint and improving performance.
- **Dry-Run Capability:** The `-D` option allows for a dry run, listing the files that would be processed without performing the actual concatenation. This is useful for verifying the command and options before full execution.
- **Backtick Path Formatting:** The `-b` option encloses file paths in backticks in headers, dry-run output, and the summary list, useful for Markdown compatibility or easy copying.
- **Summary List:** The `-s` option appends a list of all processed relative file paths at the end of the normal output, providing a quick overview of included files.

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

3. **Configure CMake (Release build recommended for optimized performance):**

    ```bash
    cmake .. -DCMAKE_BUILD_TYPE=Release
    ```

4. **Build the executable:**

    ```bash
    cmake --build . --config Release
    ```

    The `dircat` executable will be created in the `build` directory (or `build/Release` on Windows). You can then run it directly from the `build` directory or copy it to a location in your system's PATH for easier access from anywhere in the command line.

## Implementation Details

- Built using C++20 features, leveraging `<filesystem>` for efficient file system operations, `<thread>` for multi-threading, `<atomic>` for thread-safe operations, and `<regex>` for regular expression matching.
- Multi-threading is implemented to process files in parallel, up to the hardware concurrency limit or a maximum of 16 threads, whichever is lower. This optimizes processing time for directories with many files.
- Employs streaming file processing to handle files of any size without excessive memory usage. Files are read and processed chunk by chunk, making it memory-efficient even for very large files.
- Includes robust logic for C++ comment removal, accurately identifying and removing both single-line (`//`) and multi-line (`/* ... */`) comments from code files.
- Utilizes buffered I/O operations for optimized read and write performance, reducing system call overhead and improving overall speed.
- Offers flexible output ordering options, including the "process last" feature, to allow users to control the sequence of files in the concatenated output, especially for important files that should appear at the end.
- Implements comprehensive `.gitignore` rule matching, recursively checking parent directories for `.gitignore` files and applying the rules to prevent inclusion of ignored files and directories.

## Error Handling

- Implements graceful error handling for common file system operations, such as permission denied errors, file not found errors, and directory access issues. Errors are reported to `std::cerr`, and processing continues with other files if possible.
- Skips files that exceed the specified maximum file size (`-m` option) and reports a warning to `std::cerr`.
- Includes thread-safe error logging to ensure that error messages from multiple threads do not interfere with each other and are reported correctly.
- Provides clean interrupt handling using signals (e.g., SIGINT for Ctrl+C), allowing users to stop the process at any time without data corruption or program crashes.
- Performs regular expression validation to catch invalid regex patterns provided with the `-r` or `-d` options, preventing crashes due to regex errors and reporting informative error messages to `std::cerr`.
- Uses mutex-based thread synchronization to protect shared data structures and ensure data integrity in multi-threaded processing, preventing race conditions and other concurrency issues.
- Includes user error checks to detect common mistakes in command-line arguments, such as using `--only-last` without any `--last` options, and provides informative error messages to guide the user.
- Provides clear and helpful command-line argument error messages to assist users in understanding and correcting issues with their command-line input.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
