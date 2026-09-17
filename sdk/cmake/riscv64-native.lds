/* Keep ELF addresses equal to PE RVAs. GenFw merges read-only input into
 * .text and writable input into .data. No section is pageable or discarded
 * during this bootstrap stage; there is no SEH, TLS, or ELF dynamic loader. */
PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x1000;
    .text : ALIGN(0x1000)
    {
        __riscv_text_start = .;
        *(.text .text.* INIT PAGE)
        *(.rodata .rodata.* .srodata .srodata.* .rdata .rdata.*)
        __riscv_text_end = .;
    } :text

    .data : ALIGN(0x1000)
    {
        __riscv_data_start = .;
        *(.data .data.* .sdata .sdata.* INITDATA PAGEDATA)
        *(.bss .bss.* .sbss .sbss.* COMMON)
        __riscv_data_end = .;
    } :data

    .unsupported :
    {
        *(.got .got.* .plt .plt.* .dynamic .dynsym .dynstr)
        *(.tdata .tdata.* .tbss .tbss.* .eh_frame .eh_frame.*)
        *(.init_array .init_array.* .fini_array .fini_array.*)
    }
    ASSERT(SIZEOF(.unsupported) == 0, "RISC-V native images do not support GOT, ELF dynamic linking, TLS, constructors or unwind tables")
    /DISCARD/ : { *(.note.GNU-stack .comment) }
}
