# Adding a function to API 0

API 0 is the table of C functions that Forth words can call. A word whose
execution token is `API0(n)` runs the VM's API 0 instruction, which calls
`VMapi0Call(n)`, which calls entry `n` of `API0fns[]` in `api0.c`. The index
`n` is a 9-bit field in the instruction, so there can be up to 512 of them.

This walks through the steps using `cold` as the example.

## 1. Write the C function

An API function takes no C arguments and returns an `ior`: 0 for success, or
an error code from `errcodes.h`. It takes its Forth arguments from the data
stack with `vmPop()` and leaves results with `vmPush()`.

Where it goes depends on what it needs:

- If it only uses public interfaces (the VM, `forth.h`, other modules'
  headers), write it in `api0.c` as a `static` function. `cold` is one of
  these:

  ```c
  /* COLD  ( -- ) */
  static int coldboot(void) {
      vmReset();
      g_lf_sys_options |= SYS_OPTION_RUNNING;
      return 0;
  }
  ```

- If it needs another module's private state, write it in that module as
  `int lfAPI_name(void)`, with its prototype and a doc comment in that
  module's header, and make sure `api0.c` includes the header. For example,
  `load` is `lfAPI_load` in `forth.c`, because it uses the interpreter's input
  source, and `buffer` is `lfAPI_buffer` in `lfblocks.c`.

## 2. Append it to the API 0 list

`src/api0_list.h` is the only place API 0 indices are defined. Each entry is
`X(id, function)`, and its position is its index. From it, `api0.h` builds
the `API_...` enum and `api0.c` builds `API0fns[]`.

Add the new entry at the **end** of `API0_LIST`, with a new `API_` name:

```c
    X(API_COMPILE,          lfAPI_compile)      \
    X(API_BREAK,            lfAPI_newinst)      \
    X(API_COLD,             coldboot)
```

The rules:

- **Only append.** Compiled code, including code saved in `lfflash.bin`,
  contains these indices. Inserting, removing or reordering entries would make
  that code call the wrong functions.
- To retire a function, keep its entry and point it at a stub that returns
  `ERR_INVALID_API_CALL`.
- Changing the function an entry calls is fine, since the index stays the
  same. In the same commit as `cold`, `API_BREAK` changed from `lfAPI_break`
  to `lfAPI_newinst`.
- Optional tools that only exist when `FAT_FORTH` is on go at the end of
  `API0_TOOLS_LIST` instead. That list always comes after `API0_LIST`, so
  appending to `API0_LIST` moves every tool's index up by one (adding `cold`
  moved `}t` through `see`). Code that calls the tools must be recompiled;
  see step 6.

## 3. Add a dictionary entry

Give the function a Forth name with a row in `forth_heads[]` in `forth.c`:

```c
    { PREV, "|inst",        API0(API_BREAK),                          0},
    { PREV, "cold",         API0(API_COLD),                           0},
#if (FAT_FORTH & 1)
```

- `PREV` links the row to the one before it. Don't write link numbers.
- The name is matched without regard to case and must fit the 32-byte token
  buffer (31 characters).
- The last field holds flags from `forth.h`: `0` for a normal word, which is
  executed when interpreting and compiled when compiling, or `A_IMMEDIATE`
  for a word that is executed even while compiling, like `;` and `postpone`.

- A table is searched from its last row back to its first, so a later row
  hides an earlier one with the same name. Put core words before the
  `#if (FAT_FORTH & 1)` line and tools after it, next to their list.

## 4. Accept the new API 0 order

`make test` includes `unit_tests/api0`, which prints the API 0 list and
compares it with `expected.txt`. It fails whenever the list changes, so that
an accidental reorder can't slip through. After a deliberate change, update
the expected list:

```sh
make
make -C src/target/desktop/unit_tests/api0 update
git diff src/target/desktop/unit_tests/api0/expected.txt
```

Check the diff: it should show only your new entry (and, for a core word, the
tools moving up by one). Commit `expected.txt` with the change. If you push
without this step, CI fails in the `api0` test.

## 5. Test it

Where the word's effect can be checked from Forth, add assertions to
`scripts/regression.f`:

```forth
T{ 3 4 + -> 7 }T
```

Then run `make test`. It runs `regression.f` with `-o 39` (validation,
quiet, ignore CR), so the first
failing assertion stops `lf` and prints its line number.

## 6. Recompile code saved in flash

If the new entry moved other indices (any addition to `API0_LIST` moves the
tools), code compiled before the change still calls the old indices. Rebuild
it by feeding the source through `lf` again, for example `core.f`, then
`close-flash` to save it to `lfflash.bin`.

## 7. Document it

Add a row for the word to `doc/glossary.md`.

## Checklist

1. C function: `static` in `api0.c`, or `lfAPI_name` in its module with a
   documented prototype in the module's header.
2. `X(API_NAME, function)` appended to the end of `API0_LIST` (or
   `API0_TOOLS_LIST`) in `src/api0_list.h`.
3. `{ PREV, "name", API0(API_NAME), flags},` in `forth_heads[]` in `forth.c`.
4. `make -C src/target/desktop/unit_tests/api0 update`, check the diff.
5. Tests in `scripts/regression.f`; `make test` passes.
6. Code saved in flash recompiled if indices moved.
7. Glossary entry in `doc/glossary.md`.
