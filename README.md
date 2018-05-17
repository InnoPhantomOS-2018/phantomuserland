# Welcome to Phantom OS - Summer Internship 2018 Edition #

## Current tasks ##

- [ ] Inspect and document startup process of OS: from ASM code and C entry point to GUI startup. Match those steps with corresponding code file/function.
- [ ] Inspect problems with Disk Drivers when running OS in modern QEMU
- [ ] Inspect problems with running OS on modern QEMU with UEFI enabled (see phantom_new.sh for commented line)


## Build ##

  **Important!** Set '''PHANTOM_HOME''' environment variable to the path to Phantom repository root directory,
  "make all" there. If you don't know how, don't hesitate to ask!

  In Windows you will need Cygwin to do that.
  Select, at least: gcc4, subversion, binutils, make, gdb
  (see etc/cygwin_get.cmd)

  See also [TOOLCHAIN](https://github.com/dzavalishin/phantomuserland/blob/master/TOOLCHAIN)

## Run ##
  **Note**: those instructions refer to new, better way to run Phantom is QEMU. I don't know if they work in Windows. In any case, just refer to older scripts, i.e. `phantom_clean` and `phantom`.

  To run OS, you first need to create a phantom 'swap' disk image. Run `zero_ph_img` script to do that.

  Then, you want to wrap together grub bootloader and kernel files. Script `make_system_image.sh` should do everyting. **Note**: you need to have grub2 installed, so that you have `grub-mkrescue` binary available.

  The problem is that older grub doesn't work with newer QEMU for some reason, so you can't just use old floppy image. If you can't use grub-mkrescue for some reason, others can build external grub2 image for you, that you can use with phantom_new script - refer to commented lines.

  See doc/RUNNING for more details

## Debug ##
  Refer to wiki to learn how to debug kernel

## Directories ##

trunk/oldtree/kernel/phantom - kernel
trunk/phantom                - libs and unix userland (user/apps)
trunk/plib/sys/src           - native phantom userland code

trunk/run                    - QEMU run/test environment
trunk/tools/plc              - phantom language compiler / java bytecode translator

## Dox ##

  https://github.com/dzavalishin/phantomuserland/wiki
  doc/*

### We will obviously restore old README.md before merging back. ###
