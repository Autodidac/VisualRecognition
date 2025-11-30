# Roadmap and Next Steps

## Current State
- Win32 UI (`src/ui.ixx`) captures a patch around the cursor and displays it in a preview area.
- Global mouse hook triggers capture (left click) and classification (right click) even when the app is unfocused.
- The `PixelRecognizer` (`src/pixelai.ixx`) stores labeled patches, performs cosine-similarity matching, and saves/loads models to `pixelai_examples.bin`.
- Learning flow prompts for a label, trains in-memory, and attempts to persist the model immediately.

## Immediate Opportunities
1. **Model Lifecycle**: load `pixelai_examples.bin` on startup and surface errors in the status bar; prompt before overwriting.
2. **UX Feedback**: indicate when no capture is available, show confidence thresholds, and expose `min_confidence` as a setting.
3. **Stability**: guard Win32 handles in `ui.ixx` (preview/status HWNDs, hooks) to reduce leak risk on failures.
4. **Testing Hooks**: add minimal unit coverage for `PixelRecognizer` (cosine similarity, persistence round-trip) using a cross-platform test target.
5. **Release Packaging**: script a release build that bundles the executable and default model for distribution.

## Next Steps Checklist
- [ ] Decide on module build strategy for non-MSVC toolchains or document MSVC-only support explicitly.
- [ ] Implement startup load of `pixelai_examples.bin` with status messaging and fallbacks for missing files.
- [ ] Extract preview drawing into a smaller helper to simplify `ui.ixx` and enable potential resizing modes.
- [ ] Add a configuration surface (ini file or registry) to persist confidence thresholds and capture radius.
- [ ] Document manual QA scenarios (multi-monitor capture, classification with no examples, repeated learning).
