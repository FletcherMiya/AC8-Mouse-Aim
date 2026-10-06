# Improve PW-style roll stability, high-G handling and target selection

Mouse flight controls tuned toward **Project Wingman (PW)-style handling**. Parameters can be adjusted for different aircraft and personal preferences.

- Improve roll stability, especially during small corrections.
- Allow fuller pitch input during high-G turns. Final leveling may have a small offset before further correction.
- Add optional mouse-direction target priority for all weapons, preserving the original switching timing, weapon eligibility, lock range/time and multi-target behavior.

Updated to **pw.11 / bridge 41**, with current upstream `052cd6a` merged. Existing INI files and coexist installation are preserved. Set `mouse_target_priority=1` to enable target priority (default: off). [Settings](PW11.md).

Native/runtime/closed-loop tests, 3 Lua tests and 3 installer scenarios pass; the DLL matches the user-tested release. MSL, QAAM, LACM, 6AAM and LAGM passed simple user testing; logs show 41/41 target suggestions accepted. [Validation and limits](PW11_VALIDATION.md).

Single-player only. Aircraft physics and the game's native high-G trigger delay are unchanged. CC0 and third-party notices are retained.
