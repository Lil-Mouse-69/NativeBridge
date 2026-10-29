# Validation and limits

NativeBridge 0.4.0 is an experimental local build orchestrator for Apple-silicon Macs. No games, keys, firmware or backend checkouts are included.

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
