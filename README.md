# DirCat

DirCat is a high-performance C++ utility that concatenates and displays the contents of files in a directory, similar to the Unix `cat` command but for entire directories. It supports multi-threaded processing, recursive directory traversal, and filtering options.

## Usage

```bash
./dircat <directory_path> [options]
```

### Options

- `-m, --max-size <bytes>`: Maximum file size in bytes (no limit by default)
- `-n, --no-recursive`: Disable recursive directory search
- `-e, --ext <ext>`: Process only files with the specified extension (can be used multiple times and grouped)
- `-d, --dot-folders`: Include folders starting with a dot (ignored by default)
- `-i, --ignore <item>`: Ignore specific folder or file (can be used multiple times and grouped)
- `-r, --regex <pattern>`: Exclude files matching the regex pattern (can be used multiple times and grouped)
- `-c, --remove-comments`: Remove C++ style comments (`//` and `/* ... */`) from the output.
- `-l, --remove-empty-lines`: Remove empty lines from the output
- `-p, --relative-path`: Show relative path in file headers instead of filename
- `-o, --ordered`: Output files in the order they were found (by default, multi-threading may cause files to be output in a different order)
- `-z, --last <item>`:  Process specified file or directory last. The order of multiple `-z` options is strictly preserved. When a directory is specified, all files within it (and its subdirectories if recursion is enabled) will be processed after other files, maintaining the relative order specified by multiple `-z` arguments. You can specify a directory path, an exact filename (relative path), or just the filename itself.
- `-w, --markdownlint-fixes`: Apply basic Markdown linting fixes to the output (compatible with markdownlint format)

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

Show relative paths in the output:

```bash
./dircat . -p
```

Output files in the order they are found:

```bash
./dircat . -o
```

Process `main.cpp` and `utils.h` last, in that order:

```bash
./dircat . -z main.cpp utils.h
```

Process all files, output them in order, and process `notes.txt` last:

```bash
./dircat . -o -z notes.txt
```

**NEW:** Process all files, then the contents of the `src` directory, and finally `main.cpp`:

```bash
./dircat . -z src main.cpp
```

**NEW:** Process `helper.h` last, even if other files match the `src` directory processing:

```bash
./dircat . -z src helper.h
```

**NEW:** Process all `.cpp` files in the `src/utils` directory last, maintaining their order within the directory:

```bash
./dircat . -e cpp -z src/utils
```

Apply Markdown linting fixes:

```bash
./dircat . -w
```

Apply Markdown linting fixes, remove comments, and order output:

```bash
./dircat . -w -c -o
```

Example of what I'm currently using in large C# projects:

```bash
./dircat ./working-folder -e cs -l -p -o -w -z working-file-deps.cs working-file.cs 
```

## Output Format

Files are output in the following format:

````md
### File: filename.ext
```ext
[file contents]
````

Or, if `-p` is used:

````md
### File: relative/path/to/filename.ext
```ext
[file contents]
````

If `-w` (markdownlint fixes) is used, the output format changes slightly:

- The `###` is reduced to `##`
- There will be empty line after header
- There will be top-level `#` at the beginning

````md
#

## File: filename.ext

```ext
[file contents]
````

## Features

- Multi-threaded file processing for improved performance
- Recursive directory traversal (optional)
- File extension filtering
- Maximum file size limiting
- Ignore specific folders
- Ignore specific files
- Regular expression filtering for excluding files
- Option to remove C++ style comments
- Option to remove empty lines
- Option to show relative paths
- Option to enforce output order
- option to process specific files or directories last, in specified order
- Option to apply basic Markdown linting fixes
- Formatted output with file names and syntax highlighting markers
- Graceful interrupt handling
- Memory-efficient streaming of large files

## Requirements

- C++20 compatible compiler
- CMake 3.10 or higher
- Standard C++ libraries

## Building

To build DirCat, you will need to have CMake and a C++20 compatible compiler installed on your system. Follow these steps to build the project:

1. Clone the repository:

   ```bash
   git clone https://github.com/romelium/dircat.git
   cd dircat
   ```

2. Create a build directory and navigate into it:

   ```bash
   mkdir build
   cd build
   ```

3. Run CMake to configure the project:

   ```bash
   cmake .. -DCMAKE_BUILD_TYPE=Release
   ```

4. Build the project:

   ```bash
   cmake --build . --config Release
   ```

   This will create the `dircat` executable in the `build` directory.

## Implementation Details

- Uses modern C++ features including filesystem, threading, atomic operations, and regular expressions.
- Limits maximum thread count to 8 or hardware concurrency, whichever is lower.
- Processes files in chunks for memory efficiency.
- Optionally maintains consistent output ordering using sorting after multi-threaded processing.
- Handles large files efficiently through buffered reading.
- Removes comments by tracking if the current character is within a string, character literal, single-line comment, or multi-line comment.
- Removes empty lines by checking if a line contains only whitespace characters.
- Supports processing specific files and directories last in the user-specified order, even when general ordering is not enabled.

## Error Handling

- Graceful handling of permission denied errors.
- Skips files exceeding size limit.
- Thread-safe error logging.
- Signal handling for clean interruption.
- Reports invalid regular expressions.
- Robust thread synchronization to prevent crashes with the ordered option.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
