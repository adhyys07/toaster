# Netplay: design and prerequisites

Written for: whoever implements netplay in this repo.

This is the design, the interfaces, and the work that has to come first. It is
deliberately not an implementation. Rollback netcode cannot be verified by
reading it or by compiling it — a desync shows up as two players seeing
different games ten minutes in, which no unit test in this repo would catch.
Writing it before the prerequisites below are met produces code that has to be
thrown away.

## The approach: rollback, not lockstep

Lockstep (wait for the remote input before advancing) is simpler and wrong for
this. Every frame costs a round trip, so at 60Hz and 40ms of latency the game
runs at roughly a third speed. Rollback hides latency instead: predict the
remote input, advance, and if the prediction was wrong, rewind and replay.

```
each frame:
    local  = read pad
    send(frame_number, local)

    remote = received[frame_number]        # or predicted
    if a previously predicted input has arrived and differs:
        load_state(checkpoint[last_confirmed])
        for f in last_confirmed .. current:
            set_input(confirmed[f]);  run_frame()

    set_input(local, remote)
    run_frame()
    checkpoint[current] = save_state()
```

Prediction is "the remote player is still holding what they held last frame",
which is right the large majority of frames because inputs are held across
many frames.

## Why this project can host it

Three pieces already exist and do most of the work:

| Piece | What it gives netplay |
|---|---|
| `RewindBuffer` | Proof that save/load round trips are cheap and faithful |
| `RunAhead::prepare` | Already measures the cost of a round trip plus a frame |
| `Movie` | A deterministic-replay harness — the exact property netplay needs |

`System::saveState`/`loadState` is the whole interface rollback requires. No
core changes are needed.

## Prerequisites, in order

These are the risk. Steps 1–4 are worth doing on their own merits, and until
they are done there is no point writing step 5.

### 1. Prove determinism

`Movie` (`src/frontend/movie.h`) is the harness. Record a session, replay it,
hash the framebuffer every frame, and compare. A core whose movie does not
replay identically cannot do netplay, and finding that out now costs an
afternoon instead of a month.

Add this as a test, not a one-off: determinism breaks silently when someone
adds an uninitialised member or reads the clock inside a core.

```cpp
// Fails loudly on the frame where the two timelines diverge, which is the
// only useful thing to know.
bool replayIsDeterministic(System& system, const Movie& movie,
                           std::size_t& first_bad_frame);
```

Known risks in this codebase:
- `Chip8::randomByte()` is seeded from `random_state_ = 0xC8` and is part of
  the save state, so it is fine. Verify it stays that way.
- The Libretro cores (PSP, PS2, PS1) are almost certainly *not* deterministic
  and almost certainly have large states. Treat netplay as NES/GB/CHIP-8 only
  until proven otherwise.

### 2. Budget the state

Rollback needs up to 8 save/load round trips in a single frame, on top of up
to 8 re-simulated frames. `RunAhead::prepare` already measures exactly this
cost against the frame budget — reuse it rather than writing a second
estimator. If a core cannot afford 8 rollback frames, it cannot be a netplay
core, and that is a legitimate answer.

### 3. A frame-number clock

The loop is currently paced against `SDL_GetPerformanceCounter` with a target
time. Netplay needs an integer frame counter as the authority, with both peers
agreeing on it, because every input and every checkpoint is addressed by frame
number. This is a real change to how `runGenericGame` paces itself, and it has
to land before anything references `frame_number`.

### 4. Separate input from the pad

Already done, as of the `Movie` work: `main.cpp` fills `port_input[]`, then a
replay or recording may substitute for it, then it goes to `setInput`.
Netplay substitutes at the same seam. Nothing further is needed here — this is
listed so it is not re-litigated.

### 5. Only then: transport, prediction, rollback

```cpp
// Addressed by frame number, because an input that arrives late is still
// useful: it confirms a frame that was predicted.
struct InputPacket {
    uint32_t frame = 0;
    uint32_t input = 0;
    uint32_t confirmed_through = 0;   // lets the sender drop old checkpoints
};

class Netplay {
public:
    bool host(uint16_t port, std::string& error);
    bool join(const std::string& address, uint16_t port, std::string& error);

    // Called once per frame with this side's input. Fills `remote` with the
    // received or predicted input, and reports whether a rollback is needed.
    bool advance(uint32_t local_input, uint32_t& remote_input,
                 uint32_t& rollback_to_frame);

    // How far ahead of the last confirmed frame we are. Above a threshold the
    // loop should stall a frame rather than predict further: beyond about
    // eight frames the rollbacks become visible as stutter.
    int predictionDepth() const;
};
```

UDP, not TCP: a retransmitted stale input is worthless, and TCP's
head-of-line blocking turns one lost packet into a visible freeze. Inputs are
tiny, so send a sliding window of the last ~16 frames in every packet and
loss becomes free.

## What will go wrong

- **Desyncs from uninitialised state.** A member that is not in `visitState`
  but affects rendering will not show up in rewind testing (which reloads
  within one session) and will show up in netplay (two processes, different
  histories). Add a state-size and checksum assertion to the determinism test.
- **Clock drift.** Two machines at nominally 60Hz are not at the same 60Hz.
  One side has to adapt, by dropping or duplicating a frame occasionally.
- **The palette and settings.** `setPalette` is deliberately outside the save
  state (see `systems/nes/ppu.h`), which is correct, but it means two peers
  with different palettes will *look* different while being perfectly in
  sync. Do not let that get diagnosed as a desync.

## Recommendation

Do steps 1–3. Step 1 in particular pays for itself immediately: a determinism
test is the thing that makes rewind, run-ahead, replays and netplay all
trustworthy, and this repo has three features already shipping on an
assumption nothing currently verifies.
