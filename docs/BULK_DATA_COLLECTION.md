# Bulk Data Collection Guide

This guide outlines a recommended flow for gathering and organizing cursor-centered pixel patches before training with VisualRecognition.

## Capturing patches
- Launch the VisualRecognition executable on Windows.
- Use either UI buttons or global hooks to capture patches centered on the cursor:
  - **Left click** (global hook): capture a patch anywhere, even if the app is unfocused.
  - **Capture** button (UI): capture while the window is focused.
- Verify the preview to confirm the patch includes the desired context (padding near edges is handled automatically).
- Use **Delete Capture** to remove accidental or redundant entries; the matching `.bin` file in the history folder is removed when present.

## Organizing stored captures
- Captures are kept in memory during the session; plan short capture batches to avoid losing context.
- After each capture burst, use clear, descriptive labels that group related scenarios (e.g., `button_active`, `button_hover`).
- Keep written notes on what each batch represents (screen, app state, lighting) to make later labeling sessions faster.

## Labeling in a later session
- Reopen the application and load your prior binary dataset if present in the working directory:
  - Choose **Load Model** (or equivalent UI control) to import `pixelai_examples.bin`.
- Review unlabeled or newly captured patches and assign labels that match your naming scheme.
- Re-classify with **F5** as needed to verify the model responds correctly after each label addition.

## Saving to `pixelai_examples.bin`
- After labeling a batch, choose **Learn Label** to assign a label and persist the updated dataset.
- Confirm the file `pixelai_examples.bin` appears or is updated in the working directory.
- Repeat: capture → label → save. Small, frequent saves reduce risk of losing work.

## Recommended workflow for bulk collection and training
1. Plan a short capture goal (e.g., 20 patches for a specific UI state).
2. Use the global **Left click** hook for fast, unfocused captures; rely on the **Capture** button when working inside the app.
3. Organize patches by consistent naming (`feature_state`), recording context notes externally.
4. In subsequent sessions, reload `pixelai_examples.bin`, finish labeling any remaining patches, and test with **F5** classification.
5. Save after every labeling round to keep the binary dataset current.
