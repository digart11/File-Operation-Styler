# \# File Operation Styler development rules

# 

# This is a Windhawk C++ mod targeting explorer.exe on Windows 11.

# 

# \## Goal

# 

# Restyle the native Windows Explorer file-operation progress window.

# 

# Preserve Windows' existing copy, move, delete, pause, resume, cancel,

# progress calculation, error handling, and file-operation logic.

# 

# Do not implement a replacement file-copy engine.

# 

# \## Known UI structure

# 

# Top-level window:

# \- OperationStatusWindow

# 

# Known internal elements:

# \- OperationTileHost

# \- OperationTileDialog

# \- DisplayModeButton / eltDisplayModeBtn

# \- PauseButton / eltPauseButton

# \- CheckButton / eltCancelButton

# \- Element / eltSummary

# \- ChartView / eltRateChart\_New

# \- CCProgressBar / eltProgressBar

# \- ShellItemLink / eltItemName

# \- Element / eltTimeRemaining

# \- Element / eltItemsRemaining

# 

# The file-operation UI uses DirectUI plus some Win32 child windows.

# 

# Do not assume internal class names or AutomationIds are guaranteed across

# all Windows builds. Detect expected structures defensively.

# 

# \## Safety

# 

# \- Explorer stability is more important than visual customization.

# \- Fail safely if an expected internal object or symbol cannot be found.

# \- Never modify file-operation behavior or data.

# \- Do not hook undocumented functions without first confirming the symbol,

# &#x20; class, or proven implementation pattern.

# \- Avoid hard-coded addresses.

# \- Avoid assumptions based on one Windows build when possible.

# \- Every hook or modification must have a reliable unload path.

# \- The mod must restore all visual changes when disabled.

# \- Do not add settings until initialization, refresh, and unload are reliable.

# \- Make one focused change at a time.

# \- Explain the cause of a bug before editing.

# \- Review every diff for lifecycle, re-entrancy, null-pointer, and Explorer

# &#x20; crash risks.

# \- Preserve valid Windhawk metadata and compilability.

# 

# \## Development approach

# 

# Start with diagnostics only.

# 

# First milestone:

# \- Detect OperationStatusWindow.

# \- Locate the DirectUI file-operation elements safely.

# \- Log discovered classes/elements.

# \- Do not change appearance yet.

# 

# Only after detection is reliable:

# \- test harmless visual property changes

# \- verify collapsed mode

# \- verify expanded mode

# \- verify pause/resume

# \- verify multiple simultaneous operations

# \- verify disabling/unloading restores the original UI

# 

# \## Compatibility

# 

# Initial development target is Windows 11 24H2.

# 

# Do not assume compatibility with other Windows builds until tested.

# Prefer graceful no-op behavior over risky partial compatibility.

