#!/usr/bin/env bash

# Test script for Phantom OS
# Requires QEMU 2.5 or higher

function print_help {
	echo "Usage: ./run_phantom.sh [--enable-efi] [--enable-kvm] [--enable-networking] [--external-grub] [--serial-file=<filename>] [--help]"
}

# Save script directory before cd
SCRIPT_DIR="$(dirname "$(realpath "$0")")"
cd $PHANTOM_HOME/run


Q_DEBUG="-gdb tcp::1234  "

Q_BOOT="-boot order=c,menu=on "

# Q_CD="-cdrom img/grub2.img "
# Q_DISK_A="-drive file=fat:rw:fat,format=raw,media=disk "
Q_DISK_A="-drive file=phantom_boot.img,format=raw,media=disk "
Q_DISK_B="-drive file=phantom.img,format=raw,media=disk "

Q_TFTP="-net user,tftp=tftp"
Q_PORTS="-soundhw pcspk "
Q_LOG="-serial stdio"

# VIO="-drive file=vio.img,if=virtio,format=raw -net nic,model=virtio"

for i in "$@"
do
case $i in
	--enable-efi)
		Q_EFI="-L . --bios ${SCRIPT_DIR}/OVMF.fd "
		;;
	--enable-kvm)
		Q_KVM="--enable-kvm"
		;;
	--external-grub)
		Q_BOOT="-boot order=d,menu=on "
		Q_CD="-cdrom ${SCRIPT_DIR}/grub2.img "
		Q_DISK_A="-drive file=fat:rw:fat,format=raw,media=disk "
		;;
	--serial-file=*)
		SERIAL_LOG_FILE="${i#*=}"
		Q_LOG="-serial file:${SERIAL_LOG_FILE}"
		;;
	--enable-networking)
		Q_NET="-net nic,model=pcnet -net nic,model=rtl8139 "
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

qemu-system-i386 $Q_KVM $Q_DEBUG $Q_EFI -display sdl -m 256M $Q_LOG $Q_PORTS $Q_BOOT $Q_CD $Q_DISK_A $Q_DISK_B $Q_TFTP $Q_NET $VIO

exit
