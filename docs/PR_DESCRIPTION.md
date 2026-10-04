# Improve mouse flight controls toward Project Wingman-style handling

This contribution tunes AC8 mouse flight controls toward **Project Wingman (PW)-style mouse handling**. The controller and parameters were primarily tuned through **F-14D** single-player testing. Defaults and the optional F-14D preset are starting points; players can adjust them for other aircraft, flying styles and personal preferences.

Mouse-only turns could over-roll, climb before a level turn, or lose pitch authority when tracking diagonal targets. Horizon-relative bank guidance and coordinated pitch/yaw allocation improve those cases, while rate feedback, predictive roll gating and reverse correction help the aircraft settle toward the mouse target.

Changes include:

- Effective normal bank limits, upright recovery and intentional steep-dive inversion with hysteresis.
- Shared bank/turn planning, diagonal pitch authority and observation of the player's throttle/brake chord for high-G response.
- Rear-heading direction hold, additional ordinary dive pitch headroom, optional high-G yaw headroom and pose-discontinuity protection.
- Configurable keyboard bindings and response settings, with native/Lua bridge version 35.
- Reproducible Windows x64 build tools, controller/runtime/Lua tests and an optional F-14D preset.

Default settings retain the original pw.5 values: bank limit 85, roll scale 1.25, lookahead 0.10, yaw boost and diagnostics off. The optional preset uses 89, 1.35, 0.12, yaw boost and diagnostics on. Neither is presented as a universal optimum.

This is a cumulative port onto upstream `75bc229`. The coexist installer behavior is retained, including preservation of existing INI files, shared loaders and other mods. Its displayed version is updated. Existing INI values such as `max_bank=65` continue to apply; the documentation explains how to select or merge new settings.

Validation on Windows x64 with MSVC 14.41.34120 / SDK 10.0.22621.0:

- 21 native groups: 20 controller/runtime regression groups plus packaged default/preset loading through the actual INI reader.
- 3 Python/Lua tests: script syntax, remapped callbacks/focus and rejection of old native bridges.
- The rebuilt Payload DLL matches the previously tested pw.5 binary byte for byte (SHA-256 `36d4e1f246c5deb2aedbe8267fedd27cb556fb1a59d0574c4ecde87548781100`).
- Installer fixture results and reproduction commands are recorded in [PW5_VALIDATION.md](PW5_VALIDATION.md).

The synthetic dynamic results use the original release test settings; the more aggressive personal preset has configuration-loading checks and F-14D user feedback, not a full matched-speed performance study. The port was not installed into a real game during validation. It does not change aircraft physics, synthesize high-G inputs or remove the game's native high-G trigger delay. Maximum aircraft maneuverability remains unverified.

Original changes follow the upstream CC0 1.0 dedication, and third-party MIT/BSD notices are retained. Single-player only; no additional online-menu or network blocking is introduced. The local `pw.5` label identifies this contribution; the upstream release number remains the maintainer's decision.
