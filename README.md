# QMarkdown-explorer

A desktop Markdown browser for large local note collections, built with C++20 and Qt 6 Widgets.

## What it does

Point it at a folder; browse and read every `.md` file inside it without leaving the app.

- Tree view filtered to Markdown files, empty folders are hidden
- Search across file names **and** file contents, debounced
- Selecting a folder loads its `README.md`, if it has one
- `.ignore` file support, works like `.gitignore`, accepts regular expressions
- Three built-in themes (`System`, `Dark`, `Light`), hot-swappable
- 48 UI languages
- Repository shortcuts in the status bar, persisted to `repos.txt` beside the executable

## Requirements

- CMake 3.21+
- Qt 6 (Widgets, LinguistTools)
- A C++20 compiler (MSVC 2022, GCC 11+, *Clang 14+)

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

## Shortcuts

| Key | Action |
|---|---|
| `Ctrl+O` | Open folder |
| `Alt+Left` | Back |
| `Alt+Right` | Forward |
| `Ctrl+Wheel` | Zoom document font in/out |

## Status

Working and in use. Every push and every pull request runs the full automated test suite on Windows (MSVC) and Linux (GCC); both platforms pass CI. Bug reports welcome.

## License

GNU — see [LICENSE](https://github.com/MdONeilsl/QMarkdown-explorer/blob/main/LICENSE).

## Maintainer

[@MdONeilsl](https://github.com/MdONeilsl)
