# Guide shortcut fix — development build 01.36

L1 + R1 + Options previously held Guide for as long as all three buttons were
down. The moment one button was released, remaining buttons were forwarded again.
Steam's Guide layouts include desktop actions such as right-click and Alt+Tab,
so the boundary between these states could trigger unwanted commands.

The new filter sends a neutral controller sample first, then one 80 ms Guide
pulse, then another neutral sample. Buttons, triggers and sticks are neutralized
during these boundaries and the pulse. L1/R1/Options remain suppressed until all
three are released, even if the user holds them or presses them again early.
Other controls resume after the pulse. Touchpad cancels Guide and the local
Options + Touchpad exit check still uses the original physical button state.

The filter uses the existing polling thread and controller transport. There is
no sleeping, extra thread, Steam configuration change or keyboard/mouse injection.
Its state resets with the existing input session reset. Individual buttons before
the full chord is detected retain their normal behavior; the client cannot undo
an action already delivered to a game before the chord was complete.

## Validation

`tests/test_guide_chord.c` checks the neutral boundary, the deadline, a long hold,
six staggered release orders, quick taps, rearming, other buttons and touchpad
precedence. Run it through `tests/run_host_tests.sh` in the Linux build mirror.

On the PS4, try Big Picture with simultaneous and staggered presses, a five-second
hold, all release orders, and two successive activations after full release. Also
test while moving sticks or holding a trigger. Expect one Guide action, no extra
held-Guide menu, no right-click and no Alt+Tab from the shortcut. Verify normal
L1/R1/Options in a game and Options + Touchpad exit afterwards.

80 ms is a starting pulse duration, not a hardware measurement. Real PS4/Steam
latency calibration is still required. On 2026-10-20 the user confirmed that the
shortcut works on their PS4 running 01.36. This is a user-observed functional
pass, not an instrumented check of every release order or the 80 ms timing.
