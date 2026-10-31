#ifndef LITERTOS_PORT_H
#define LITERTOS_PORT_H

#include "LiteRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LITERTOS_PORT_CONTEXT_SIZE 21U

StackType_t *LiteRTOS_portInitStack(StackType_t *stackTop, void (*entry)(void));

void LiteRTOS_portSwitch(StackType_t **saveSP, StackType_t *newSP);

#ifdef __cplusplus
}
#endif

#endif
