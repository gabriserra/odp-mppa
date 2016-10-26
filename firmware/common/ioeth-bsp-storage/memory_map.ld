MEMORY
{
    /* Definitions of available memory regions */
    private_smem      	   : ORIGIN = 0x003F0000, LENGTH = 64K
    internal_mem           : ORIGIN = 0xF0000000, LENGTH = 2M
    ddr                    : ORIGIN = 0xF0200000, LENGTH = 254M
}
