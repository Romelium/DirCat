# DirCat

DirCat is a high-performance C++ utility that concatenates and displays the contents of files in a directory, similar to the Unix `cat` command but for entire directories. It supports multi-threaded processing, recursive directory traversal, and filtering options.

## Features

- Multi-threaded file processing for improved performance
- Recursive directory traversal (optional)
- File extension filtering
- Maximum file size limiting
- Ignore specific folders
- Ignore specific files
- Regular expression filtering for excluding files
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
   git clone https://github.com/your-username/dircat.git
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

## Usage

```bash
./dircat <directory_path> [options]
```

### Options

- `-m, --max-size <MB>`: Maximum file size in MB (default: 10)
- `-n, --no-recursive`: Disable recursive directory search
- `-e, --ext <ext>`: Process only files with the specified extension (can be used multiple times)
- `-d, --dot-folders`: Include folders starting with a dot (ignored by default)
- `-i, --ignore <item>`: Ignore specific folder or file (can be used multiple times)
- `-r, --regex <pattern>`: Exclude files matching the regex pattern (can be used multiple times)

### Examples

Display all files in the current directory:

```bash
./dircat .
```

Process only C++ files up to 20MB in size:

```bash
./dircat . --max-size 20 --ext cpp
```

Process files in the current directory without recursion:

```bash
./dircat . --no-recursive
```

Ignore `build` folder and `temp.txt` file:

```bash
./dircat . --ignore build --ignore temp.txt
```

Include folders starting with a dot:

```bash
./dircat . --dot-folders
```

Exclude all files ending with `.tmp` or `.log`:

```bash
./dircat . -r "\.tmp$" -r "\.log$"
```

Exclude files containing the word "backup":

```bash
./dircat . -r "backup"
```

## Output Format

Files are output in the following format:

````
### File: filename.ext
```ext
[file contents]
````

## Implementation Details

- Uses modern C++ features including filesystem, threading, atomic operations, and regular expressions.
- Limits maximum thread count to 8 or hardware concurrency, whichever is lower.
- Processes files in chunks for memory efficiency.
- Maintains consistent output ordering regardless of thread execution order.
- Handles large files efficiently through buffered reading.

## Error Handling

- Graceful handling of permission denied errors.
- Skips files exceeding size limit.
- Thread-safe error logging.
- Signal handling for clean interruption.
- Reports invalid regular expressions.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
