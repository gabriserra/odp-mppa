/* Include board-specific definitions */
INCLUDE board.ld 

__dtb_size = 16K;
REGION_ALIAS("REGION_TLS", internal_mem);

/* Definition of sections */
SECTIONS
{
  /* We assume 1KB for each vector and EV to be set to 1KB at start */
  . = K1_BOOT_ADDRESS;
  .boot.startup : { KEEP(*(.boot.startup)) } > internal_mem
  .boot.startup_eth K1_BOOT_ADDRESS + 0x200 : { KEEP(*(.boot.startup_eth)) } > internal_mem
  
  .boot.exception K1_EXCEPTION_ADDRESS : 
  { 
    /* The main exception handler */
    KEEP(*(.boot.exception)) 
  } > internal_mem
  
  .boot.interrupt K1_INTERRUPT_ADDRESS : 
  { /* The main interrupt handler */
    KEEP(*(.boot.interrupt))
  } > internal_mem
  
  .boot.syscall K1_SYSCALL_ADDRESS :
  {
    /* The main syscall handler */
    KEEP(*(.boot.syscall))
  } > internal_mem
  
  /* The rest of the boot stuff */
  .boot :
  {
    *(.boot)
    . = ALIGN(8);
  }  > internal_mem
  
  .text   ALIGN(8) :
  {
      KEEP(*(.init))
      KEEP(*(.fini))
      *(.text)
      *(.text.*)
    . = ALIGN(4096);
  }  > internal_mem 
  
  .rodata   ALIGN(8) :
  {
    *(.rodata)
    *(.rodata.*)
    . = ALIGN(8);
  }  > internal_mem 
  
  /* The bsp configuration arguments */
  .bsp_config ALIGN(8) :
  {
    KEEP(*(.bsp_config_desc))
    INTERNAL_RAM_SIZE_VALUE = ABSOLUTE(INTERNAL_RAM_SIZE) ;
    RM_STACK_SIZE_VALUE = ABSOLUTE(RM_STACK_SIZE) ;
    DDR_SIZE_VALUE = ABSOLUTE(RamSize) ;
    *(.bsp_config)
  }  > internal_mem

  /* This is the value of the GP base */
  _data_start = .;
  .sdata  ALIGN(8):
  {
    *(.sdata .sdata.* .gnu.linkonce.s.*)
  } > internal_mem

    .sbss  ALIGN(8):
  {
    _sbss_start = .;
    *(.dynsbss)
    *(.sbss .sbss.* .gnu.linkonce.sb.*)
    *(.scommon)
   _sbss_end = .;
  } > internal_mem

  .data   ALIGN(8):
  {
    *(.data)
    *(.data.*)
    . = ALIGN(8);
  }  > internal_mem

  .spawn_dir   ALIGN(8):
  {
    MPPA_SPAWN_DIRECTORY_ADDRESS = ABSOLUTE(.);
    . += MPPA_SPAWN_DIRECTORY_SIZE;
  }  > internal_mem

  .ctors ALIGN(8):
  {
    KEEP(*crtbegin.o(.ctors))
    KEEP(*crtbegin.?(.ctors))
    KEEP (*(EXCLUDE_FILE (*crtend.o *crtend?.o $OTHER_EXCLUDE_FILES) .ctors))
    KEEP (*(SORT(.ctors.*)))
    KEEP (*(.ctors))
  }
  
  .dtors ALIGN(8):
  {
    KEEP (*crtbegin.o(.dtors))
    KEEP (*crtbegin?.o(.dtors))
    KEEP (*(EXCLUDE_FILE (*crtend.o *crtend?.o $OTHER_EXCLUDE_FILES) .dtors))
    KEEP (*(SORT(.dtors.*)))
    KEEP (*(.dtors))
  }
  
  .bss   ALIGN(8):
  {
    _bss_start = .;
    *(.bss)
    *(.bss.*)
    *(COMMON)
    *(.gnu.linkonce.b.*)
    . = ALIGN(8);
    _bss_end = .;
  }  > internal_mem 
  
  /* Stack area for RM */
  .stack ALIGN(0x1000) :
  {      
     _rm_stack_end = ABSOLUTE(.);
     RM_STACK_END = ABSOLUTE(.);
     . += RM_STACK_SIZE;
     . += RM_STACK_SIZE;
     . += RM_STACK_SIZE;
     . += RM_STACK_SIZE;
     _rm_stack_start = ABSOLUTE(.);
     RM_STACK_START = ABSOLUTE(.);
  } > internal_mem

  .mppa_args ALIGN(8): 
  {
    /* Space reserved for mppa_spawn args */
    _mppa_arg_start = ABSOLUTE(.);
    PROVIDE(MPPA_ARGAREA_START = ABSOLUTE(.));
    . += MPPA_ARGAREA_SIZE;
    _mppa_arg_end = ABSOLUTE(.);
  } > internal_mem =0

  /* TLS support */
  INCLUDE tls.ld 

  .heap_start ALIGN(8):
  {
    _heap_start = ABSOLUTE(.);
  } > internal_mem

  . = DEBUG_BASE;

  .heap_end ALIGN(8):
  {
    _heap_end = ABSOLUTE(.);
    _debug_start = ABSOLUTE(.);
  } > internal_mem

   /* DWARF2 debugging sections */
  .debug_info       0 : { *(.debug_info) }
  .debug_line       0 : { *(.debug_line) }
  .debug_abbrev     0 : { *(.debug_abbrev) }
  .debug_frame      0 : { *(.debug_frame) }
  .debug_aranges    0 : { *(.debug_aranges) }
  .debug_pubnames   0 : { *(.debug_pubnames) }
  .debug_str        0 : { *(.debug_str) }
  .debug_funcnames  0 : { *(.debug_funcnames) }
  .debug_typenames  0 : { *(.debug_typenames) }
  .debug_varnames   0 : { *(.debug_varnames) }
  .debug_weaknames  0 : { *(.debug_weaknames) }
  .debug_macinfo    0 : { *(.debug_macinfo) }
  .debug_loc        0 : { *(.debug_loc) }

  /* tracepoints */
  __k1_tracepoint_data 0:
  {
    KEEP (*(__k1_tracepoint_data*))
  }

  .dtb (K1_BOOT_ADDRESS + LENGTH(internal_mem) - __dtb_size) :
  {
    . = .; /* Force output of an empty section */
    __dtb_start = .;
    LONG(0xdeadfeed);
    . += __dtb_size - 4;
    __dtb_end = .;
  } > internal_mem
}
