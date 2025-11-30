# Roadmap and Next Steps

## Current State
- Win32 UI (`src/ui.ixx`) captures a patch around the cursor and displays it in a preview area.
- Global mouse hook triggers capture on left click even when the app is unfocused; classification is handled by a global F5 keyboard hook.
- The `PixelRecognizer` (`src/pixelai.ixx`) stores labeled patches, performs cosine-similarity matching, and saves/loads models to `pixelai_examples.bin`.
- Learning flow prompts for a label, trains in-memory, and attempts to persist the model immediately.
- Timestamped backups of `pixelai_examples.bin` are written alongside the model file and trimmed according to the `BackupRetention` setting in `pixelai.ini`.
- Startup loads `pixelai_examples.bin`, creating a placeholder file when none exists and surfacing load failures through the status bar and dialogs.

## Immediate Opportunities
1. **Hook Coverage**: consider adding a secondary gesture (e.g., right-click or another hotkey) to trigger classification outside the app window.
2. **UX Feedback**: indicate when no capture is available, show confidence thresholds, and expose `min_confidence` as a setting.
3. **Stability**: guard Win32 handles in `ui.ixx` (preview/status HWNDs, hooks) to reduce leak risk on failures.
4. **Testing Hooks**: add minimal unit coverage for `PixelRecognizer` (cosine similarity, persistence round-trip) using a cross-platform test target.
5. **Release Packaging**: script a release build that bundles the executable and default model for distribution.

## Next Steps Checklist
- [ ] Decide on module build strategy for non-MSVC toolchains or document MSVC-only support explicitly.
- [x] Implement startup load of `pixelai_examples.bin` with status messaging, placeholder creation when missing, and fallbacks for load failures.
- [ ] Extract preview drawing into a smaller helper to simplify `ui.ixx` and enable potential resizing modes.
- [ ] Add a configuration surface (ini file or registry) to persist confidence thresholds and capture radius.
- [ ] Document manual QA scenarios (multi-monitor capture, classification with no examples, repeated learning).
