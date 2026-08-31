# File Operation Styler changelog

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

