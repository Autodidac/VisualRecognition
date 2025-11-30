# Changelog

## 0.1.6
- Renamed the module set to a clearer cross-system naming scheme (interface, automation, diagnostics, vision) and updated imports/projects accordingly.
- Corrected build guidance to target MSVC toolset v145 and bumped `VERSION` to 0.1.6.

## 0.1.5
- Corrected README to reflect the current capture format (360×960 portrait frames saved to `captures/`), the macro hotkeys (**F6**–**F9**), and the lack of a global F5 classification hook.

## 0.1.4
- Reorganized the UI layout with a top-aligned status bar, wider spacing, and a dedicated Clear preview control for quickly resetting the workspace.
- Added stronger capture/delete guards to avoid accidental captures when clicking UI controls and to surface history counts in status messages.
- Improved cleanup by allowing history clearing to remove on-disk captures and by tightening deletion feedback.

## 0.1.3
- Fixed startup layout alignment so the initial window shows all controls without resizing.
- Updated macro documentation to reflect the latest behavior and hotkey interactions.
