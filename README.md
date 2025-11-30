# VisualRecognition

VisualRecognition is a Windows desktop utility that captures small pixel patches around the mouse cursor and classifies them using an in-memory example-based model. The tool is geared toward rapid experimentation with visual snippets: capture a region, label it, and immediately reuse it for recognition.

## Features
- Global hooks for quick actions (left-click to capture anywhere, F5 to classify) even when the window is not focused.
- Patch preview with simple status messaging.
- One-click learning: add a labeled patch and persist it to `pixelai_examples.bin`.
- Lightweight cosine-similarity recognizer implemented in C++20 modules.

## Build
1. Open `VisualRecognition.slnx` in Visual Studio 2022 (preview solution format) on Windows.
2. Ensure the MSVC toolchain supports C++20 modules.
3. Build the `VisualRecognition` project; the entry point is `VisualRecognition/src/main.cpp`, which imports the `ui` module.

## Run
- Launch the built executable on Windows.
- Use the buttons in the UI or hooks:
  - **Left click**: capture a patch around the cursor.
  - **F5**: classify the most recent capture globally (even when the app is not focused).
  - **Learn Label**: prompt for a label and persist the model to `pixelai_examples.bin` in the working directory.

## Project Layout
- `VisualRecognition/src/ui.ixx`: Win32 UI, capture logic, and interaction with the recognizer.
- `VisualRecognition/src/pixelai.ixx`: Example-based pixel recognizer and model persistence.
- `VisualRecognition/src/main.cpp`: Entry point that launches the UI.
- `VERSION`: Semantic version for releases.
- `docs/`: Developer-facing documentation, including checklists and roadmap.

## Contributing
See `docs/DEVELOPMENT_CHECKLIST.md` for the steps to follow when making changes. Update `VERSION` and documentation alongside functional updates to keep releases traceable.
