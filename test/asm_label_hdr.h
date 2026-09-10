/* GNU/Darwin asm labels on declarators (sys/cdefs.h __DARWIN_ALIAS*). */
#pragma once

int asm_label_fn(int x) __asm("_asm_label_fn");
int asm_label_fn2(void) __asm__("_asm_label_fn2");
