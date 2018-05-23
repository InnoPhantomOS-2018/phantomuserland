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

No matter in which VM you are planning to run Phantom, you will need a special phantom image. To create one, run `zero_ph_img.{sh,cmd}` script.

### Windows ###
  Go to `/run/qemu_cygwin` directory and run `phantom.cmd`. In case of problems, fix them.

### Old QEMU, Ubuntu 12.04 ###
  To actually see Phantom OS running, you currently need an old QEMU, that require Ubuntu 12.04, because it has required versions of libraries in repositories.

  Install qemu like that `sudo dpkg -i /etc/qemu_0.15.0-2_amd64.deb`. Then install following libraries:

  - libcurl3

  - libesd0

  - libjpeg62

  - libspice-server1

  Also, follow instructions from [here](https://askubuntu.com/a/339371).

  After that and building system, you should be able to run system with `/run/qemu_old/run_phantom.sh` script.

### Modern QEMU on modern Linux-based OS ###
  Install qemu and, if required, package with additional architectures. Refer to your distribution  package repositories.

  The problem is that older grub doesn't work with newer QEMU for some reason, so you can't just use old floppy image.

  So, you need a grub2 image. Best way to get one is to wrap together grub bootloader and kernel files. Script `make_system_image.sh` should do everyting for you. **Note**: you need to have grub2 installed, so that you have `grub-mkrescue` binary available. If you can't use grub-mkrescue for some reason, use pre-built external grub image - it's similar to one used in script for older systems, but more modern - see below on how to use it.

  Then, run system with `/run/qemu_modern/run_phantom.sh` script. Launch it with `--help` flag for help on possible arguments/flags. For example, flag `--external-grub` will launch system with grub from `/run/qemu_modern/grub2.img` file,

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
