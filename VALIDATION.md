# Validation and limits

NativeBridge 0.3.1 is an experimental local build orchestrator for Apple-silicon Macs. It does not include Nintendo games, keys, firmware or upstream repository checkouts.

## Checks completed

- Swift application compilation and ad-hoc code-signature verification.
- Local Mac app launch, first-run checklist, cancellation of the download approval dialog, and signing-team/device discovery.
- 31 automated tests, including patch integration against upstream source snapshots.
- UIKit settings code compilation and linking against iPhoneOS for all three backend configurations and the unsupported BOTW resolution configuration.

## Not yet validated

- A complete retail-game build.
- Installation of all missing dependencies from a clean Mac.
- Apple provisioning and installation of a generated game on an iPhone.
- Gameplay, the injected settings UI, control-opacity interaction, or frame-rate/resolution behavior on an actual iPhone.

## Reproduce local tests

```sh
/usr/bin/python3 -m unittest discover -s Source -p 'test_*.py' -v
./build.sh
codesign --verify --deep NativeBridge.app
```

Five source-patch integration tests plus the patch-upgrade test require upstream fixture snapshots through `NATIVEBRIDGE_UPSTREAM_FIXTURES`; they are skipped without that directory. The remaining tests use synthetic inputs. See `Source/test_ios_settings.py` for the required fixture filenames and source paths. Upstream snapshots and game content are intentionally not included.

The build script creates an ad-hoc-signed app and ZIP. Neither Developer ID signing nor Apple notarization has been performed. GitHub source publication alone does not change this validation status.
