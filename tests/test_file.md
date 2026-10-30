# Code Block Rendering Test

A small file to visually verify the code-block styling. Open this in QMarkdown-explorer and switch between the three themes.

## Inline code

Use `setDefaultStyleSheet()` to apply CSS to the document.

## Simple fenced block

```
plain text inside a fence
no language tag
```

## Fenced block with language tag

```cpp
#include <QTextDocument>

void applyStyles(QTextDocument *doc) {
    doc->setDefaultStyleSheet(QStringLiteral("pre { padding: 8px; }"));
}
```

```python
def greet(name):
    return f"Hello, {name}!"
```

## Mixed inline and block

Paragraph before the block, with `inline_code()` in the middle.

```json
{
    "theme": "dark",
    "background": "#2b2f34",
    "foreground": "#e6e8ea"
}
```

Paragraph after the block.

## Long line (wrapping behaviour)

```text
This is a deliberately long line of text inside a code fence to check how wrapping is handled in the current view width. It should wrap because the widget uses WidgetWidth line wrapping.
```

## Code block after a heading

### Subsection

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build --config Debug --parallel
ctest --test-dir build --output-on-failure -C Debug
```

## Code block with an image nearby

The image below must still render after a theme switch — that is the regression guarded by `MarkdownDocument`'s `loadResource()` override.

![placeholder](does-not-exist.png)

```cpp
// Even a missing image above must not break this block.
int x = 42;
```

## Indented code block (four spaces)

    this is an indented code block
    Qt treats it as <pre><code> too

## Block quote containing a code block

> A quoted block:
>
> ```
> quoted code
> ```
>
> And the quote continues.

## Last block

```
end of test
```