# VisualRecognition

VisualRecognition is a Windows desktop utility that records portrait-sized screen grabs anchored to the mouse cursor and classifies them using an in-memory example-based model. The tool is geared toward rapid experimentation with visual snippets: capture a frame, label it, and immediately reuse it for recognition.

## Features
- Global hooks for quick actions: left-click to capture anywhere outside the UI, and low-level hotkeys (**F7** record, **F8** clear, **F9** play/stop, **F6** exit) to drive the macro engine even when the app is unfocused.
- Patch preview with simple status messaging and a live mouse-coordinate indicator so you can see exactly where captures and macro mouse events will land.
- Portrait captures: a fixed 360×960 BGRA32 frame with the cursor ~12% from the top. Pixels are clamped at screen edges so every capture stays full-sized.
- One-click learning: add a labeled patch, persist it to `pixelai_examples.bin`, and keep timestamped backups beside the model file according to the `BackupRetention` setting in `pixelai.ini`.
- Delete captures directly from the history list, removing the on-disk `.bin` entry when present.
- Reset the UI instantly with **Clear preview**, which wipes the on-screen image, clears capture history, and cleans up any saved capture files.
- Lightweight cosine-similarity recognizer implemented in C++23 modules.

## Build
1. Open `VisualRecognition.sln` in Visual Studio 2026 (MSVC v145 toolset) on Windows.
2. Ensure the MSVC toolchain supports C++23 modules.
3. Build the `VisualRecognition` project; the entry point is `VisualRecognition/src/main.cpp`, which imports the `ui` module.

## Run
- Launch the built executable on Windows.
- Use the buttons in the UI or hooks:
  - **Left click** (outside the window): capture a patch around the cursor and store a BMP in `captures/` under the app directory.
  - **Macro bar**: Record and Clear manage the captured macro, Play starts/stops playback, Repeat loops playback, and Exit stops playback and closes the app. These buttons mirror the global hotkeys **F7** (Record), **F8** (Clear), **F9** (Play), and **F6** (Exit) so you can drive macros while another window is active. Recording uses the global keyboard/mouse hooks, so captured events include clicks and keys from outside the app.
- **Learn Label**: prompt for a label, persist the model to `pixelai_examples.bin` in the app directory, and write timestamped backups next to it as governed by `BackupRetention` in `pixelai.ini`.
- **Classify**: run recognition on the selected capture from the UI (no global F5 shortcut). Status messages show scores with three-decimal precision for easier comparison between runs.
- **Delete Capture**: remove the selected history entry and delete the corresponding capture file if it exists.
- **Clear preview**: clear the preview pane, capture history, and any saved capture files from the current session.
- Follow the [bulk data collection guide](docs/BULK_DATA_COLLECTION.md) for recommended steps to capture, organize, label, and save patches for training.

### Model file validation
- The loader rejects corrupt or oversized `pixelai_examples.bin` files: dimensions must fit within reasonable bounds, label text is capped, and the pixel count must match the expected width × height.
- If a model fails to load, remove the damaged file (or restore from a timestamped backup) and retrain from known-good captures.

### Configuration
- Optional app-level settings live in `pixelai.ini` alongside the executable. Omit the file entirely to keep defaults.
- `BackupRetention` controls how many timestamped copies of `pixelai_examples.bin` to keep in the app directory. Missing, non-numeric, or non-positive values fall back to the default of 5 backups.

## Project Layout
- `VisualRecognition/src/interface_app.ixx`: Win32 UI host that wires together the interface partitions (state, capture, storage, layout, and mouse hooks).
- `VisualRecognition/src/vision_recognition_engine.ixx`: Example-based pixel recognizer and model persistence.
- `VisualRecognition/src/automation_macro_engine.ixx`: Macro recording/playback engine; `automation_macro_types.ixx` defines shared types and globals, and `automation_macro_hooks.ixx` exposes the hook entry points.
- `VisualRecognition/src/diagnostics_console_log.ixx`: Console-style logger that flushes messages into the UI log control.
- `VisualRecognition/src/main.cpp`: Entry point that launches the UI.
- `VERSION`: Semantic version for releases.
- `docs/`: Developer-facing documentation, including checklists and roadmap.

## Changelog
- See `docs/CHANGELOG.md` for release notes.

## Contributing
New to the codebase? Start with `docs/CONTRIBUTOR_QUICKSTART.md` for a runtime flow overview. See `docs/DEVELOPMENT_CHECKLIST.md` for the steps to follow when making changes. Update `VERSION` and documentation alongside functional updates to keep releases traceable.
