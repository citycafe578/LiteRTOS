#ifndef LITERTOS_INTERNAL_H
#define LITERTOS_INTERNAL_H

#include "LiteRTOS.h"

#if !defined(__AVR__)
#error "LiteRTOS currently targets AVR microcontrollers only."
#endif

#ifndef LITERTOS_MAX_TASKS
#define LITERTOS_MAX_TASKS 4
#endif


#define LITERTOS_STACK_FILL 0xA5U
#define LITERTOS_TASK_READY     0U
#define LITERTOS_TASK_BLOCKED   1U
#define LITERTOS_TASK_SUSPENDED 2U

struct LiteRTOS_TCB
{
    StackType_t *topOfStack;
    TaskFunction_t taskCode;
    const char *taskName;
    void *taskParameters;

    StackType_t *stackBase;
    configSTACK_DEPTH_TYPE stackDepth;

    TickType_t delayStart;
    TickType_t delayTicks;

    UBaseType_t priority;
    uint8_t state;
};

typedef struct LiteRTOS_TCB LiteRTOS_TCB;

/* 由 uxTaskGetStackHighWaterMark.s 實作：從 base 起算連續等於 fill 的位元組數（最多 depth）。 */
uint16_t prvCountUntouchedStack(const StackType_t *base, uint16_t depth, uint8_t fill);


#if !defined(__INTELLISENSE__)
_Static_assert(
    sizeof(LiteRTOS_TCB) == LITERTOS_TCB_SIZE,
    "LITERTOS_TCB_SIZE does not match LiteRTOS_TCB.");
#endif

#endif
