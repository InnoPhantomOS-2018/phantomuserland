#!/usr/bin/env bash

# Save script directory before cd
SCRIPT_DIR="$(dirname "$(realpath "$0")")"
cd $PHANTOM_HOME/run

if ! type "grub-mkrescue" > /dev/null; then
	echo "grub-mkrescue not found, please install grub package with your package manager or use external grub image (./run_phantom.sh --external-grub)"
	exit
fi

echo "Making phantom_boot.img image, using content of fat folder"
cp -R ${SCRIPT_DIR}/grub_files/* ./fat/
sudo grub-mkrescue -o ./phantom_boot.img --modules="font gfxterm echo reboot usb_keyboard multiboot fat ls cat ext2 iso9660 reiserfs xfs part_sun part_gpt part_msdos" ./fat/

sudo chown $USER phantom_boot.img
