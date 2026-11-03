# Issue #6293: transfer viewport validation

The custom host previously grew by `tile count * row height` without a work-area
limit. Rows were then divided equally across the client height, so simply capping
the window would compress them or leave operations inaccessible.

The change keeps the existing DPI-scaled row heights and footer geometry. Host
height is limited by the selected monitor's work area, the proposed top edge, and
the measured non-client height. Only an off-screen or unusably low top edge is
adjusted. A standard child scrollbar appears when the content overflows, using
`GetSystemMetricsForDpi` for its width. Fitting hosts have zero offset and no
visible scrollbar.

Each host owns its scrollbar, offset, wheel remainder, DPI, and failure state.
The common mapping is `row index * row height - host offset`. Glass uses this
mapping for buffered painting, clipping, ring positions and pointer routing.
Non-Glass uses it to move and clip only the mod-owned information-panel HWNDs;
Explorer-owned DirectUI elements and progress controls keep their native geometry.
The existing footer seam is excluded from the viewport, preserving fitting layouts.

Only this host's scrollbar messages are consumed. Wheel input respects the system
scroll-lines setting, accumulates partial detents, and leaves Ctrl/Shift wheel
messages alone. The standard scrollbar provides keyboard and accessibility support.
Special native presentations suspend custom scrolling. Failure to initialize,
position or clip the custom viewport restores native visuals and captured native
size, and keeps that HWND native for the remainder of its lifetime. Scrollbar
cleanup runs on the owning UI thread when the last transfer finishes or the host
is torn down. No new hooks, timers, settings or native scrolling overrides are used.

## 26H2 geometry regression

The runtime failure on build 26300.9818 reported a requested client size of
570x200 but the native 447x133 client remained. The resize scope suppressed native
geometry capture and recursive resizing. The scrolling change also used that flag
to skip the post-native `WM_WINDOWPOSCHANGING` correction, allowing a synchronous
native rewrite of our size request to reach USER32. Immediate size verification
then marked the host permanently native. The production-subclass regression test
reproduced this sequence with the reported 463x172 outer / 447x133 client geometry
at 96 DPI; it also exercises the styled size at the test monitor's actual DPI.

The scope now retains the owned size request. After the native window procedure
runs, the subclass restores that request's `WINDOWPOS` without recursively calling
`SetWindowPos`. Recursive sizing and capture suppression remain in effect. A
targeted log records native rewrites only when they occur during an owned resize.

Verification remains mandatory. A mismatch waits for `WM_WINDOWPOSCHANGED` or one
posted host-layout verification, without another resize attempt. A matching native
completion permits styling; a persistent mismatch restores native presentation.
Verification posts carry a request ID so an older layout/verification message
cannot prematurely fail a newer request before its native completion runs.
Changed transfer count/mode/DPI invalidates the old target rather than verifying
against stale expectations. Pending targets are canceled on native fallback,
teardown and monitor/work-area changes. There are no polling timers or retry loops.

Pre-transition native geometry no longer initializes the viewport: non-final mode
updates defer to the existing host-layout callback. Verified fitting layouts need
no scrollbar initialization or scroll-state allocation merely for layout; only a
deferred mismatch needs a per-host geometry record. Duplicate native visuals
are suppressed only after the final geometry and presentation are ready.

The test covers synchronous rewriting, asynchronous completion, persistent failure,
bounded callback processing, native restoration, and an ordinary styled single
transfer with no scrollbar or scroll-state entry. These checks reproduce the code
failure mechanism, but the actual 26H2 native callback and final appearance still
require runtime confirmation using the new targeted log.

## Touched production functions

- Sizing/mapping: `CalculateCustomHostWindowHeight`,
  `GetVerifiedCustomHostWindowHeight`, `ResizeOperationStatusWindowForMode`,
  `GetMultiTileSlotBounds`, `GetCirclePlacement`.
- Layout/rendering/input: `OperationStatusWindowSubclassProc`,
  `DrawInfoPanelFrame`, `GetInfoPanelCancelRect`, `GetInfoPanelPauseRect`,
  `PositionInfoPanel`, `PositionProgressCirclesForHost`, `PositionFooterOverlay`,
  `ApplyDisplayMode`.
- Fallback/lifetime: `IsHostInSpecialOperationState`,
  `HideCustomPresentationForHost`, `ScheduleCustomReapplyForHost`,
  `DestroyProgressCircle`, `DestroyProgressCirclesForHost`,
  `RemoveHostSubclassRecord`, `RemoveDestroyedHostRecords`.
- New helpers: `ReadHostScrollState`, `GetHostTransferRows`,
  `CalculateTransferViewport`, `GetTransferViewport`,
  `UpdateHostTransferViewport`, `RouteTransferScrollMessage`,
  `SuspendHostTransferViewport`, `DestroyHostTransferViewport`,
  `ForgetHostTransferViewport`, `FailHostTransferViewport`,
  `HostGeometryMatches`, `ClearPendingHostGeometry`, `VerifyPendingHostGeometry`.
- Geometry ownership: `ScopedHostGeometryChange`.

Glass text hints, fonts, colors, version metadata and the optional `_CalculateRate`
hook are unchanged by this patch.

## Automated checks

Run from the repository on Windows:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\run-transfer-viewport-tests.ps1
```

The runner uses the installed Windhawk compiler and Windows SDK manifest tool.
Override `-CompilerRoot` or `-ManifestTool` if installed elsewhere. Build artifacts
go to `%TEMP%\FileOperationStyler-overflow-tests`.

The viewport suite runs twice: first without a v6 executable manifest, then with
the original Explorer-like manifest. Both runs initialize the production v6
subclass API before creating test windows and release it after UI teardown.

The runner also builds `common-controls-v6-tests.cpp` as a manifest-free host.
It retains Common Controls v5.82, then verifies that the production loader selects
a distinct v6 module and restores the previous thread activation context. Real
hidden-window checks cover subclass installation, reference-data updates, lookup,
dispatch through `DefSubclassProc`, removal, destruction, and reinitialization.
Test-only API substitutions exercise context creation/activation and DLL-loading
failures, a wrong DLL version, and each missing required export (including
`DllGetVersion`). Each failure must leave the API empty and return from
`Wh_ModInit` before UI classes, GDI+, or skin target modules are initialized.
The installed v5 DLL may expose some subclass names on other Windows builds;
missing-export coverage is injected and does not depend on that variation.

The compatibility layer uses `CreateActCtxW` with the system-directory
`shell32.dll` manifest resource 124, which declares the Common Controls v6
dependency on the validation machine. It activates this context only around
`LoadLibraryExW`, deactivates it on the same thread, and releases the context.
`DllGetVersion` must report major version 6 and all four subclass exports must
resolve before the API is published. The owned DLL reference remains until
final shutdown, after existing subclass/window teardown, including initialization
rollback. No process-default or window-creation activation context is changed.

Resource 124 is a Windows implementation detail; missing/changed resources or
failed v6 selection disable initialization safely. These synthetic checks do not
establish OneCommanderConnector or OneCommander UI compatibility. Review the x64
production DLL's PE import table separately: none of `SetWindowSubclass`,
`GetWindowSubclass`, `RemoveWindowSubclass`, or `DefSubclassProc` may appear as
static imports. Keeping `-lcomctl32` in the mod metadata is intentional.

The test executable includes the production source with Windhawk's inert
`WH_EDITING` APIs. It creates isolated Win32 tool windows outside the virtual
desktop and never installs hooks or touches Explorer/file operations. It checks:

- Fixed row geometry for 1, 2, 4, 6 and 10 transfers in both modes at 96/144 DPI.
- Work-area limits at ordinary, near-bottom and off-screen proposed positions.
- Real child scrollbar range/position, keyboard input, and wheel accumulation.
- Independent active hosts; additions, leading-row removals and shrink-to-fit.
- Compact/expanded transitions while scrolled; matching row and pointer mapping.
- Non-Glass child regions and Glass raster clipping at viewport boundaries.
- Persistent native fallback after losing a required scrollbar.
- Last-transfer completion and cleanup of scrollbar, footer, row and host state.
- Input before a queued DPI refresh, ensuring the offset is converted only once.
- Synchronous resize callbacks, ensuring an owned resize cannot recursively size
  the same host or reposition rows before the outer resize completes.
- Native fallback and registry retirement during a scrollbar update, ensuring
  the outer update cannot publish stale state or leave an orphan scrollbar.
- Explicit preservation of surviving tile order across additions and removals.

The automated checks do not confirm DWM/Acrylic appearance, Explorer-owned action
dispatch, real native special-state restoration, or actual mixed-monitor DPI
transitions. Those require the runtime checklist below.

## Explorer runtime checklist

Use only the local development mod, rebuilt from this repository. On a 1920x1080
monitor, run this matrix at **100% and 150% scaling**, in compact and expanded mode,
with Acrylic, Custom Blur and an opaque preset:

| Simultaneous transfers | Required observation |
| --- | --- |
| 1 | Existing appearance/size when it fits; no scrollbar or reserved strip. |
| 2 | Existing appearance, row spacing and footer when they fit; independent actions. |
| 4 | Normal growth until the work-area limit; scrolling only if needed for this mode, DPI and position. |
| 6 | Same row heights as before the cap; every row and control reachable after scrolling. |
| 10 | Window/footer remain in the work area; top and bottom rows reachable with wheel, arrows, pages and thumb drag. |

For overflowing hosts:

1. Scroll to the middle and bottom. On several different visible rows, test the
   ring's hover/pause/resume interaction, small Pause/Resume and Cancel. Confirm
   each action affects the indicated transfer. Check small-controls-hidden mode.
2. Confirm the footer never scrolls, gets covered, or becomes inaccessible. Test
   More/Fewer Details and bottom Cancel. Repeat with the solid footer option.
3. Add transfers while scrolled. Finish/cancel rows above and below the viewport,
   including the leading row. Shrink back to two, then one: offset must clamp,
   scrollbar disappear when unnecessary, and remaining transfers stay reachable.
4. Finish the last transfer while scrolled. Reopen a progress window: no stale
   offset, orphan scrollbar, blank row, or unusable footer should remain.
5. Move the host near the bottom edge, minimize/restore it, change the work area,
   and move between 100%/150% monitors. Verify the work-area cap, scrollbar width,
   thumb range, clipping and clickable regions update together.
6. Open two independent hosts and scroll one. The other's offset must not change.
7. Trigger Replace/Skip and a permission/error presentation while scrolled. The
   complete native UI must remain usable; returning to normal must restore the
   viewport. Disable/reload the mod during progress and check native restoration.
8. Inspect percentage, description, size, speed/time, graph labels and footer
   text for regressions. Confirm ring/bar animation and progress data remain correct.

Treat Windows build compatibility and visual correctness as unconfirmed until
these Explorer tests pass on the intended builds.
