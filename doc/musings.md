# ok

What is the purpose of language standards?
It's to keep things the way they are and to clear up ambiguities.
Language standards are there to protect the existing code base.

Forth traditionally prints "ok" after processing an input line.
This is at odds with cooked input. Industry standard terminals let you edit your
input line locally and hit <Enter>, at which point it sends the entire line and emits a local newline.

If you want to use one of the many off-the-shelf terminals instead of writing your own,
complete with line editing, a history buffer, and cut-and-paste, you can't go by the standard
[reference](https://forth-standard.org/standard/implement "Annex E: Reference Implementations")
implementation of QUIT. LiteForth puts "ok>" at the beginning of the line instead.

It makes you wonder what else the standards guys are missing.

# Prefetch buffers

128-bit prefetch buffers are often used in modern MCUs. How does that impact the ISA?
A 128-bit buffer would fit eight (8) 16-bit instructions.
Definition addresses do not need to be 128-bit-aligned, but it may help.

## A hypothetical ISA

A 128-bit prefetch buffer, or `ig` can hold 21 slots. where each slot is 6-bit.
Immediate data could be 6 to 24 bits wide.

To indicate data stack effect, a `.` at the beginning of a name indicates a *push*
to the data stack and a `.` at the end indicates a *pop*.

| \\  | *0*   | *1*   | *2*   | *3*   | *4*   | *5*   | *6*   | *7*   |
|-----|-------|-------|-------|-------|-------|-------|-------|-------| 
| *0* | nop   | inv   | ;     |       | 2/c   | 2/    | 2\*   | swap  |  
| *1* | drop. | xor.  | and.  | +.    | !a.   | !a+.  | !b.   | !b+.  |  
| *2* | .a    | .b    | .r>   | .cy   | .dup  | .over | .r@   |       |     
| *3* | a!.   | b!.   | >r.   | cy!.  | .@a   | .@a+  | .@b   | .@b+  |  
| *4* | -if1  | -if2  | next1 | next2 | API01 | API02 | API11 | API12 |
| *5* | if1.  | if2.  | rjmp1 | rjmp2 |       |       | rcal1 | rcal2 |  
| *6* | .lit1 | .lit2 | .lit3 | .lit4 | .lit5 | .lit6 | .bof1 | .bof2 |  
| *7* | jump1 | jump2 | jump3 | jump4 | call1 | call2 | call3 | call4 |  

- cy = carry caused by addition or shift
- u = general purpose register
- a, b = address registers for memory
- @ and ! support bit fields: auto-increment handles bit fields
- literals are signed
- `bof` is a literal offset by the B register
- jumps and calls have a 6- to 24-bit absolute address.
- If you have to jump higher than 24-bit, use a literal followed by `>r ;`.
- ifs, next, and relative jumps and calls have a 6- or 12-bit relative address
- API0 and API1 have a 6- or 12-bit function number

## Hardware

It takes a bit of multiplexing to pull this off.
A barrel shifter would use the 5-bit `sid` to shift the `ig`,
and then the instruction decoder selects how to mask off the output.
So, immediate data takes some time to settle.
Any immediate data that is routed to an adder should register it before the adder,
removing a critical path at the expense of an added clock cycle.

The normal Forth instructions don't have decoding in their critical path.
The inputs to the ALU are already registers. Decode selects which output to save.

## Simulation

There's the rub. Decoding a 128-bit instruction on an MCU won't be pretty.
This is why I'm sticking with the 16-bit ISA.
