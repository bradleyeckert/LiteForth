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

`cell+` steps to the next bit field, so you can have an array of 8-bit bytes, 
or an array of any width of data between 1- and 32-bit.

