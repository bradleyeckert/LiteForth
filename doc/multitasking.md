# Multitasking

The VM has enouugh instructions to implement a multitasker.

There are at least two tasks, terminal and app, in a round-robin queue.
The terminal task is `begin pause break again`.

## pause

Multitasking involves swapping out user state. To handle this, register U points to
a user space in memory. When `pause` is called, the return address is on the stack.
A minimum user task space contains:

- STATUS, the execution address of the task's handler
- FOLLOWER, the link to the next task in the chain
- TASKNOW, task state data

`: pause  status @a+ >r ;` jumps to the task handler, which is either:
`: sleeping  @a >r ;` which skips to the next task, or:
`: woke  @a+ b!  task[ @a swap !a ]task ;` which swaps out the task:

- `@a+ b!` saves FOLLOWER in B
- `status` compiles `0 user`, which loads A with U.
- `task[` pushes R to the return stack, T to the data stack, loads A with U, and packs T with rp:sp.
- `@a swap !a` swaps out the state.
- `]task` unpacks T into rp and sp, and loads R with B.
- `;` returns to the caller of `pause`.

## task setup

`task` *( user_cells data_stack return_stack -- )* creates a new task:

- *user_cells* is the number of cells in the task's data space (can be 0)
- *data_stack* is the number of cells in the task's data stack (typically 16 to 32)
- *return_stack* is the number of cells in the task's return stack (typically 16 to 32)

The U register is 0 if the round-robin task queue is empty.

A task consists of IDATA and UDATA. The IDATA is:

- STATUS, the execution address of the task's handler
- FOLLOWER, the link to the next task in the chain
- TASKNOW, task state data

The UDATA is static data specifically for the task.


