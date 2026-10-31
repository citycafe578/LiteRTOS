#include <avr/io.h>
#include "LiteRTOS_port.h"

#ifndef __AVR__
#error "LiteRTOS_port_avr.c 只能用於 AVR。"
#endif

void LiteRTOS_portSwitch(
    StackType_t **saveSP __attribute__((unused)),
    StackType_t *newSP __attribute__((unused))) __attribute__((naked, noinline));
void LiteRTOS_portSwitch(
    StackType_t **saveSP __attribute__((unused)),
    StackType_t *newSP __attribute__((unused))){
    /* 參數：saveSP 在 r25:r24，newSP 在 r23:r22。 */
    __asm__ __volatile__(
        "push r2  \n\t"
        "push r3  \n\t"
        "push r4  \n\t"
        "push r5  \n\t"
        "push r6  \n\t"
        "push r7  \n\t"
        "push r8  \n\t"
        "push r9  \n\t"
        "push r10 \n\t"
        "push r11 \n\t"
        "push r12 \n\t"
        "push r13 \n\t"
        "push r14 \n\t"
        "push r15 \n\t"
        "push r16 \n\t"
        "push r17 \n\t"
        "push r28 \n\t"
        "push r29 \n\t"
        "in   r0, __SREG__ \n\t"
        "push r0  \n\t"

        /* *saveSP = SP */
        "movw r30, r24 \n\t"
        "in   r24, __SP_L__ \n\t"
        "in   r25, __SP_H__ \n\t"
        "st   Z+, r24 \n\t"
        "st   Z,  r25 \n\t"

        "in   r0, __SREG__ \n\t"
        "cli \n\t"
        "out  __SP_H__, r23 \n\t"
        "out  __SREG__, r0 \n\t"
        "out  __SP_L__, r22 \n\t"

        "pop  r0  \n\t"
        "out  __SREG__, r0 \n\t"
        "pop  r29 \n\t"
        "pop  r28 \n\t"
        "pop  r17 \n\t"
        "pop  r16 \n\t"
        "pop  r15 \n\t"
        "pop  r14 \n\t"
        "pop  r13 \n\t"
        "pop  r12 \n\t"
        "pop  r11 \n\t"
        "pop  r10 \n\t"
        "pop  r9  \n\t"
        "pop  r8  \n\t"
        "pop  r7  \n\t"
        "pop  r6  \n\t"
        "pop  r5  \n\t"
        "pop  r4  \n\t"
        "pop  r3  \n\t"
        "pop  r2  \n\t"
        "ret \n\t"
    );
}
