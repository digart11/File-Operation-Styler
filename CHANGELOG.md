# File Operation Styler changelog

## 1.4.0

- Added experimental support for third-party file managers using compatible native Windows file-operation dialogs.
- Added runtime Common Controls v6 subclass API resolution for additional application processes.
- Tested with OneCommander and File Pilot 0.8.5 standalone.
- Added manual process inclusion instructions. Windows Explorer continues working automatically.
- Preserved existing Explorer styling and native file-operation behavior.

## 1.3.0
- Fixed initialization on newer Windows 11 26H2 builds where the native _CalculateRate symbol is unavailable, using the existing measured transfer-rate fallback.
- Fixed multiple simultaneous file operations extending beyond the screen by limiting the window to the monitor work area and adding vertical scrolling.
- Added scrollbar and mouse-wheel navigation while keeping individual transfer controls and the shared footer accessible.
- Improved layout positioning and cleanup as grouped operations are added, removed, or completed.
- Improved Glass text antialiasing for smoother rendering.
- Improved handling of Explorer's native window-size rewrites during custom layout changes.

## 1.2.0

- Added a Frosted Glass theme with Windows Acrylic and Custom Blur styles.
- Added current-file progress while the circular indicator continues to show overall operation progress.
- Added interactive Pause / Resume behavior to the large progress circle.
- Added an option to hide the small Pause / Resume and Cancel buttons in the upper-right corner. The large progress-circle control and bottom Cancel button remain available.
- Added an option to hide the percentage from the file-operation window title.
- Improved multiple-operation support, including synchronized More / Fewer Details behavior.
- Improved fallback to the native Windows UI for conflicts, errors, and unsupported presentation states.
- Improved restoration and teardown when the mod is disabled, reloaded, or settings are changed.

## 1.1.0

- Added Frosted Glass theme with full-window glass styling and customizable footer appearance.
- Added optional current-file progress bar showing progress for the file currently being copied or moved.
- Added footer customization with Glass or Solid styles, including custom solid footer colors.
- Improved progress animations with smoother updates and reduced unnecessary repainting during long operations.
- Improved handling of multiple simultaneous file operations and operation lifecycle cleanup.
- Reduced overhead when current-file progress tracking is disabled by avoiding unnecessary file metadata tracking and shared progress processing.
- Improved reliability when switching between normal progress views and special Explorer states such as conflicts or permission prompts.

## 1.0.0

- Initial public release.
- Modern custom layout for Windows 11 copy, move, delete, and recycle operations.
- Compact and expanded views with circular percentage indicator.
- Shows transferred size, current item, remaining items, speed, and estimated time.
- Progress graph in More Details view.
- Supports multiple simultaneous file operations.
- Preserves native Pause, Resume, Cancel, conflict, and error handling.
- Includes built-in themes and customizable colors, typography, and progress styling.

## 0.12.0 architecture-alpha

- Replaced the visible normal-operation DirectUI composition with one opaque,
  custom-rendered presentation surface per native operation tile.
- The custom surface now owns the description, transferred/total summary,
  items remaining, speed, time remaining, completion bar, speed graph,
  Pause/Resume button, and Cancel button. The existing circular completion
  control remains custom rendered.
- Added a custom shared More/Fewer Details footer. Pause/Resume, Cancel, and
  More/Fewer invoke the corresponding live DirectUI button through
  `DirectUI::Button::DefaultAction`; no file-operation logic or synthetic mouse
  input is implemented by the mod.
- Removed native DirectUI bounds, margins, padding, fonts, colors, chart size,
  progress-bar size, and container backgrounds from the active normal-mode
  layout path. The old mutation helpers remain temporarily in the source only
  as unreachable migration/diagnostic code and are no longer startup
  requirements.
- Retained native DirectUI discovery for tile identity, normal/special state,
  copy/move/delete classification, native description text, and native action
  lookup. Native progress, byte/item counters, and rate calculation remain the
  data sources.
- All custom HWND and GDI+ geometry starts as 96-DPI logical units and is
  converted once using the actual `OperationStatusWindow` DPI.
- Native controls stay alive underneath the opaque normal presentation. On a
  conflict, permission, file-in-use, or other detected special state, all
  custom surfaces are hidden and the untouched Explorer UI is revealed. The
  custom surfaces are recreated/repositioned when normal progress resumes.
- Presets, custom colors, custom fonts, logical layout settings, graph opacity,
  and element visibility settings continue to apply to custom-rendered UI.
  Legacy `nativeFont`, `nativeDetailSize`, `nativeValueWeight`,
  `nativeLabelWeight`, `actionSize`, and `actionWeight` settings are retained
  for settings compatibility but are temporarily inactive because 0.12 does
  not skin native DirectUI visuals.
