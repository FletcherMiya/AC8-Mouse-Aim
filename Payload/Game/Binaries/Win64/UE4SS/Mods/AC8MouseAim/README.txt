AC8 Mouse Aim 0.2.30-pw.11 contribution build (native/Lua bridge 41)

This flight-control version is tuned toward Project Wingman (PW)-style mouse
handling. Adjust the parameters to suit your aircraft, flying style and
personal preferences.

Changes: steadier roll, fuller high-G pitch input (with a possible small offset
when leveling out), and optional mouse-direction target priority that keeps
native weapon eligibility, lock envelopes and multi-target behavior.

Offline single-player only. The mod adjusts flight input, camera and optional
target priority. It does not change aircraft physics, thrust, lift, drag,
weapon data, damage, or save files.

Controls
  Mouse      Move the world-space target marker; the aircraft follows it.
  F8         Enable/disable the instructor.
  F9         Recenter the target on the aircraft nose.
  F10        Reload config.ini and recenter.
  F          Free look while keeping the flight target.
  F7         Toggle direction markers.
  W/S A/D Q/E  Manual pitch/roll/yaw takeover; pitch also releases auto roll.
  Ctrl+Space   Observe the game's throttle/brake chord for high-G response.
  RMB        Reserved for game actions; does not release camera or flight control.

The native controller refuses to activate unless the game was started by the
included offline launcher, which sets EOS_USE_ANTICHEATCLIENTNULL=1. Never use
this installation for multiplayer. Run Disable-Mod-For-Multiplayer.cmd first.

First flight
  1. Select Expert flight controls in AC8.
  2. Enter Training or a campaign mission using Launch-AC8-Mouse-Aim.cmd.
  3. If pitch or roll moves opposite the cyan marker, close the game, flip the
     matching sign in config.ini, and relaunch. If the two axes are swapped,
     exchange pitch_slot=0 and roll_slot=2.

All keyboard bindings are configurable in [keys]; restart the game after
changing keys. Flight-axis and high-G keys must match the game's bindings.
Control tuning can be reloaded with F10. High-G handling does not remove the
game's native trigger delay or change aircraft performance.

Packaged tuning: max_bank=89, roll_rate_scale=1.35, roll_lookahead=0.12,
high_g_yaw_boost=1, diagnostics=1, angle_control=1, rollout_coordination=1.
Target priority defaults off: set mouse_target_priority=1 to enable it.
presets/f14d-pw11.ini enables this option with the same flight settings.
Back up config.ini before merging values; keep your sensitivity and keys.
The coexist installer preserves existing config.ini, including max_bank=65
from older releases. Missing new settings use built-in defaults.

Never mix this Lua entrypoint with an upstream/older native DLL. For detailed
changes, configuration ranges and validation limits see docs/PW11.md in the
source repository. This is a contribution build, not an upstream release.

Logs are written under AC8MouseAim\Logs. Game updates may require a rebuilt
input signature. Remove or disable the mod if AC8 crashes after an update.
