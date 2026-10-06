# pw.11 contribution validation

Validated on 2026-10-07, Windows x64, MSVC 14.41.34120 / SDK 10.0.22621.0, Python 3.13 and pinned Lupa 2.8.

## Source and integration

- Keep `pw5-flight-control` so the existing upstream PR #4 receives this update.
- Merge upstream `052cd6a`, including its uninstall-command documentation fix. Resolve the overlapping experimental flight controllers in favor of the tested pw.11 implementation, rather than combining different control algorithms.
- Runtime sources, Lua and default INI match local pw.11 (`ce1de6331e6a6112e88b30042b02848c260cbb5d`); user flight evidence is recorded through project commit `4487a98`.
- Preserve the coexist installer's behavior, shared runtime files, original licenses and third-party notices. Update only its version display. The new preset enables optional target priority; the packaged default leaves it off.

## Reproduction and results

```powershell
.\.venv\Scripts\python.exe tools/build_native.py --update-payload
.\.venv\Scripts\python.exe tools/check_package.py
```

Use `--vs-dev-cmd 'path/to/Common7/Tools/VsDevCmd.bat'` when Visual Studio is not discoverable. Build dependencies and setup commands are in the README; no adjacent source checkout is required.

Passed controller, angle-control, actual runtime/INI, target-selection and closed-loop rollout checks; 3 Lua tests passed. Installer fixtures passed fresh install, coexist upgrade with preserved configuration/other mods, and refusal of an unknown runtime without writes.

The rebuilt Payload DLL is byte-identical to the user-tested pw.11 release:

```text
861c5a35d08d01f22c12412d534ea6230857f9f44b9d00835660b7eec472e50f
```

`build/BUILD-MANIFEST.json` records the source revision, dirty state, source hashes and toolchain; local build logs and fixtures remain untracked. Native/Lua bridge: **41**.

## Limits

User testing on Su-57/Su-35 reported normal behavior with MSL, QAAM, LACM, 6AAM and LAGM. The reviewed log shows 41/41 suggestions accepted plus 2 normal no-candidate fallbacks, with no selection read errors, rejection or disable events. It does not label each weapon or measure actual lock envelopes and multi-target slots.

These checks do not establish maximum aircraft maneuverability, exhaustive weapon coverage or compatibility with future game builds. More permissive high-G pitch input can leave a small offset during final leveling before correction. This port validation did not install or launch the real game; the existing user-tested installation remains unchanged.
