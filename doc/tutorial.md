# Tutorial

A very, very rough draft.

LiteForth is a cross-platform Forth designed to run on an MCU, but it can run on a PC
as console app. The console app uses stdio directly.
It has command line options to replace stdio with a serial port,
but `serial_io.c` has a problem (under Windows, anyway), so not currently.

With no command line arguments, it uses stdio.

Once `./lf` is launched, `bye` quits and `words` lists the words.

## Forth on a terminal

Industry standard terminals, in cooked mode, let you edit your input line locally and hit <Enter>,
at which point it sends the entire line and emits a local newline.
This breaks the Forth tradition of printing `ok` at the end.
To get around this, input starts with an `ok>` prompt on a new line.
Any output produced as a result of interpreting the input begins on a new line.
`ok>` appears after this output.

When connected to stdio, LiteForth can take its input from a file instead of stdin.
For example, entering `./lf < myforth.f` on the command line routes `myforth.f`
to stdin, which is like your computer typing `myforth.f` into the console really fast.
When `myforth.f` is exhausted, LiteForth switches you back over to the terminal.
So, you could boot up an entire Forth application with `./lf < bootme.f` as long as
`bootme.f` contains all of the code.

You can paste clipboard text into a terminal with a right-click on a mouse or ctrl+V.
Text that contains newlines does the obvious: you see `ok>`s on the left.

## Run-time display options

- `>options` ( u -- ) Sets the option flags
- `options>` ( -- u ) Gets the option flags

- 0010h : `>options` is locked, `u` is ignored
- 0008h : echo input lines
- 0004h : quit immediately upon error
- 0002h : suppress the stack display before `ok>`
- 0001h : suppress `ok>` prompt

When you are replacing stdin with a file, you don't want `ok>` printing after
each line. The input file would start with:

- `3 >options` if Forth code you want to include at startup; put `0 >options` at the end.
- `7 >options` if a test file; put `bye` at the end.

You don't need a trailing blank line. 
The last line of the file should contain `0 >options` so you don't get multiple `ok>`s.

## Scripts

The `./scripts` folder contains input scripts for LiteForth.

- `primitives.f` is the file for CI/CD. It runs a regression test on the Forth primitives.

## Bitfields

Native handling of bit fields by `@` and `!` is a Forth novelty.
It's really pretty interesting to see in action.
It takes advantage of 32-bit numbers having 10 bits more than you need to cover a 16 MB
(4M cell) address space. Those extra bits contain a size and shift count.
`@` and `!` handle data of any width from 1 to 32 bits without any additional operators
like `c@`, `c!`, `w@`, `w!`, etc.

Bytes have always been a conundrum for cell-addressed machines.
Treating bytes as 8-bit-wide bit fields,
with `@` and `!` supporting bit fields in hardware, is one solution.

`char+` steps to the next bit field, so you can have an array of 8-bit bytes, 
or an array of any width of data between 1- and 32-bit.

Variables smaller than 32-bit are bit fields (`: variable 32 bits ;`).
You could define several small variables in 1 cell. For example,
```
4 bits mynybl
8 bits mybit
1 bits myflag
19 bits whateverelse
```
Bit fields compile using `,`.
Use `bit` to set the bit field width of the data you want to compile.
`bit` starts a new cell if the current cell doesn't have enough unused bits left.
`: align  32 bit ;` unconditionally aligns whichever memory space you are in.
To compile everyone's favorite bit field, the byte, use `8 bit`.
`,` will behave like `c,`.

## Dictionary pointers

The `dp[]` data structure maintains pointer context for the dictionary.
On some Forths, such as those with interleaved everything, a variable `dp` will do.
Not so in LiteForth. Different pointers are used for code, data, and header spaces.

`dp[]` is a 6-cell 3x2 matrix arranged like this:

| *data*  | *code*  | *head*  |
|---------|---------|---------|
| `dp`    | `cp`    | `hp`    |
| `dpmax` | `cpmax` | `hpmax` |

The 2-bit variable `dp^` selects the column to be used by `here` and `,`.
Some definitions that are built from `dp[]` and `dp^` are:
```forth
: 'here   dp^ @ cells dp[] + ;
: here    'here @ ;
: unused  'here dup 3 + @ swap @ - ;
: _data   0 dp^ ! ;
: _code   1 dp^ ! ;
: _head   2 dp^ ! ;
: ,       'here @ dup >r ! r> char+ ; 
```
# create does>

- `create` creates a word that returns a data pointer.
- `does>` replaces the return code with a jump to new code.

In LiteForth, `does>` has two use cases:

1. `create` compiles to Flash, so its data is read-only.
2. `create` compiles to RAM, so its data is volatile.

```forth
variable doesptr
: create  : here dup ,lit  there doesptr !  postpone ; ;
: does>   doesptr @ torg ] ;
```

# header

`header` ( w aux <name> -- ) creates a new header in the current wordlist.
`w` and `aux` are data to populate the header, since it lives outside of
the LiteForth sandbox.

`>header` ( w aux -- ) modifies the last defined header by ORing the w and aux fields.


