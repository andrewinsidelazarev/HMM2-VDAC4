/* Codex: все данные unwinder и его относительные указатели находятся в одной секции загрузчика. */
SECTIONS
{
  . = SIZEOF_HEADERS;
  .hash : { *(.hash) }
  .gnu.hash : { *(.gnu.hash) }
  .dynsym : { *(.dynsym) }
  .dynstr : { *(.dynstr) }
  .rela.dyn : { *(.rela.dyn) *(.rela.data*) *(.rela.rodata*) *(.rela.text*) *(.rela.got) *(.rela.ctors) *(.rela.dtors) }
  .rela.plt : { *(.rela.plt) }
  . = ALIGN(16);
  .text : { *(.plt .plt.* .got.plt .got.plt.*) *(.literal .text .stub .literal.* .text.* .gnu.linkonce.literal.* .gnu.linkonce.t.*) }
  . = ALIGN(16);
  .data : {
    *(.rodata .rodata.* .gnu.linkonce.r.*)
    . = ALIGN(4); __hmm_eh_begin = .; KEEP(*(.eh_frame)) LONG(0); __hmm_eh_end = .;
    *(.gcc_except_table .gcc_except_table.*)
    . = ALIGN(4); __hmm_ctors_begin = .; KEEP(*(SORT(.ctors.*))) KEEP(*(.ctors)) __hmm_ctors_end = .;
    __hmm_init_begin = .; KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array)) __hmm_init_end = .;
    __hmm_dtors_begin = .; KEEP(*(SORT(.dtors.*))) KEEP(*(.dtors)) __hmm_dtors_end = .;
    __hmm_fini_begin = .; KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array)) __hmm_fini_end = .;
    *(.data.rel.ro .data.rel.ro.* .got .data .data.* .gnu.linkonce.d.*)
    LONG(0);
  }
  . = ALIGN(16);
  .bss : { *(.bss .bss.* .gnu.linkonce.b.*) *(COMMON) }
  .dynamic : { *(.dynamic) }
  .got.loc : { *(.got.loc) }
  .xt.lit 0 : { *(.xt.lit .xt.lit.*) }
  .xt.prop 0 : { *(.xt.prop .xt.prop.*) }
}
