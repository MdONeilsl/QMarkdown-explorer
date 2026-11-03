# Rejected Links

Each link below must do nothing when clicked. No navigation, no scroll.

## Mixed file + anchor

* [Mixed: child.md#child](nested/child.md#child)
* [Mixed: links-anchors.md#simple](links-anchors.md#simple)

These look like they should open the file and jump to the section. They do
neither — this is by design, so behaviour never depends on whether the
target happens to exist.

## Web links (rewritten to plain text before parsing)

* [Example](https://example.com)
* [Qt](https://www.qt.io)
* https://github.com/MdONeilsl/QMarkdown-explorer — bare URL, no markdown
  link syntax.

`convertWebLinksToPlainText` rewrites each `[text](url)` to `text (url)`
before the markdown is parsed, so no anchor reaches the renderer. Even a
bare URL that survives as an anchor is rejected by the `http` / `https`
scheme check in `handleLink`.

## Out of root

* [Parent of root: ../README.md](../README.md)
* [Two levels up: ../../README.md](../../README.md)

The application knows only about the folder you opened as the root. Any
target that canonicalises outside it is rejected before the click has any
effect.

## Silent no-ops

* [Anchor that resolves to nothing: #](#)
* [Missing anchor in this file](#does-not-exist)

Both leave the view untouched.