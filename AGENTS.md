# File Operation Styler development rules

This is a Windhawk C++ mod targeting explorer.exe on Windows 11.

## Goal

Restyle the native Windows Explorer file-operation progress window.

Preserve Windows' existing copy, move, delete, pause, resume, cancel,
progress calculation, conflict handling, error handling, and all actual
file-operation logic.

Do not implement a replacement file-copy engine.

The mod is a visual/layout layer over the native Explorer implementation.

## Current development target

Initial development target:

- Windows 11 24H2
- explorer.exe
- OperationStatusWindow file-operation progress UI

Do not assume compatibility with other Windows builds until tested.

Prefer graceful no-op behavior over risky partial compatibility.

## Known native UI structure

Top-level window:

- OperationStatusWindow

Known related native structure/elements include:

- OperationTileHost
- OperationTileDialog
- eltDisplayModeBtn
- eltPauseButton
- eltCancelButton
- eltSummary
- eltRateChart_New
- eltProgressBar
- eltItemName
- eltTimeRemaining
- eltItemsRemaining
- eltRegularTile
- eltDetails

Known useful implementation techniques:

- COperationStatusTile::\_CreateTileElement is a reliable tile-creation anchor
  on the current development build.
- DirectUI::StrToID plus FindDescendent can locate known DirectUI elements.
- PBM_GETPOS / PBM_GETRANGE can obtain genuine native completion progress.
- The UI contains DirectUI elements plus Win32 child controls.

These names and structures are implementation details, not stable APIs.

Do not assume internal classes, symbols, element IDs, or hierarchy are
guaranteed across Windows builds.

Validate pointers, elements, HWNDs, and expected structures defensively.

## Existing working functionality

Do not discard or redesign these working parts unless the current task
specifically requires it:

- custom circular completion-progress control
- genuine native completion percentage
- PBM_GETPOS / PBM_GETRANGE progress acquisition
- buffered circle rendering without flicker
- dark caption/body surface
- native caption showing "% complete"
- native completed and total byte acquisition
- custom "completed / total (%)" text
- individual DirectUI font/color styling
- resizing OperationStatusWindow
- reserved left column for the circular progress indicator
- existing DirectUI element-discovery helpers

Preserve working behavior while investigating remaining layout problems.

Do not rewrite the entire UI or replace native controls merely to solve
a positioning problem.

## Desired normal progress layout

The upper layout should remain geometrically stable between compact and
expanded modes.

Shared upper area:

- circular completion indicator
- "Copying X items from ... to ..."
- completed bytes / total bytes (%)
- speed
- time remaining
- items remaining
- straight completion progress bar
- pause/cancel controls as appropriate

Compact mode:

- no speed-history graph
- straight bar represents completion progress
- circular indicator also represents completion progress

Expanded mode:

- same upper layout and positions as compact mode
- native speed-history graph/details are added underneath
- switching More Details / Fewer Details should not rearrange the common
  upper layout unnecessarily

Do not confuse the native speed-history graph with completion progress.

## Current immediate milestone

The current task is NOT a visual redesign.

The immediate task is to reliably determine and control the native
compact/expanded transition.

Investigate what Explorer actually does when the user performs:

1. Fewer details
2. More details
3. Fewer details
4. More details

Determine:

- how compact versus expanded state can be identified reliably
- which native Shell32 method/event performs or completes the transition
- when Windows applies native visibility changes
- when Windows applies native geometry/layout changes
- the safest point at which this mod should reapply its layout

Before implementing another visual layout change, instrument the current
implementation to log at least:

- eltDetails existence, visibility, and bounds
- eltRateChart_New existence, visibility, and bounds
- eltProgressBar existence, visibility, and bounds
- native progress HWND existence, visibility, and rectangle
- eltRegularTile bounds
- OperationStatusWindow client and window size
- eltDisplayModeBtn state/text when safely obtainable

Log state both before and after relevant native transitions where possible.

Do not guess compact/expanded state solely from one geometry value unless
testing demonstrates that it is reliable.

## Other native file-operation states

Normal copy/move progress is only one presentation.

Other states include at least:

- normal compact progress
- normal expanded progress
- Replace or Skip Files
- Compare/choose-file conflict view
- permission/error/interruption presentations

Treat these as separate states.

Do not allow normal-progress styling to spill blindly into conflict,
error, permission, or interruption screens.

Do not attempt to skin those additional states until normal compact and
expanded progress are reliable.

## Safety

Explorer stability is more important than visual customization.

- Fail safely if an expected internal object, element, HWND, or symbol
  cannot be found.
- Never modify file-operation behavior or file-operation data.
- Never hook by hard-coded address.
- Undocumented Shell functions may only be hooked after the relevant
  symbol/implementation has been confirmed for the target build.
- Prefer existing proven hooks over adding new hooks.
- Avoid unnecessary global hooks.
- Avoid high-frequency polling when a deterministic native transition
  or callback is available.
- Avoid LayoutUpdated, PointerMoved, CompositionTarget::Rendering, or
  equivalent continuous callbacks as layout synchronization mechanisms
  unless there is no safer alternative.
- Every hook, subclass, callback, timer, event registration, and resource
  must have a reliable teardown path.
- The mod must restore modified native visual/layout state when disabled
  where technically possible.
- Never leave custom child windows attached after unload.
- Do not add user settings until initialization, refresh, state handling,
  and unload are reliable.
- Make one focused change at a time.
- Explain the likely cause of a bug before editing.
- Review every diff for lifecycle, re-entrancy, stale pointers,
  null pointers, thread affinity, and Explorer crash risks.
- Preserve valid Windhawk metadata and compilability.

## Development workflow

Before editing:

1. Read the complete current source.
2. Understand the existing hooks and layout path.
3. Identify what is already working.
4. State the specific cause or hypothesis being investigated.
5. Make the smallest change needed to test that hypothesis.

After editing:

1. Review the complete diff.
2. Check for accidental unrelated changes.
3. Check initialization and unload symmetry.
4. Check pointer/object lifetime.
5. Check re-entrancy risks.
6. Check UI-thread assumptions.
7. Confirm the mod still compiles.
8. Explain exactly what should be tested manually.

Do not perform broad refactors while investigating a specific UI-state bug.

Do not replace known-working code merely because an alternative design
looks cleaner.

## Future settings

Later, Windhawk settings may expose visual parameters such as:

- accent color
- text colors
- circle size
- circle thickness
- font sizes
- spacing
- positioning

Do not implement these yet.
