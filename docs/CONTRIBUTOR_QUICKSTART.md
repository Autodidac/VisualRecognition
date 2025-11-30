# Contributor Quickstart: Runtime Flow

This guide summarizes how the app runs so you can trace code paths quickly when making changes.

## Startup sequence
- **Settings**: `pixelai.ini` is read from the executable directory. If it is missing, a default file is created with `[Saving]` → `BackupRetention=5` so backups work out of the box.
- **History**: Prior captures are loaded from `%TEMP%\\pixelai_captures`. Entries are sorted by timestamp and the newest capture becomes selected for preview and classify actions.
- **Model**: The recognizer looks for `pixelai_examples.bin` beside the executable. If it does not exist, a placeholder file with the correct header and patch size is created before loading so classification can proceed.

## Capture pipeline
- **Global mouse hook**: A low-level mouse hook watches for `WM_LBUTTONDOWN` events outside the app window. When triggered, it calls `DoCapture()` so you can capture without focusing the app.
- **Patch acquisition**: `CapturePatchAroundCursor()` reads a 481×481 BGRA region centered on the cursor, clamps the bounds to the virtual screen, and pads empty regions near monitor edges.
- **History entry**: Successful captures are appended to in-memory history, selected immediately, and saved to `%TEMP%\\pixelai_captures/<timestamp>.bin` (with a deduplicated suffix if needed). The preview invalidates to show the latest patch, and the status bar reflects success or failure.

## Classification flow
- **Global keyboard hook**: A low-level keyboard hook listens for `F5` and invokes `DoClassify()` even when the app is unfocused. The Classify button reuses the same function.
- **Scoring**: Classification runs cosine similarity over the normalized RGB feature vectors stored in memory, returning the best label when the score meets the configured confidence threshold (`PixelRecognizer::min_confidence`, default 0.85).

## Model training, saving, and backups
- **Learning**: The "Learn Label" button prompts for a label, adds the current capture as an example, and prepares to save `pixelai_examples.bin` in the executable directory.
- **Backups**: If a model already exists, the app confirms overwrite, writes a timestamped backup in the same directory, and enforces the `BackupRetention` limit from `pixelai.ini` by deleting oldest backups beyond the limit.
- **Persistence**: After optional backup, the updated examples are written to `pixelai_examples.bin`. Failures are surfaced in the status bar; classification relies on the file being readable at next startup.

## Adjusting settings (`pixelai.ini`)
- Location: Same directory as the executable; created automatically if missing.
- Keys: Under `[Saving]`, `BackupRetention` controls how many timestamped backups to keep (0 disables cleanup). Add more keys here as new features require them.

## Quick pointers to source
- **UI and hooks**: `VisualRecognition/src/ui.ixx` (`MouseHookProc`, `KeyboardHookProc`, `DoCapture`, `DoClassify`, `LoadCaptureHistory`, settings helpers).
- **Recognizer**: `VisualRecognition/src/pixelai.ixx` (feature extraction, cosine similarity, load/save format).
