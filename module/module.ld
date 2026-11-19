/* SPDX-License-Identifier: MPL-2.0 */
/* Augments the linker's default shared-object script: constructors and
 * destructors go to private sections that module/runtime/crt.c runs, so the
 * QNX loader sees no DT_INIT_ARRAY or DT_FINI_ARRAY to run on its own. */
SECTIONS
{
  .nbt_preinit_array : {
    PROVIDE_HIDDEN(__nbt_preinit_array_start = .);
    KEEP(*(.preinit_array))
    PROVIDE_HIDDEN(__nbt_preinit_array_end = .);
  }
  .nbt_init_array : {
    PROVIDE_HIDDEN(__nbt_init_array_start = .);
    KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
    KEEP(*(.init_array))
    PROVIDE_HIDDEN(__nbt_init_array_end = .);
  }
  .nbt_fini_array : {
    PROVIDE_HIDDEN(__nbt_fini_array_start = .);
    KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
    KEEP(*(.fini_array))
    /* picolibc's atexit/__cxa_atexit runner (C++ static destructors). Last
     * here, so it runs first: the array runs backwards. */
    KEEP(*(.fini_array_onexit))
    PROVIDE_HIDDEN(__nbt_fini_array_end = .);
  }
}
INSERT BEFORE .init_array;
