#!/usr/bin/env bash

# Script to run Phantom OS in old QEMU

function print_help {
	echo "Usage: ./run_phantom.sh [--qemu-version=<n.nn.n>] [--serial-file=<filename>] [--help]"
}

cd $PHANTOM_HOME/run

# Despite the fact that there is windows binary in this directory, we use bios file from there
QDIR="qemu/0.14.1"

# SOUND=-soundhw sb16

# USB=-usb -usbdevice mouse
# USB=-usb

# VIO=-drive file=vio.img,if=virtio,format=raw -net nic,model=virtio
# VIO=-drive file=vio.img,if=virtio,format=raw
# VIO=-net nic,model=virtio

# You expected serial output to the file, but it was me, stDIO!
Q_LOG="-serial stdio"

# Q_NET= -net nic,model=ne2k_pci -net user -tftp ./tftp
Q_NET="-net nic,model=pcnet -net nic,model=rtl8139  -net user -tftp ./tftp"

# Q_MACHINE=-m 85

Q_DISKS="-boot a -no-fd-bootchk -fda img/grubfloppy.img -hda fat:fat -hdb phantom.img "

# Q_KQ=-enable-kqemu
#Q_KQ="-kernel-kqemu"

# Q_VGA=-vga std
# Q_VGA=-vga cirrus
Q_VGA="-vga vmware"
# -virtioconsole 4

# [ -s local.sh ] && {
# echo Loading local.sh
# . ./local.sh
# }


# process arguments
for i in "$@"
do
case $i in
	--qemu-version=*)
		QEMU_VERSION="${i#*=}"
		QDIR="qemu/$QEMU_VERSION"
		shift # past argument=value
		;;
	--serial-file=*)
		SERIAL_LOG_FILE="${i#*=}"
		Q_LOG="-serial file:${SERIAL_LOG_FILE}"
		;;
	--help|-h)
		print_help
		exit
		;;
	*)
		;;
esac
done

# if log is redirected to file
if [ "$Q_LOG" != "-serial stdio" ]; then
	# If old log already exists
	if [ -e "$SERIAL_LOG_FILE" ]; then
		# If even older log already exists
		if [ -e "${SERIAL_LOG_FILE}.old" ]; then
			# then delete it
			rm "${SERIAL_LOG_FILE}.old"
		fi
		# then backup a file
		mv "${SERIAL_LOG_FILE} ${SERIAL_LOG_FILE}.old"
	fi
fi

qemu -smp 3 $Q_VGA -s $Q_KQ -L $QDIR/bios $Q_MACHINE $Q_LOG $Q_DISKS $Q_NET $VIO $USB $SOUND

exit


# ----- Unused

# SCSI=-drive file=scsi.img,if=scsi,unit=0
# Q_PORTS= -parallel file:lpt_01.log  -serial file:serial0.log
#    -net nic,model=rtl8139 -net nic,model=i82559er -net nic,model=pcnet -net nic,model=ne2k_isa
# Q_DISKS=-boot a -no-fd-bootchk -fda img/grubfloppy.img -hda fat12.img -hdb phantom.img
# Q_DISKS=-boot a -fda img/grubfloppy.img -hda fat12.img -hdb phantom.img
# Q_MACHINE=-M isapc
# DEBUG=
