# Versioning and Release Notes

Use this guide to keep releases repeatable and documented. Always update `VERSION`, documentation, and artifacts together so the released binary matches what users read.

## When to bump `VERSION`
- **Major**: breaking changes to file formats (for example, `pixelai_examples.bin` or `pixelai.ini` incompatibilities) or removal of core UI flows.
- **Minor**: new user-facing capabilities, settings, or workflows that keep backward compatibility but change behavior.
- **Patch**: bug fixes or performance improvements that users can observe without altering workflows.
- For non-user-visible refactors, leave `VERSION` unchanged; bump it as soon as an observable change lands.

## Recording documentation updates
- Update `README.md` when commands, shortcuts, settings, or runtime expectations change.
- Refresh `docs/CONTRIBUTOR_QUICKSTART.md` if runtime flows, file locations, or build steps move.
- Add follow-up ideas and deferred tasks to `docs/ROADMAP.md` so future releases capture them.
- Mention the `VERSION` change and notable user-visible updates in your PR summary.

## Verifying artifacts before tagging a release
1. Bump `VERSION` and commit the change along with documentation updates.
2. Open the solution in Visual Studio, set the configuration to **Release**, and perform **Clean** then **Rebuild** for `VisualRecognition` to produce fresh binaries (typically under `x64/Release/`).
3. Confirm the rebuilt executable reflects the new version (file properties) and launches successfully with manual smoke tests for capture, classify, and learn flows.
4. Ensure any bundled defaults (for example, `pixelai.ini` and example model binaries) are present beside the executable and match the updated behaviors described in the README.
5. Only tag the release after the above checks pass, and include the new `VERSION` and verification notes in the tag message or release description.
