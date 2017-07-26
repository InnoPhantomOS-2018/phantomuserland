#include <ia32/asm.h>

#if 0
#include <ia32/seg.h>

	.code16		

Entry(bios_int_10)

    int		$0x10

	// Far jump to kernel data segment
	ljmp	$MAIN_TSS, $0
	//ljmp	$0x50, $0

	// for any case - exit with int 21
    int		$0x21

Entry(bios_int_10_end)

#else
	.code16		

// This one is stable
Entry(bios_int_10)
    int		$0x10
    int		$0x21
Entry(bios_int_10_end)

#endif
