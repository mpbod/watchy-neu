SECTIONS
{
  . = 0x1000;
  .text : ALIGN(4) { *(.literal .literal.*) *(.text .text.*) }
  .rodata : ALIGN(4) { *(.rodata .rodata.*) }
  .data.rel.ro : ALIGN(4) { *(.data.rel.ro .data.rel.ro.*) }
  .data : ALIGN(4) { *(.data .data.*) *(.got .got.*) }
  .bss (NOLOAD) : ALIGN(4) { *(.bss .bss.* COMMON) }
  /DISCARD/ : { *(.comment .xtensa.info .xt.prop .xt.lit) }
}
