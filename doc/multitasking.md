# Multitasking

The VM has enough instructions to implement a cooperative, round-robin
multitasker. The words are in `scripts/go.f`, and `scripts/regression.f`
tests them.

There are at least two tasks, terminal and app, in a round-robin queue.
The terminal task is `begin pause break again`.

## User areas

Register U points to the running task's user area. The `user` instruction
(`n _user + ,inst`) sets A to U+n in one instruction. A task's user area
starts with four cells:

| Cell | Name | Contents |
|---|---|---|
| U+0 | STATUS | The xt `pause` jumps to: `awake`, `asleep` or `[start]` |
| U+1 | FOLLOWER | The user area of the next task in the ring |
| U+2 | TASKNOW | The task's saved `rp:sp` while it isn't running |
| U+3 | ENTRY | Where a task that hasn't run yet starts |

The task's own user variables follow.

The data and return stacks are shared rings of `STACK_CAPACITY` cells. Each
task gets a window of each, so a task's state is just its `rp:sp`.

## pause

```forth
: asleep  [ 1 _user + ,inst ] @a u!  [ 0 _user + ,inst ] @a >r ;
: pause   task[ [ 2 _user + ,inst ] !a  asleep ;
: awake   [ 2 _user + ,inst ] @a ]task  r> drop drop ;
```

- **`pause`** saves the running task. `task[` pushes T onto the data stack
  and R (the return address into the task) onto the return stack, and
  leaves `rp:sp`, which is stored in TASKNOW. Then `asleep` (a jump, since
  it's a tail call) moves on.
- **`asleep`** sets U to FOLLOWER and jumps to that task's STATUS: `>r ;`
  is a jump through the return stack. A sleeping task's STATUS is
  `asleep`, so it's skipped.
- **`awake`** resumes a task. `]task` switches `sp` and `rp` to its saved
  values. It also sets R from B and loads T from the old stack, so neither
  is the task's yet. `r>` brings back its R, the return address saved by
  `task[`, and the two `drop`s bring back its T. `;` then returns into the
  task just after its `pause`.

While control passes through the ring, the code runs on the stacks of the
task that paused. It only writes above that task's saved `sp` and `rp`, and
the only slot it rewrites (the one just above `sp`) gets the same value
that's saved there.

Note that a `;` folded into a group of micro-ops returns *before* the group
runs. The compiler keeps `;` separate after `>r` and `r>`, which `pause`,
`asleep` and `awake` depend on.

## Starting tasks

```forth
10 16 16 task t1          ( user_cells data_stack return_stack <name> -- )
: launch  t1 activate  begin  ( your code )  pause again ;
launch
```

- **`task`** reserves the task's stack windows from `stackused`, which
  holds the `rp:sp` cells used so far (the terminal keeps the first `0x30`
  of each). If a stack window would run past the end, it yeets -118. The
  task's header is in IDATA: cell 0 is the stack base `rp:sp`, and cell 1
  is the address of its user area. The user area is in UDATA and is
  `user_cells` + 4 cells long.
- **`activate`** links the task into the ring after the running one. It
  sets the task's STATUS to `[start]`, and its ENTRY to the rest of the word
  that called `activate`. That word then returns to its own caller, and the
  task starts the first time `pause` reaches it.
- **`[start]`** sets STATUS to `awake`, switches to the task's empty stacks
  with `]task` (with R = B = ENTRY), and `;` jumps to ENTRY.
- **`multi`** starts the ring with just the terminal, whose user area is
  `operator`. `activate` calls it if U is 0.

`up` returns the current user area. A task sleeps when its STATUS is
`asleep` and wakes when it's `awake`:
`' asleep t1 1 + @ !` puts `t1` to sleep.

## Caveats

- **Start the ring first.** Don't `pause` until there's a ring (`multi`, or
  the first `activate`). With U = 0, `pause` jumps through cell 0.
- **Errors inside a task.** If a word the terminal runs errors while
  another task is running, QUIT resets the stack pointers but U still points
  to that task. The next `pause` would then save the terminal into the wrong
  user area.
- **Not yet in step mode.** The app that `cold` runs in step mode isn't in
  the ring yet. The tests run the ring from terminal words, which `pause`.
