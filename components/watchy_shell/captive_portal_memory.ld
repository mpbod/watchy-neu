/*
 * This ESP32 D/IRAM window is outside the low DRAM data segment and is
 * byte-addressable from both cores. captive_portal_idf.c reserves the same
 * range from the heap before heap registration.
 */
MEMORY
{
    watchy_captive_task_seg (RW) : ORIGIN = 0x3ffe5000, LENGTH = 0x1400
}

_watchy_captive_task_region_start = ORIGIN(watchy_captive_task_seg);
_watchy_captive_task_region_end =
    ORIGIN(watchy_captive_task_seg) + LENGTH(watchy_captive_task_seg);

SECTIONS
{
    .watchy_captive_task (NOLOAD) : ALIGN(16)
    {
        _watchy_captive_task_start = ABSOLUTE(.);
        KEEP(*(.watchy_captive_task.tcb))
        . = ALIGN(16);
        _watchy_captive_task_stack_start = ABSOLUTE(.);
        KEEP(*(.watchy_captive_task.stack))
        _watchy_captive_task_stack_end = ABSOLUTE(.);
        . = ALIGN(16);
        _watchy_captive_task_end = ABSOLUTE(.);
    } > watchy_captive_task_seg
}

ASSERT(
    ADDR(.watchy_captive_task) == ORIGIN(watchy_captive_task_seg),
    "Watchy captive DNS task storage moved from its reserved D/IRAM window"
)
ASSERT(
    SIZEOF(.watchy_captive_task) <= LENGTH(watchy_captive_task_seg),
    "Watchy captive DNS task storage exceeds its reserved D/IRAM window"
)
ASSERT(
    (_watchy_captive_task_stack_end - _watchy_captive_task_stack_start) >= 4096,
    "Watchy captive DNS static stack is smaller than 4096 bytes"
)
ASSERT(
    (ORIGIN(dram0_0_seg) + LENGTH(dram0_0_seg) - _bss_end) >= 1536,
    "Watchy fixed low-DRAM reserve fell below 1536 bytes"
)
