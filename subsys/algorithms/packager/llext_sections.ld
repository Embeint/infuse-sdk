SECTIONS
{
  # Keep function sections contiguous so branch relocations between
  # library functions remain in range after LLEXT loading.
  .text :
  {
    *(.text)
    *(.text.*)
  }

  # Zephyr LLEXT does not support multiple `.bss` sections, fold
  # them all together into a single section in the output file.
  .bss :
  {
    *(.bss)
    *(.bss.*)
    *(COMMON)
  }

  # Mark everything in .exported_sym as used so --gc-sections does
  # not prune this required information.
  .exported_sym : {
    KEEP(*(.exported_sym))
  }
}
