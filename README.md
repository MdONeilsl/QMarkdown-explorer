# QMarkdown-explorer

A desktop Markdown browser for large local note collections, built with C++20 and Qt 6 Widgets.

## What it does

Point it at a folder; browse and read every `.md` file inside it without leaving the app.

- Tree view filtered to Markdown files — empty folders are hidden
- Search across file names **and** file contents, debounced
- Selecting a folder loads its `README.md`, if it has one
- Three built-in themes (`default`, `dark`, `light`), hot-swappable
- 48 UI languages
- Repository shortcuts in the status bar, persisted to `repos.txt` beside the executable

## Requirements

- CMake 3.21+
- Qt 6.12 (Widgets, LinguistTools)
- A C++20 compiler (MSVC 2022, GCC 11+, Clang 14+)

## Build

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.12
cmake --build build --config Release
```

The executable lands in `build/` (or `build/Release/` on MSVC). Styles, translations, Qt runtime DLLs and the platform plugin are copied next to it automatically.

## Run

```sh
./QMarkdown-explorer                          # prompt for a folder
./QMarkdown-explorer --folder ~/notes         # open directly
./QMarkdown-explorer --style dark --language de
./QMarkdown-explorer --list-languages         # what's installed
```

Full option list: `./QMarkdown-explorer --help`.

## Status

Working and in use, but no automated test suite. Cross-platform builds are exercised on Windows; Linux and macOS are untested beyond compilation. Bug reports welcome.

## License

MIT — see [LICENSE](LICENSE).

## Maintainer

[@your-username](https://github.com/your-username)