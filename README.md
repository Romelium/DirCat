# DirCat

DirCat is a high-performance C++ utility that concatenates and displays the contents of files in a directory, similar to the Unix `cat` command but for entire directories. It supports multi-threaded processing, recursive directory traversal, file filtering, and various output formatting options.

## Usage

```bash
./dircat <directory_path> [options]
```

### Options

- `-m, --max-size <bytes>`: Maximum file size in bytes (no limit by default).
- `-n, --no-recursive`: Disable recursive directory search.
- `-e, --ext <ext>`: Process only files with the specified extension (can be used multiple times, grouped).
- `-x, --exclude-ext <ext>`: Exclude files with the specified extension (can be used multiple times, grouped).
- `-d, --dot-folders`: Include folders starting with a dot (ignored by default).
- `-i, --ignore <item>`: Ignore specific folders or files (can be used multiple times, grouped).  Paths are relative to the input directory.
- `-r, --regex <pattern>`: Exclude files matching the regex pattern (can be used multiple times, grouped). Applies to filenames only.
- `-c, --remove-comments`: Remove C++ style comments (`//` and `/* ... */`) from the output.
- `-l, --remove-empty-lines`: Remove empty lines from the output.
- `-f, --filename-only`: Show only the filename in file headers (default is relative path).
- `-u, --unordered`: Output files as they are processed (potentially faster, but order is not guaranteed).  This is the default if `-z` is not used.
- `-z, --last <item>`: Process specified file or directory last.  The order of multiple `-z` options is strictly preserved. When a directory is specified, all files within it (and its subdirectories if recursion is enabled) will be processed after other files, maintaining the relative order specified by multiple `-z` arguments. You can specify a directory path, an exact filename (relative path), or just the filename itself.  Paths are relative to the input directory.
- `-Z, --only-last`: Only process the files and directories specified by the `-z` option.  Implies `-u`.  Requires at least one `-z` argument.
- `-w, --no-markdownlint-fixes`: Disable fixes for Markdown linting (enabled by default).
- `-t, --no-gitignore`: Disable gitignore rules.
- `-g, --gitignore <path>`: Use gitignore rules from a specific path (defaults to `<directory_path>/.gitignore`).

### Examples

Display all files in the current directory:

```bash
./dircat .
```

Process only C++ files up to 1024 bytes in size:

```bash
./dircat . --max-size 1024 --ext cpp
```

Process files in the current directory without recursion:

```bash
./dircat . --no-recursive
```

Ignore `build` folder and `temp.txt` file:

```bash
./dircat . --ignore build temp.txt
```

Include folders starting with a dot:

```bash
./dircat . --dot-folders
```

Exclude all files ending with `.tmp` or `.log`:

```bash
./dircat . -r "\.tmp$" "\.log$"
```

Exclude files containing the word "backup":

```bash
./dircat . -r backup
```

Process only files with extensions `cpp`, `h`, and `hpp`:

```bash
./dircat . -e cpp h hpp
```

Exclude `.min.js` files, but include other `.js` files.

```bash
./dircat . -e js -x min.js
```

Remove C++ comments from the output:

```bash
./dircat . -c
```

Remove empty lines from the output:

```bash
./dircat . -l
```

Remove C++ comments and empty lines from the output:

```bash
./dircat . -c -l
```

Show only filenames in the output (no paths):

```bash
./dircat . -f
```

Output files in an unordered fashion (may be faster):

```bash
./dircat . -u
```

Process `main.cpp` and `utils.h` last, in that order:

```bash
./dircat . -z main.cpp utils.h
```

Process all files, output them in the order discovered, and process `notes.txt` last:

```bash
./dircat . -z notes.txt
```

Process the contents of the `src` directory, and finally `main.cpp`:

```bash
./dircat . -z src main.cpp
```

Process `helper.h` last, even if other files match the `src` directory processing:

```bash
./dircat . -z src helper.h
```

Process all `.cpp` files in the `src/utils` directory last, maintaining their order within the directory:

```bash
./dircat . -e cpp -z src/utils
```

*Only* process `main.cpp` and `utils.h` (nothing else):

```bash
./dircat . -Z -z main.cpp utils.h
```

Disable Markdown linting fixes:

```bash
./dircat . -w
```

Disable Markdown linting fixes, remove comments:

```bash
./dircat . -w -c
```

Disable gitignore processing:

```bash
./dircat . -t
```

Use a specific gitignore file:

```bash
./dircat . -g /path/to/.gitignore
```

Example of usage in large C# projects (removing empty lines and comments, and processing specific files last):

```bash
./dircat ./working-folder -e cs -l -c -z working-file-deps.cs working-file.cs
```

## Output Format

Files are output in the following format (with Markdown linting fixes, which are enabled by default):

````md
#

## File: relative/path/to/filename.ext

```ext
[file contents]
````

If `-f` (filename only) is used:

````md
#

## File: filename.ext

```ext
[file contents]
````

If `-w` (no Markdown linting fixes) is used, the output format is:

````md
### File: relative/path/to/filename.ext

```ext
[file contents]
```
````

## Features

- **Multi-threaded file processing:** Improves performance by processing multiple files concurrently.
- **Recursive directory traversal:**  Optionally processes files in subdirectories.
- **File extension filtering:**  Includes or excludes files based on their extensions.
- **Maximum file size limiting:**  Skips files larger than a specified size.
- **Ignore specific folders and files:**  Excludes specified paths from processing.
- **Regular expression filtering:** Excludes files based on filename patterns.
- **Comment removal:**  Removes C++ style comments (`//` and `/* ... */`).
- **Empty line removal:**  Removes blank lines from the output.
- **Filename-only output:**  Displays only filenames instead of relative paths.
- **Unordered output:**  Outputs files as they are processed, potentially improving speed.
- **Process last:**  Processes specific files and directories last, in the order specified.
- **Only last:** Process only the files/directories specified with `--last`.
- **Markdown linting fixes:**  Formats the output to be compatible with Markdown linters.
- **Gitignore support:**  Respects `.gitignore` rules for ignoring files and directories.
- **Formatted output:**  Includes file headers and syntax highlighting markers.
- **Graceful interrupt handling:**  Handles Ctrl+C gracefully.
- **Memory-efficient:**  Streams large files to avoid loading them entirely into memory.

## Requirements

- C++20 compatible compiler
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

3. **Configure with CMake:**

    ```bash
    cmake .. -DCMAKE_BUILD_TYPE=Release
    ```

4. **Build:**

    ```bash
    cmake --build . --config Release
    ```

    This creates the `dircat` executable in the `build` directory.

## Implementation Details

- Uses modern C++ features, including `<filesystem>`, `<thread>`, `<atomic>`, and `<regex>`.
- Limits the maximum number of threads to 8 or the hardware concurrency, whichever is lower.
- Processes files in chunks for memory efficiency.
- When ordered output is required, sorts results after multi-threaded processing.
- Handles large files efficiently through buffered reading.
- Removes comments by tracking whether the current position is within a string, character literal, single-line comment, or multi-line comment.
- Removes empty lines by checking if a line contains only whitespace.
- Supports processing specific files/directories last, in the specified order.
- Implements gitignore rule matching with support for wildcards, negation, and directory-specific rules.

## Error Handling

- Handles permission denied errors gracefully.
- Skips files exceeding the size limit.
- Uses thread-safe error logging.
- Handles interrupts (Ctrl+C) cleanly.
- Reports invalid regular expressions.
- Uses robust thread synchronization to prevent race conditions.
- Reports errors if `--only-last` is used without any `--last` arguments, or if specified files/directories don't exist.
- Provides clear error messages for invalid arguments and options.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
