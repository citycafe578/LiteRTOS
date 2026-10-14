#include <stdbool.h>
#include <stddef.h>
#include <Arduino.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include "LiteRTOS_internal.h"
#include "LiteRTOS_port.h"

#if configTICK_RATE_HZ != 1000U
#error "This initial port requires configTICK_RATE_HZ == 1000."
#endif

static LiteRTOS_TCB *taskList[LITERTOS_MAX_TASKS];
static UBaseType_t taskCount = 0;
static UBaseType_t lastRunIndex = 0xFF;     /* 0xFF：no task ran */
static LiteRTOS_TCB *currentTask = NULL;
static StackType_t *schedulerSP = NULL;

static uint8_t criticalNesting = 0;
static uint8_t criticalSavedSREG = 0;

static TickType_t getTickCountInternal(void){
    return (TickType_t)millis();
}

static bool isRunable(const LiteRTOS_TCB *task, TickType_t now){
    switch(task -> state){
        case LITERTOS_TASK_READY:
            return true;
        case LITERTOS_TASK_BLOCKED:
            return false;
        case LITERTOS_TASK_SUSPENDED:
            return (TickType_t)(now - (task -> delayStart)) >= (task -> delayTicks);
        default:
            return false;
    }
}

static LiteRTOS_TCB *pickNext(TickType_t now){
    LiteRTOS_TCB *best = NULL;
    UBaseType_t bestInd = 0;
    const UBaseType_t start = (UBaseType_t)(lastRunIndex + 1);
    for(UBaseType_t i = 0; i < taskCount; i++){
        UBaseType_t k = (UBaseType_t)(lastRunIndex + i);
        if(k >= taskCount){
            i = (UBaseType_t)(i - taskCount);
        }
        LiteRTOS_TCB *task = taskList[i];

        if(!isRunable(task,now)){
            continue;
        }

        if(best == NULL || task -> priority > best -> priority){
            best = task;
            bestInd = i;
        }
    }

    if(best != NULL){
        lastRunIndex = bestInd;
    }
    
    return best;
}

static void switchToScheduler(void){
    LiteRTOS_portSwitch(&currentTask -> topOfStack, schedulerSP);
}

static void blockCurrenFor(TickType_t ticks){
    currentTask -> delayStart = getTickCountInternal();
    currentTask -> delayTicks = ticks;
    currentTask -> state = LITERTOS_TASK_BLOCKED;
    switchToScheduler();
}

static void taskEntry(void){
    LiteRTOS_TCB *self = currentTask;
    self -> taskCode(self -> taskParameters);
    self -> state = LITERTOS_TASK_SUSPENDED;
    while(1){
        LiteRTOS_portSwitch(&self -> topOfStack, schedulerSP);
    }
}

TaskHandle_t xTaskCreateStatic(
    TaskFunction_t taskCode,
    const char *const taskName,
    configSTACK_DEPTH_TYPE stackDepth,
    void *taskParamters,
    UBaseType_t priority,
    StackType_t *stackBuffer,
    StaticTask_t *taskBuffer
){
    if(taskCode == NULL || taskBuffer == NULL || stackBuffer == NULL || stackDepth < LITERTOS_PORT_CONTEXT_SIZE || taskCount >= LITERTOS_MAX_TASKS){
        return NULL;
    }

    LiteRTOS_TCB *tcb = (LiteRTOS_TCB *)taskBuffer -> storage;
    for(configSTACK_DEPTH_TYPE i = 0; i < stackDepth; i++){
        stackBuffer[i] = LITERTOS_STACK_FILL;
    }

    tcb->taskCode = taskCode;
    tcb->taskName = taskName;
    tcb->taskParameters = taskParamters;
    tcb->stackBase = stackBuffer;
    tcb->stackDepth = stackDepth;
    tcb->delayStart = 0;
    tcb->delayTicks = 0;
    tcb->priority = priority;
    tcb->state = LITERTOS_TASK_READY;
    tcb->topOfStack = LiteRTOS_portInitStack(&stackBuffer[stackDepth - 1], taskEntry);

    taskList[taskCount++] = tcb;

    return tcb;
}

void vTaskStartScheduler(void){
    while(1){
        LiteRTOS_TCB *next = pickNext(getTickCountInternal());
        if(next == NULL){
            continue;
        }
        next -> state = LITERTOS_TASK_READY;
        currentTask = next;
        LiteRTOS_portSwitch(&schedulerSP, next -> topOfStack);
        currentTask = NULL;
    }
}

void vTaskDelay(TickType_t ticksToDelay){
    if(currentTask == NULL){
        return;
    }
    if(ticksToDelay == 0){
        switchToScheduler();
        return;
    }

    if(ticksToDelay == portMAX_DELAY){
        currentTask -> state = LITERTOS_TASK_SUSPENDED;
        switchToScheduler();
        return;
    }
    blockCurrenFor(ticksToDelay);
}

BaseType_t xTaskDelayUntil(TickType_t *previousWakeTime, TickType_t timeIncrement){
    if(previousWakeTime == NULL || currentTask == NULL){
        return pdFALSE;
    }

    const TickType_t constTickCount = getTickCountInternal();
    const TickType_t timeToWake = (TickType_t)(*previousWakeTime + timeIncrement);
    BaseType_t shouldDely = pdFALSE;

    if(constTickCount < *previousWakeTime){
        if(timeToWake < *previousWakeTime && timeToWake > constTickCount){
            shouldDely = pdTRUE;
        }
    }else{
        if(timeToWake < *previousWakeTime || timeToWake > constTickCount){
            shouldDely = pdTRUE;
        }
    }

    *previousWakeTime = timeToWake;

    if(shouldDely != pdFALSE){
        blockCurrenFor((TickType_t)(timeToWake - constTickCount));
    }else{
        switchToScheduler();
    }

    return shouldDely;
}

vPortYield(void){
    if(currentTask == NULL){
        return;
    }
    switchToScheduler();
}

TickType_t xTaskGetTickCount(void){
    return getTickCountInternal();
}

TaskHandle_t xTaskGetCurrentTaskHandle(void){
    return currentTask;
}

UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task){
    const LiteRTOS_TCB *tcb = (task != NULL)? task : currentTask;
    if(tcb == NULL){
        return 0;
    }
    configSTACK_DEPTH_TYPE untouched = 0;
    while(untouched < tcb -> stackDepth && tcb -> stackBase[untouched] == LITERTOS_STACK_FILL){
        untouched++;
    }

    return (untouched > 255)? 255 : (UBaseType_t)untouched;
}

void vPortEnterCritical(void){
    const uint8_t saved = SREG;
    cli();
    if(criticalNesting == 0){
        criticalSavedSREG = saved;
    }
    criticalNesting++;
}

void vPortExitCritical(void){
    if(criticalNesting == 0){
        return;
    }
    if(--criticalNesting == 0){
        __asm__ __volatile__("" ::: "memory");
        SREG = criticalSavedSREG;
    }
}