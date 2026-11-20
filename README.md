# Shooter — Console Edition (C++17)

A polished bottom-of-screen shooter. Move left/right, fire upward,
destroy descending enemies. Enemies that reach the floor cost a life;
lose all three and it's over. Score = enemies destroyed.

A deliberately dependency-free C++17 project — standard library only —
built to teach **the raw-terminal input model**, **speed tuning that
actually feels right**, and a small but real **state machine**
(menu → countdown → play → game over).

## Difficulty

**Easy.** Same tier as Flappy and Snake. The interesting lessons aren't
graphics — they're input semantics and pacing.

## Features

- **Start menu → 3-2-1-GO countdown → play → game-over menu.** Same state
  machine pattern as Flappy's polished version.
- **2-second grace period** after the countdown before the first enemy
  spawns — same courtesy Flappy gives the player before the first pipe.
- **ANSI color + Unicode glyphs.** Red enemies, yellow bullets, cyan player,
  dim blue starfield.
- **Starfield background** with slow parallax scroll — no grid, just depth.
- **Swept-style movement via accumulators.** Enemies and bullets move at
  fractional cells-per-tick, so the speeds are tunable independently of the
  tick rate. 60 Hz is a *display* rate; the simulation is smoother.
- **Discrete-press input.** Each keypress is one action. Holding a key
  relies on the OS terminal auto-repeat — no stuck held state possible.

## Input model (the lesson)

This is the single most important thing the project teaches. In a raw
terminal:

- `read(STDIN_FILENO, ...)` gives you **keystrokes**, not **key events**.
- **There is no key-up event.** You cannot ask "is the player currently
  holding A?" — that state doesn't exist.
- The correct model is: **each keypress is one discrete action**. Tap A →
  the player moves one cell. Hold A → the terminal's own auto-repeat
  sends repeated `a` bytes → the player moves smoothly. Release → no more
  bytes → movement stops.

The baseline version of this project (earlier in development) tried to
track "A is held" with a boolean, and the player **got stuck moving in
one direction forever**. That's the bug this input model fixes. It's a
genuinely important lesson: **model the medium you actually have, not
the one you wish you had.**

## Controls

| Key | Action |
|-----|--------|
| `1` | Start (from the menu) |
| `A` / `D` | Move left / right (tap = one cell; hold = auto-repeat) |
| `SPACE` | Fire (cooldown: 4 shots/sec) |
| `Q` | Menu (while playing) / Quit (menu, game over) |
| `R` | Restart (on game over) |
| `M` | Return to menu (on game over) |

## Build & Run

### Quick build (g++)

```bash
g++ -std=c++17 -O2 -pthread main.cpp -o shooter
./shooter
```

### With CMake

```bash
cmake -B build
cmake --build build
./shooter
```

## How it works

- **Fixed-timestep loop at 60 Hz.** Real time is accumulated and consumed
  in whole `1/60 s` ticks. The accumulator is capped so a stalled terminal
  cannot fast-forward the simulation.
- **State machine.** `Menu → Countdown → Playing → GameOver`, same shape
  as the polished Flappy. Countdown is 3 seconds (60 ticks per digit).
- **Fractional movement accumulators.** Enemies move 2.2 cells/sec and
  bullets move 30 cells/sec; each has a `stepAccum` that adds
  `speed * dt` per tick and moves the whole cells out. This lets the
  speeds be tuned by feel, not constrained to integer cells/tick.
- **Speed tuning.** The *baseline* version had enemies descending at
  6 cells/sec on a 20-row field (~3s to reach the floor) while bullets
  moved at 60 cells/sec (~0.25s to cross). The asymmetry made the game
  feel unfair and the bullets invisible. The polished speeds (2.2/sec
  enemies, 30/sec bullets, 4/sec fire rate, 1 enemy every 2.2 sec) give
  the player real reaction time. **The tuning is the design.**
- **Collision.** Bullets and enemies are checked with nested loops —
  naive `O(bullets × enemies)`. Fine at the counts this game produces.
  Tunnelling is possible in theory (both move past each other in one
  tick) but with bullets at 30 cells/sec and enemies at 2.2, the relative
  speed is high enough that it's not observable in practice. Noted as a
  polish item if it ever becomes a problem.

## What this project teaches

- **The raw-terminal input model** — no key-up events, count keypresses,
  consume them per tick. This is the fix for the classic "stuck held key"
  bug.
- **Fractional speed accumulators** — the way to make a game feel right
  when the render rate is locked and the speeds you want aren't integer
  multiples.
- **Speed tuning is design, not code.** The most important changes between
  the baseline and the polished version weren't new features — they were
  numbers. Enemy speed, spawn rate, fire rate, grace period. Pacing *is*
  the game.
- **`erase-remove_if` for lifetime management** — used in four different
  places (bullets off top, bullets hit, enemies dead, enemies off bottom).
  Doing it by hand in a loop is UB; the idiom is the correct pattern.
- **A state machine in the main loop** — countdown → playing → over, each
  with its own input handling. Generalizes to any game.

## Requirements

- **Compiler:** any C++17 compiler (`g++`, `clang++`, MSVC)
- **Platform:** Linux, macOS, or Windows
- **Terminal:** ANSI color + UTF-8 support (default in modern terminals)
- **Dependencies:** standard library only
- **Threads:** `-pthread` (GCC/Clang) or `Threads::Threads` (CMake)

## Known limitations

- **Bullet tunnelling is theoretically possible but not observed in
  practice** at the shipped speeds. If it ever becomes visible, the fix
  is swept collision: check whether the bullet's `[prevY, newY]` segment
  crosses the enemy's row, not just whether it lands exactly on it.
- **Auto-repeat rate is per-terminal.** Holding A/D feels smooth on
  default Fedora terminals (~25–30 Hz after a short initial delay); some
  terminals can be tuned. This is the OS, not the code.
- **Terminal-agnostic rendering.** The game uses cursor-home
  (`\033[H`) plus a single `cout` per frame. On some very slow terminals
  this can still flicker; a true double-buffered renderer is a polish
  item.

## Roadmap

- [ ] Enemy horizontal drift
- [ ] Enemy shooting back
- [ ] Multiple enemy types (fast/fragile, slow/tanky)
- [ ] Power-ups (spread shot, shield)
- [ ] High score persisted to disk
- [ ] Double-buffered renderer
- [ ] Swept collision (if tunnelling ever becomes visible)

## Notes

- The binary is named `shooter`. Both the `g++` command and the CMake
  build produce it at the project root.
- The header brief in `main.cpp` explains the speed tuning in detail —
  worth reading alongside this README.

## License

MIT.
