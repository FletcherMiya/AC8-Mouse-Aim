# Changelog

## 2026-10-04 — 0.2.30-pw.5 contribution

- Port cumulative PW-style mouse flight guidance onto upstream 0.2.30/coexist installer r1: horizon-relative bank limits, coordinated level/diagonal turns, rate feedback, predictive gating and stronger reverse correction.
- Add configurable flight/function keys and observed high-G chord response, rear-heading hold, ordinary dive pitch headroom, optional high-G yaw headroom and pose-discontinuity protection.
- Document that tuning primarily used the F-14D and can be adjusted for other aircraft and preferences. Keep original pw.5 defaults and include the optional F-14D user preset (89/1.35/0.12, yaw boost and diagnostics enabled).
- Add portable build/test tools and pinned header/test dependencies with retained third-party notices. Rebuild the matching bridge-35 Payload DLL; its SHA-256 matches the tested pw.5 binary.
- Validate 21 native groups, 3 Lua tests and fresh/coexist/unknown-runtime installer fixtures. Preserve installer coexist behavior, update its version display, and document existing-INI preservation. Real-game validation and maximum maneuverability are separate from these offline checks.
