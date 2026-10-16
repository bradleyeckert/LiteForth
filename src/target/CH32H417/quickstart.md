# Running Forth on the nanoCH32H417

- Install MounRiver and open the `SimulateCDC` solution.
- Connect the nanoCH32H417 "WCHLinkE" port to your PC with a USB-C cable.
- If the "WCHLinkE" has a solid blue light, it is in ARM mode. Change it to RISC-V mode.
- Click on "Build All Projects" or Ctrl+B.
- Highlight the V5F project.
- Click on "Download" or F8.

At this point, the "USB-FS" USB-C may be connected to a PC running a terminal emulator.
While any terminal emulator will work, [kwai](https://github.com/bradleyeckert/kwai) was written
specifically to support embedded targets with cooked input. It has useful LifeForth-specific features.

To extend the system:

- Format a microSD card with 2 partitions: FAT32 (or whatever) in the first partition, and an unformatted (or type DA) second partition.
- Run the `./forth/build.bat` batch file to build a disk image for mass storage.
- Copy the generated `lfblocks.fb4` file to this second partition.
- If you bought the kit with the 1.54" LCD module, plug in the LCD module.
- Plug the microSD card into the nanoCH32H417 board and power it up.
- Enter `1 load`.

The board now has a Forth dictionary in Flash.

The boot demo bumps `counter`, so `counter @ .` shows it incrementing.
`try` launches a clock demo, which uses the multitasker to maintain a clock display.
