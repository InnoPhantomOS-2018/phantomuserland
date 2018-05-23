### Script for running Phantom OS on modern Linux systems using modern versions of QEMU

After building OS, you must pack it in image with grub bootloader. To do so, run `make_system_image.sh` script. If you can't do this, you can use external grub image by running system like that: `./run_phantom.sh --external-grub`
