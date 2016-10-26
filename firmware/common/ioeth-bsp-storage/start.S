.section .boot.startup_eth, "ax", @progbits
.align 8
.global _start_eth
.proc _start_eth
_start_eth:
	make $r14 = _data_start
# Get node ID
	get $r5 = $pcr
	;;
	extfz $r0 = $r5, 23, 16
	;;
	make $r4,K1_EXCEPTION_ADDRESS 
	;;
	set $ev = $r4 
	;;
	make $r12 = _rm_stack_start - 16
	comp.ne $r1 = $r0, 160
	comp.ne $r2 = $r0, 224
	;;
	and $r1 = $r1, $r2
	;;
	cb.nez $r1, __k1_asm_start_pe
	;;
	call __k1_rm_c_startup
	;;
	goto __k1_asm_proc_poweroff
	;;
