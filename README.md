# File Operation Styler

A modern replacement for the standard Windows 11 file operation window.

File Operation Styler gives copy, move, delete, and recycle operations a cleaner modern layout while keeping the normal Windows file operation behavior.

## Screenshots

![File Operation Styler](images/file-operation-styler.png)

### Interactive Controls

![File Operation Styler interactive controls](images/file-operation-styler-controls.png)

The large progress circle can pause and resume operations, while optional settings can hide the smaller controls and title-bar percentage for a cleaner layout.

### Default vs File Operation Styler

![Default vs File Operation Styler](images/file-operation-styler-compare.png)

### Themes

![File Operation Styler Themes](images/file-operation-styler-themes-1.2.png)

## What's new in 1.3.0

- Fixed compatibility with newer Windows 11 26H2 builds.
- Added vertical scrolling for multiple simultaneous file operations.
- Added mouse-wheel and scrollbar navigation.
- Improved Glass text rendering and window resizing.

See [CHANGELOG.md](CHANGELOG.md) for the complete release history.

## Features

- Modern copy, move, delete, and recycle operation window
- Circular overall progress indicator
- Current-file progress bar
- Transferred size, remaining items, speed, and estimated time
- Progress graph in More Details view
- Multiple file operations in one window, with automatic vertical scrolling when needed
- Pause, resume, and cancel controls
- Interactive Pause / Resume control in the large progress circle
- Option to hide the small Pause / Resume and Cancel buttons
- Option to hide the percentage from the window title
- Works with normal Windows conflict and error dialogs
- Several built-in themes, including Frosted Glass
- Windows Acrylic and Custom Blur glass styles
- Custom colors, fonts, text sizes, and progress thickness

## Customization

Choose one of the included themes or adjust the available colors, typography, progress styling, and other presentation options to create your own look.

The Frosted Glass theme supports both Windows Acrylic and Custom Blur, including adjustable tint and opacity.

## Notes

File Operation Styler changes the appearance of the normal Windows file operation window only. Windows continues to handle the actual copy, move, delete, conflicts, errors, pause, resume, and cancel operations.

Settings changes apply to new file-operation windows; operations already in progress may use the native Windows presentation until they complete.

File Operation Styler has been tested on Windows 11 25H2 and 26H2 x64, including build 26300.9818. It relies on private Explorer and Shell presentation interfaces, so compatibility may vary between Windows builds. Unsupported presentations fall back to the native Windows UI.
