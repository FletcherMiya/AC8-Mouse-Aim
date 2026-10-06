# Changelog

## 2026-10-07 — 0.2.30-pw.11 contribution

- Improve roll stability, keeping small corrections gentle and large turns responsive.
- Allow fuller high-G pitch input; final leveling can have a small offset before further correction.
- Add optional mouse-direction target priority for all weapons while retaining native switching timing, eligibility, lock envelopes and multi-target behavior.
- Port the tested pw.11 sources and matching bridge-41 DLL, merge upstream `052cd6a`, and preserve coexist installation and existing INI files. Parameters remain adjustable; target priority defaults off, with an enabled preset.
- Pass native/runtime/closed-loop checks, 3 Lua tests and 3 installer scenarios. See [validation and limits](docs/PW11_VALIDATION.md).

## 2026-10-04 — 0.2.30-pw.5 contribution

- Port cumulative PW-style mouse flight guidance onto upstream 0.2.30/coexist installer r1: horizon-relative bank limits, coordinated level/diagonal turns, rate feedback, predictive gating and stronger reverse correction.
- Add configurable flight/function keys and observed high-G chord response, rear-heading hold, ordinary dive pitch headroom, optional high-G yaw headroom and pose-discontinuity protection.
- Document that tuning primarily used the F-14D and can be adjusted for other aircraft and preferences. Keep original pw.5 defaults and include the optional F-14D user preset (89/1.35/0.12, yaw boost and diagnostics enabled).
- Add portable build/test tools and pinned header/test dependencies with retained third-party notices. Rebuild the matching bridge-35 Payload DLL; its SHA-256 matches the tested pw.5 binary.
- Validate 21 native groups, 3 Lua tests and fresh/coexist/unknown-runtime installer fixtures. Preserve installer coexist behavior, update its version display, and document existing-INI preservation. Real-game validation and maximum maneuverability are separate from these offline checks.
