AC8 Mouse Aim 0.2.30-pw.5 contribution build (native/Lua bridge 35)

This flight-control version is tuned toward Project Wingman (PW)-style mouse
handling, primarily through in-game testing with the F-14D. Defaults and the
optional preset are starting points: adjust the parameters to suit your
aircraft, flying style and personal preferences.

Offline single-player only. The mod reads aircraft attitude and replaces only
the three player-control axes. It does not change aircraft physics, thrust,
lift, drag, weapon data, damage, or save files.

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

Default tuning: max_bank=85, roll_rate_scale=1.25, roll_lookahead=0.10,
high_g_yaw_boost=0, diagnostics=0. presets/f14d-pw5.ini is an optional user
preset (89, 1.35, 0.12, yaw boost and diagnostics enabled). Back up config.ini
before merging preset values; keep your own sensitivity and key bindings.
The coexist installer preserves existing config.ini, including max_bank=65
from older releases. Missing new settings use built-in defaults.

Never mix this Lua entrypoint with an upstream/older native DLL. For detailed
changes, configuration ranges and validation limits see docs/PW5.md in the
source repository. This is a contribution build, not an upstream release.

Logs are written under AC8MouseAim\Logs. Game updates may require a rebuilt
input signature. Remove or disable the mod if AC8 crashes after an update.
