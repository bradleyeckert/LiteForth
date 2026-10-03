# Multitasking

The VM has enough instructions to implement a cooperative, round-robin
multitasker. The words are in `scripts/go.f`, and `scripts/regression.f`
tests them.

There are at least two tasks, terminal and app, in a round-robin queue.
The terminal task is `begin pause break again`.

The focus is on a lightweight `pause`, especially past sleeping tasks.

## VM support

| Word | Instruction | Stack | Action |
|---|---|---|---|
| `task[` | `sys>` 0 | `( -- r:d )` | Pushes T onto the data stack and R onto the return stack, then T = `rp << 16 \| sp` |
| `]task` | `sys` 0 | `( r:d -- r:d )` | Sets `sp` and `rp` from T. T stays: the instruction doesn't pop |
| `n _user + ,inst` | `user` n | `( -- )` | A = U + n |
| `u!` | micro-op | `( a -- )` | U = a |

`]task` doesn't pop because changing the stack pointers and popping a stack
in the same instruction doesn't map well to hardware (Verilog).

## User areas

Register U points to the running task's user area. A task's user area has
three cells:

| Cell | Name | Contents |
|---|---|---|
| U+0 | NEXT | The user area of the next task in the ring |
| U+1 | ACTION | The xt that `pause` jumps to: `awake` or `asleep` |
| U+2 | R:D | The task's saved `rp:sp` while it isn't running |

A task doesn't need a cell for where to resume: that address is on its
return stack.

The data and return stacks are shared rings of `STACK_CAPACITY` cells. Each
task gets a window of each, so a task's state is just its `rp:sp`.

## pause

```forth
:noname     ( next a -- )   drop u! ; constant asleep
: pause     ( -- next a )   [ _user ,inst ] @a+ @a+ >r a ;
: stop      ( -- )          asleep [ _user 1 + ,inst ] !a pause ;

:noname     ( next a -- )
    a! u! task[ dup !a
    [ _user 2 + ,inst ] @a ]task r> drop drop
; constant awake
```

- **`pause`** fetches NEXT and ACTION of the running task, and pushes ACTION
  onto the return stack. `a ;` compiles to one instruction whose return runs
  before its micro-ops, so `pause` jumps to ACTION. The data stack holds
  NEXT and `a`, the address of R:D (A after two `@a+`). R is the return
  address into the task that called `pause`.
- **`awake`** switches tasks:
  - `a! u!`: A = the R:D cell of the task that paused, U = NEXT.
  - `task[ dup !a`: pushes the paused task's T and R (its return address)
    onto its own stacks and stores its `rp:sp` in its R:D.
  - `[ _user 2 + ,inst ] @a ]task`: fetches the next task's saved `rp:sp` and
    switches the stack pointers to it.
  - `r> drop drop`: `r>` pushes the old R and pops the next task's saved
    return address into R. The two `drop`s leave the next task's own T on
    top.
  - `;` returns into the next task, just after its `pause`.
- **`asleep`** sets U to NEXT and returns to the code that called `pause`,
  without switching stacks.
- **`stop`** sets the running task's ACTION to `asleep`, then calls `pause`.

## Starting

```forth
_udata here 3 allot constant operator

: multitask ( -- )
    operator  dup u! dup a! !a+  awake !a+  task[ !a+ r> drop
;
```

- **`multitask`** makes a ring of one task, the terminal, whose user area is
  `operator`. NEXT is `operator` itself, ACTION is `awake`, and R:D is the
  current `rp:sp`.
- **The demo app** (the `:noname` that `go.f` jumps to from cell 0) runs
  `init-idata multitask hi`, then loops `begin demo-step pause break again`.
  `cold` starts it, and `counter` rises more slowly than without `pause`.

`task` and `activate`, to create tasks and add them to the ring, will come
back later.

## Notes

- **Start the ring first.** Don't `pause` before `multitask`. With U = 0,
  `pause` fetches NEXT and ACTION from cells 0 and 1, and jumps to cell 1.
- **One task so far.** So far only a ring of one task has been run, so
  `awake` switches the terminal to itself. `regression.f` checks that the
  terminal's stacks and R survive `pause`, and what `stop` does in that ring.
