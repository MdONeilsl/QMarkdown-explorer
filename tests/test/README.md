# Hyperlink Manual Test

Open the folder containing this file as the root in QMarkdown-explorer, then
click through each document.

## What to verify

* **Anchors scroll within the current file.** They never open a new document.
* **Cross-file links open the target document.** They never scroll.
* **Mixed links (`file.md#section`) do nothing.** No navigation, no scroll.
* **Web links are plain text.** `[Example](https://example.com)` renders as
  `Example (https://example.com)` and is not clickable.
* **Out-of-root links do nothing.** `../README.md` reaches above the root
  and is rejected.
* **Missing anchors are silent.** Clicking `#does-not-exist` leaves the view
  untouched.
* **Duplicate headings.** Three `## Section` headings in the anchors
  document are reachable as `#section`, `#section-1`, `#section-2`.

## Documents

* [Anchors](links-anchors.md) — intra-document scrolling
* [Cross-file](links-cross-file.md) — opening sibling documents
* [Rejected](links-rejected.md) — cases that must do nothing
* [Nested child](nested/child.md) — direct navigation to a subdirectory

[Back to top](#hyperlink-manual-test)