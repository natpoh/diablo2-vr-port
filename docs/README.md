# Helping with D2R VR

D2R VR is MIT-licensed. Fork it, fix something, and send a pull request.

| File | What is in it |
|---|---|
| [KNOWN_ISSUES.md](KNOWN_ISSUES.md) | What is broken or unfinished now, what we know about each problem, and where to start in the code |
| [ROADMAP.md](ROADMAP.md) | Where the project is going, and which parts anyone can pick up |
| [WEAPON_WEIGHT.md](WEAPON_WEIGHT.md) | Weapon weight for first person: a model with tests and a Unity bench, not in the game yet |
| [DEVELOPER_NOTES.md](DEVELOPER_NOTES.md) | How the parts fit, how to debug, and what crashes the game |

## How to help

1. **Pick an issue.** The ones tagged **[repo]** can be fixed here. Those
   tagged **[BodyWalk/FlatVR]** need BodyWalk or FlatVR, which are separate
   products: talk to us first.
2. **Say you are on it** in our Discord channel
   [#diablo-2-vr](https://discord.com/channels/1481909961897279562/1556377249484251298)
   ([join the server](https://discord.gg/kVYjEdJx3e)), so two people don't do
   the same thing.
3. **Build:** see "Building" in the main README. Test in the game (offline,
   single player) with the game's log open.
4. **Send a pull request:**
   - say what you changed, and how you tested it (headset or flat, which
     view, which 3D mode);
   - one fix per pull request;
   - keep the code's style: comments that say why, ini keys documented in
     `d2r_vr.ini`;
   - a setting players should see goes into the settings app too.

**Not accepted:**

- the game's files, or anything taken from them (art, data tables);
- anything that touches online play.

Pictures (skies, ceilings) must be your own work.
