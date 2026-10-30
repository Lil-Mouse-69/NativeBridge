# Validation and limits

NativeBridge 0.5.0 is an experimental local build orchestrator for Apple-silicon Macs. No games, keys, firmware or backend checkouts are included.

## Recovery verification

The recovered GitHub sources compiled successfully into the 0.4.0 wrapper. The rebuilt archive passed strict code-signature verification and bundled-source comparisons. The current test run discovered 44 tests: 38 passed and six upstream-fixture tests skipped because the fixture directory was unavailable.

## Completed during development

- 44 automated tests passed with upstream fixtures available, including patch integration and synthetic native C tests for BOTW resolution.
- UIKit settings code compiled and linked for nxnative, BOTW, TOTK 1.4.3 and Mush8 configurations.
- A full BOTW 1.9.0 iPhone build completed with the updated settings patches and signing.
- An earlier requirements-fix build installed on the test iPhone. Installation of the final settings build was started, but its completion and launch results could not be recovered after the local working folder was removed.

## Outstanding checks

- Confirm final settings build installation and launch on the iPhone.
- Visually check the bottom-center gear, touch interaction and both rendering modes on device.
- Physical wired/Bluetooth controller hide/show; no controller was connected for testing.
- Sustained gameplay, measured GPU rendering dimensions and frame rates.
- Dependency installation on a clean Mac.

Graphics preference changes require closing and reopening the game. Selecting 60 FPS does not guarantee the device sustains it.

## Reproduce

```sh
python3 -m unittest discover -s Source -p 'test_*.py' -q
bash build.sh
codesign --verify --deep --strict NativeBridge.app
```

Upstream integration tests require NATIVEBRIDGE_UPSTREAM_FIXTURES and skip when those snapshots are absent. See Source/test_ios_settings.py for fixture paths. Tests without fixtures do not replace full integration coverage.

The Mac wrapper is ad-hoc signed, without Developer ID signing or Apple notarization.

## 0.4.1 SDK discovery

All 37 engine tests passed. The rebuilt, signature-verified app detected the installed Microsoft .NET SDK 10.0.403 with PATH restricted to /usr/bin:/bin. Installer locations are now searched independently of PATH, including Homebrew versioned installations and architecture-specific locations. The previously reported missing-SDK symptom did not reproduce with 0.4.0 on this Mac.

## 0.5.0 validation

- 52 tests passed with actual upstream fixtures, including all three adapters, upgrade protection, IPA embedding, original-file preservation, import validation and bounded cleanup.
- The actual Objective-C save import function was compiled and executed against temporary folders on macOS: Zelda account routing worked and a second launch preserved newer save progress.
- iPhone settings compiled for nxnative, BOTW, TOTK 1.4.3 and Mush8.
- Current upstream Zelda export now includes a render hook by default; the adapter accepts both that form and the earlier version-scoped export form.
- No full game build or new physical iPhone test was performed for 0.5.0. Screen-fit behavior, custom render dimensions, overlay appearance and save usability still require gameplay validation.
- Cheat import is archive-only. Universal nxnative screen fitting and Mush8 performance-overlay controls are not implemented.

## 0.5.1 validation

52 tests pass, including the native save function replacing an existing save only with explicit import permission, preserving a backup and consuming the pending seed once. Four iPhone header configurations compile. The Files picker and live aspect change still require physical iPhone testing. Cheat execution remains unsupported.

## 0.6.0 validation

53 tests passed with upstream fixtures. Native Foundation execution tests verify static data-cheat parsing, data writes, disable behavior, text/out-of-bounds rejection and 64-bit values; save tests verify backups and one-time replacement. The iPhone header compiles/links for nxnative, TOTK 1.0, TOTK 1.2.1, TOTK 1.4.3, BOTW and Mush8 using runtime stubs. Upstream adapter fixtures cover registration hooks and build-ID metadata generation. Full game rebuilds, picker interaction and cheat/gameplay effects are not device-verified.

TOTK 1.0 full-screen is not implemented: upstream has no camera-aspect sites for that version. Supported runtime cheats are type-0 main-data writes only; all other VM operations and executable/read-only writes are rejected. No universal cheat-compatibility claim is made.
