# VisualRecognition

VisualRecognition is a Windows desktop utility that captures small pixel patches around the mouse cursor and classifies them using an in-memory example-based model. The tool is geared toward rapid experimentation with visual snippets: capture a region, label it, and immediately reuse it for recognition.

## Features
- Global hooks for quick actions (left-click to capture anywhere, F5 to classify) even when the window is not focused, plus a macro bar (Record, Clear, Play, Repeat, Exit) that rides on the same hooks to control automation without refocusing the app.
- Patch preview with simple status messaging and a live mouse-coordinate indicator so you can see exactly where captures and macro mouse events will land.
- Captures stay centered on the cursor with padding near screen edges, keeping previews and training sizes consistent.
- One-click learning: add a labeled patch, persist it to `pixelai_examples.bin`, and keep timestamped backups beside the model file according to the `BackupRetention` setting in `pixelai.ini`.
- Delete captures directly from the history list, removing the on-disk `.bin` entry when present.
- Lightweight cosine-similarity recognizer implemented in C++23 modules.

## Build
1. Open `VisualRecognition.slnx` in Visual Studio 2026 (preview solution format) on Windows.
2. Ensure the MSVC toolchain supports C++23 modules.
3. Build the `VisualRecognition` project; the entry point is `VisualRecognition/src/main.cpp`, which imports the `ui` module.

## Run
- Launch the built executable on Windows.
- Use the buttons in the UI or hooks:
  - **Left click**: capture a patch around the cursor.
  - **F5**: classify the most recent capture globally (even when the app is not focused).
  - **Macro bar**: Record and Clear manage the captured macro, Play starts/stops playback, Repeat loops playback, and Exit stops playback and closes the app. These buttons mirror the global hotkeys **F7** (Record), **F8** (Clear), **F9** (Play), and **F6** (Exit) so you can drive macros while another window is active. Recording uses the global keyboard/mouse hooks, so captured events include clicks and keys from outside the app.
- **Learn Label**: prompt for a label, persist the model to `pixelai_examples.bin` in the working directory, and write timestamped backups next to it as governed by `BackupRetention` in `pixelai.ini`.
- **Delete Capture**: remove the selected history entry and delete the corresponding `.bin` file if it exists.
- Classification status messages show scores with three-decimal precision for easier comparison between runs.
- Follow the [bulk data collection guide](docs/BULK_DATA_COLLECTION.md) for recommended steps to capture, organize, label, and save patches for training.

## Project Layout
- `VisualRecognition/src/ui.ixx`: Win32 UI, capture logic, and interaction with the recognizer.
- `VisualRecognition/src/pixelai.ixx`: Example-based pixel recognizer and model persistence.
- `VisualRecognition/src/main.cpp`: Entry point that launches the UI.
- `VERSION`: Semantic version for releases.
- `docs/`: Developer-facing documentation, including checklists and roadmap.

## Changelog
- See `docs/CHANGELOG.md` for release notes.

## Contributing
New to the codebase? Start with `docs/CONTRIBUTOR_QUICKSTART.md` for a runtime flow overview. See `docs/DEVELOPMENT_CHECKLIST.md` for the steps to follow when making changes. Update `VERSION` and documentation alongside functional updates to keep releases traceable.
