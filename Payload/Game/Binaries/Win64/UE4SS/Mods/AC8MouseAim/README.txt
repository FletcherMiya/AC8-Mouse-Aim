AC8 Mouse Aim 0.2.33 (response-matched braking preview)

Stopping prediction and stick inversion share the same estimated axis response.
Arrival-curve acceleration feed-forward and bounded rollout coupling added.
Estimates come from one flight trace, not universal aircraft identification.

All axes now anticipate stopping distance and smoothly remove feed-forward
before actively braking. Attitude timing uses a high-resolution clock.
F11 toggles a 20-second, up-to-20Hz asynchronous flight trace in Logs.
Trace commands are automatic proposals, not final keyboard-overridden inputs.

New controller default: coordinated bank/pull and bounded predictive roll-out.
No near-target roll speed reduction; camera and HUD are unchanged.
For the accepted legacy controller, set controller_mode=0 under [control] in
config.ini and press F10. Set 1 to restore the new controller (also the default
when the key is absent). Flight feel still needs in-game confirmation.

Offline single-player only. The mod reads aircraft attitude and replaces only
the three player-control axes. It does not change aircraft physics, thrust,
lift, drag, weapon data, damage, or save files.

Controls
  Mouse      Move the world-space target marker; the aircraft follows it.
  F8         Enable/disable the instructor.
  F9         Recenter the target on the aircraft nose.
  F10        Reload config.ini and recenter.
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

Logs are written under AC8MouseAim\Logs. Game updates may require a rebuilt
input signature. Remove or disable the mod if AC8 crashes after an update.
