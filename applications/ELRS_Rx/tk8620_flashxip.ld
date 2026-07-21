/*
 * Copyright (c) 2019 Nuclei Limited. All rights reserved.
 * Modifications copyright (c) Shanghai Taolink Technologies Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/******************************************************************************
 * @file     gcc_Device.ld
 * @brief    GNU Linker Script for gd32vf103 based device
 * @version  V1.0.0
 * @date     17. Dec 2019
 ******************************************************************************/

/*********** Use Configuration Wizard in Context Menu *************************/

OUTPUT_ARCH( "riscv" )

/********************* Stack / Heap Configuration ****************************
 * <h> Stack / Heap Configuration
 * <o0> Stack Size (in Bytes) <0x0-0xFFFFFFFF:8>
 * <o1> Heap Size (in Bytes) <0x0-0xFFFFFFFF:8>
 * </h>
 */
__STACK_SIZE = 0x00001000;
__HEAP_SIZE  = 0x00000000;

__RAMCODE_SIZE = 0x00000000;

/********************* ILM RAM Configuration ************************************
 * <h> ILM RAM Configuration
 * <o0> ILM RAM Base Address <0x0-0xFFFFFFFF:8>
 * <o1> ILM RAM Size (in Bytes) <0x0-0xFFFFFFFF:8>
 * </h>
 */
__ILM_FLASH_BASE = 0xc200D800;
__ILM_FLASH_SIZE = 0x00022800;

/*--------------------- DLM RAM Configuration ---------------------------
 * <h> DLM RAM Configuration
 * <o0> DLM RAM Base Address    <0x0-0xFFFFFFFF:8>
 * <o1> DLM RAM Size (in Bytes) <0x0-0xFFFFFFFF:8>
 * </h>
 */

__DLM_RAM_BASE = 0x20080000;
__DLM_RAM_SIZE = 0x00010000;

/**************************** end of configuration section ********************/

/* Define base address and length of ilm_flash and dlm_ram */
MEMORY
{
	ilm_flash (rxai!w) : ORIGIN = __ILM_FLASH_BASE, LENGTH = __ILM_FLASH_SIZE
	dlm_ram (wxai) : ORIGIN = __DLM_RAM_BASE, LENGTH = __DLM_RAM_SIZE
}
/* Linker script to place sections and symbol values. Should be used together
 * with other linker script that defines memory regions FLASH,ILM and RAM.
 * It references following symbols, which must be defined in code:
 *   _Start : Entry of reset handler
 *
 * It defines following symbols, which code can use without definition:
 *   _ilm_lma
 *   _ilm
 *   __etext
 *   _etext
 *   etext
 *   _eilm
 *   __preinit_array_start
 *   __preinit_array_end
 *   __init_array_start
 *   __init_array_end
 *   __fini_array_start
 *   __fini_array_end
 *   _data_lma
 *   _edata
 *   edata
 *   __data_end__
 *   __bss_start
 *   __fbss
 *   _end
 *   end
 *   __heap_end
 *   __StackLimit
 *   __StackTop
 *   __STACK_SIZE
 */
/* Define entry label of program */
ENTRY(_start)
SECTIONS
{
  __STACK_SIZE = DEFINED(__STACK_SIZE) ? __STACK_SIZE : 2K;

  .init.boot      :
  {
    /* Stage1 reset handler runs in Flash, then copies image to RAM */
    KEEP (*(SORT_NONE(.init.boot)))
  } >ilm_flash AT>ilm_flash

  . = ALIGN(4);
  PROVIDE(__ram_image_lma = .);

  .ram_image : AT(__ram_image_lma) ALIGN(4)
  {
    . = ALIGN(4);
    PROVIDE(__ram_vma_start = .);

    *(.vtable .vtable.*)
    *(.vtable_ilm .vtable_ilm.*)
    *(.irq_entry)

    *(.text.trap)
    *(.text.trap.*)

    *(.init)
    *(.fini)
    *(.ram_code)
    *(.code_backup)
    *(.text.unlikely .text.unlikely.*)
    *(.text.startup .text.startup.*)
    *(.text .text.*)
    *(.gnu.linkonce.t.*)

    . = ALIGN(4);
    *(.rdata)
    *(.rodata .rodata.*)
    *(.gnu.linkonce.r.*)
    . = ALIGN(8);
    *(.srodata.cst16)
    *(.srodata.cst8)
    *(.srodata.cst4)
    *(.srodata.cst2)
    *(.srodata .srodata.*)

    /* section information for initial. */
    . = ALIGN(4);
    __rt_init_start = .;
    KEEP(*(SORT(.rti_fn*)))
    __rt_init_end = .;
    /* section information for finsh shell */
    . = ALIGN(4);
    __fsymtab_start = .;
    KEEP(*(FSymTab))
    __fsymtab_end = .;
    . = ALIGN(4);
    __vsymtab_start = .;
    KEEP(*(VSymTab))
    __vsymtab_end = .;

    . = ALIGN(4);
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array))
    PROVIDE_HIDDEN (__preinit_array_end = .);

    . = ALIGN(4);
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
    KEEP (*(.init_array EXCLUDE_FILE (*crtbegin.o *crtbegin?.o *crtend.o *crtend?.o ) .ctors))
    PROVIDE_HIDDEN (__init_array_end = .);

    . = ALIGN(4);
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT_BY_INIT_PRIORITY(.fini_array.*) SORT_BY_INIT_PRIORITY(.dtors.*)))
    KEEP (*(.fini_array EXCLUDE_FILE (*crtbegin.o *crtbegin?.o *crtend.o *crtend?.o ) .dtors))
    PROVIDE_HIDDEN (__fini_array_end = .);

    . = ALIGN(4);
    KEEP (*crtbegin.o(.ctors))
    KEEP (*crtbegin?.o(.ctors))
    KEEP (*(EXCLUDE_FILE (*crtend.o *crtend?.o ) .ctors))
    KEEP (*(SORT(.ctors.*)))
    KEEP (*(.ctors))

    . = ALIGN(4);
    KEEP (*crtbegin.o(.dtors))
    KEEP (*crtbegin?.o(.dtors))
    KEEP (*(EXCLUDE_FILE (*crtend.o *crtend?.o ) .dtors))
    KEEP (*(SORT(.dtors.*)))
    KEEP (*(.dtors))

    . = ALIGN(4);
    PROVIDE(_data = .);
    *(.data .data.*)
    *(.gnu.linkonce.d.*)
    . = ALIGN(8);
    PROVIDE( __global_pointer$ = . + 0x800 );
    *(.sdata .sdata.* .sdata*)
    *(.gnu.linkonce.s.*)
    . = ALIGN(4);
    PROVIDE(_edata = .);
    PROVIDE(edata = .);

    . = ALIGN(4);
    PROVIDE(__ram_vma_end = .);
  } >dlm_ram

  PROVIDE(__ram_lma_start = LOADADDR(.ram_image));
  PROVIDE(__ram_lma_end   = LOADADDR(.ram_image) + SIZEOF(.ram_image));

  . = ALIGN(4);
  PROVIDE( _fbss = . );
  PROVIDE( __bss_start = . );
  .bss (NOLOAD) :
  {
    *(.sbss*)
    *(.gnu.linkonce.sb.*)
    *(.bss .bss.*)
    *(.gnu.linkonce.b.*)
    *(COMMON)
    . = ALIGN(4);
    PROVIDE(__bss_end = .);
  } >dlm_ram

  . = ALIGN(8);
  PROVIDE( _end = . );
  PROVIDE( end = . );
  /* Define stack and head location at dlm_ram */
  .stack ORIGIN(dlm_ram) + LENGTH(dlm_ram) - __STACK_SIZE :
  {
    PROVIDE( _heap_end = . );
    . = __STACK_SIZE;
    PROVIDE( _sp = . );
  } >dlm_ram AT>dlm_ram
}
