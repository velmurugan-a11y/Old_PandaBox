/*
 * gd32f305vct6.ld -- links this native bring-up firmware as a standalone
 * image starting at 0x08000000 (the chip's own reset vector location) --
 * NOT into the old X-Box APP1/APP2 slot scheme from the sibling
 * pandabox-firmware project. Flash it alone via J-Link for Phase-1
 * bring-up; no bootloader is needed for that.
 *
 * FLASH/RAM sizes: 512KB / 96KB, the "VC" row of the GD32F305xx
 * datasheet's device table -- confirmed exact part from the schematic
 * and BOM (GD32F305VCT6, LQFP100).
 */
MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 96K
}

ENTRY(Reset_Handler)

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } > FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } > FLASH

  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } > RAM AT> FLASH

  .bss :
  {
    . = ALIGN(4);
    _sbss = .;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } > RAM

  PROVIDE(_end = _ebss);

  ._user_heap_stack :
  {
    . = ALIGN(8);
    . = . + 0x2000;
    . = ALIGN(8);
  } > RAM

  _estack = 0x20018000; /* top of 96KB SRAM */
}
