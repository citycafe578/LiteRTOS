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
static volatile uint8_t readyMask = 0, blockedMask = 0;
static bool prioUniform = true;

static TickType_t getTickCountInternal(void){
    return (TickType_t)millis();
}

static void setState(LiteRTOS_TCB *task, uint8_t newState){
    const uint8_t sreg = SREG;
    cli();
    const uint8_t bit = (uint8_t)(1U << task->slot);
    uint8_t ready = readyMask & (uint8_t)~bit;
    uint8_t blocked = blockedMask & (uint8_t)~bit;
    if(newState == LITERTOS_TASK_READY || newState == LITERTOS_TASK_RESTART){
        ready |= bit;
    }else if(newState == LITERTOS_TASK_BLOCKED){
        blocked |= bit;
    }
    task->state = newState;
    readyMask = ready;
    blockedMask = blocked;
    SREG = sreg;
}

static uint8_t lowestBit(uint8_t m){
    uint8_t i = 0;
    while(!(m & 1U)){
        m = (uint8_t)(m >> 1);
        i++;
    }
    return i;
}

static LiteRTOS_TCB *pickNext(void){
    uint8_t cand = readyMask;
    const uint8_t sleeping = blockedMask;

    if(sleeping != 0){
        const TickType_t now = getTickCountInternal();
        uint8_t m = sleeping;
        while(m != 0){
            const uint8_t i = lowestBit(m);
            m = (uint8_t)(m & (uint8_t)(m - 1));
            const LiteRTOS_TCB *task = taskList[i];
            if((TickType_t)(now - (task -> delayStart)) >= (task -> delayTicks)){
                cand |= (uint8_t)(1U << i);
            }
        }
    }

    if(cand == 0){
        return NULL;
    }

    // 位置 >= start 的先，再繞回去
    const uint8_t start = (uint8_t)(lastRunIndex + 1);
    const uint8_t after = (uint8_t)(cand & (uint8_t)(0xFFU << start));
    uint8_t best;

    if(prioUniform){
        best = lowestBit((after != 0)? after : cand);
    }else{
        UBaseType_t bestPrio = 0;
        bool have = false;
        best = 0;
        for(uint8_t pass = 0; pass < 2; pass++){
            uint8_t m = (pass == 0)? after : (uint8_t)(cand & (uint8_t)~after);
            while(m != 0){
                const uint8_t i = lowestBit(m);
                m = (uint8_t)(m & (uint8_t)(m - 1));
                const UBaseType_t prio = taskList[i] -> priority;
                if(!have || prio > bestPrio){
                    have = true;
                    best = i;
                    bestPrio = prio;
                }
            }
        }
    }

    lastRunIndex = best;
    return taskList[best];
}

static inline void contextSwitch(StackType_t **saveSP, StackType_t *newSP){
    __asm__ __volatile__("" ::: "memory");
    LiteRTOS_portSwitch(saveSP, newSP);
    __asm__ __volatile__("" ::: "memory");
}

static void switchToScheduler(void){
    contextSwitch(&currentTask -> topOfStack, schedulerSP);
}

static void prvResetTask(LiteRTOS_TCB *tcb);
static void reschedule(void){
    LiteRTOS_TCB *self = currentTask;
    if(self == NULL){
        return;
    }
    LiteRTOS_TCB *next = pickNext();
    if(next == NULL){
        switchToScheduler();
        return;
    }
    if(next == self){
        if(self -> state == LITERTOS_TASK_RESTART){
            switchToScheduler();
        }else if(self -> state != LITERTOS_TASK_READY){
            setState(self, LITERTOS_TASK_READY);
        }
        return;
    }
    if(next -> state == LITERTOS_TASK_RESTART){
        prvResetTask(next);
    }
    if(next -> state != LITERTOS_TASK_READY){
        setState(next, LITERTOS_TASK_READY);
    }
    currentTask = next;
    contextSwitch(&self -> topOfStack, next -> topOfStack);
}

static void blockCurrenFor(TickType_t ticks){
    currentTask -> delayStart = getTickCountInternal();
    currentTask -> delayTicks = ticks;
    setState(currentTask, LITERTOS_TASK_BLOCKED);
    reschedule();
}

static void taskEntry(void){
    LiteRTOS_TCB *self = currentTask;
    self -> taskCode(self -> taskParameters);
    setState(self, LITERTOS_TASK_SUSPENDED);
    while(1){
        contextSwitch(&self -> topOfStack, schedulerSP);
    }
}

static void prvResetTask(LiteRTOS_TCB *tcb){
    for(configSTACK_DEPTH_TYPE i = 0; i < tcb->stackDepth; i++){
        tcb->stackBase[i] = LITERTOS_STACK_FILL;
    }
    tcb->delayStart  = 0;
    tcb->delayTicks  = 0;
    tcb->notifyValue = 0;
    setState(tcb, LITERTOS_TASK_READY);
    tcb->topOfStack  = LiteRTOS_portInitStack(&tcb->stackBase[tcb->stackDepth - 1], taskEntry);
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
    if(taskCount > 0 && priority != taskList[0]->priority){
        prioUniform = false;
    }

    tcb->taskCode = taskCode;
    tcb->taskName = taskName;
    tcb->taskParameters = taskParamters;
    tcb->stackBase = stackBuffer;
    tcb->stackDepth = stackDepth;
    tcb->priority = priority;
    tcb->slot = taskCount;
    taskList[taskCount++] = tcb;
    prvResetTask(tcb);
    return tcb;
}

void vTaskStartScheduler(void){
    while(1){
        LiteRTOS_TCB *next = pickNext();
        if(next == NULL){
            continue;
        }

        if(next->state == LITERTOS_TASK_RESTART){
            prvResetTask(next);
        }
        if(next -> state != LITERTOS_TASK_READY){
            setState(next, LITERTOS_TASK_READY);
        }
        currentTask = next;
        contextSwitch(&schedulerSP, next -> topOfStack);
        currentTask = NULL;
    }
}

void vTaskDelay(TickType_t ticksToDelay){
    if(currentTask == NULL){
        return;
    }
    if(ticksToDelay == 0){
        reschedule();
        return;
    }

    if(ticksToDelay == portMAX_DELAY){
        setState(currentTask, LITERTOS_TASK_SUSPENDED);
        reschedule();
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
        reschedule();
    }

    return shouldDely;
}

void vPortYield(void){
    if(currentTask == NULL){
        return;
    }
    reschedule();
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
    //  uxTaskGetStackHighWaterMark.s 實作
    const uint16_t untouched = prvCountUntouchedStack(tcb -> stackBase, (uint16_t)tcb -> stackDepth, LITERTOS_STACK_FILL);

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

void vTaskDelete(TaskHandle_t task){
    if(task == NULL){
        task = currentTask;
    }
    if(task == currentTask){
        setState(currentTask, LITERTOS_TASK_SUSPENDED);
        reschedule();
        return;
    }
    for(UBaseType_t i = 0; i < taskCount; i++){
        if(taskList[i] == task){
            for(UBaseType_t j = i; j + 1 < taskCount; j++){
                taskList[j] = taskList[j+1];
            }
            taskCount--;
            for(UBaseType_t j = i; j < taskCount; j++){
                taskList[j] -> slot = (uint8_t)j;
            }
            {
                const uint8_t sreg = SREG;
                const uint8_t low = (uint8_t)((1U << i) - 1U);
                cli();
                readyMask = (uint8_t)((readyMask & low) | ((readyMask   >> (i + 1)) << i));
                blockedMask = (uint8_t)((blockedMask & low) | ((blockedMask >> (i + 1)) << i));
                SREG = sreg;
            }
            if(lastRunIndex != 0xFF){
                if(i < lastRunIndex){
                    lastRunIndex--;
                }else if(i == lastRunIndex){
                    lastRunIndex = (UBaseType_t)(i - 1);
                }
            }
            return;
        }
    }

}

void vTaskSuspend(TaskHandle_t task){
    if(task == NULL){
        task = currentTask;
    }
    if(task == NULL){
        return;
    }
    setState(task, LITERTOS_TASK_SUSPENDED);

    if(task == currentTask){
        reschedule();
    }
}

void vTaskResume(TaskHandle_t task){
    if(task == NULL || task->state != LITERTOS_TASK_SUSPENDED){
        return;
    }

    task->delayStart = 0;
    task->delayTicks = 0;
    setState(task, LITERTOS_TASK_READY);

}

void xTaskNotifyWait(TaskHandle_t task, uint8_t *val){
    if(task == NULL){
        task = currentTask;
    }
    if(task == NULL){
        return;
    }
    vPortEnterCritical();
    const bool mustWait = (task->notifyValue == 0);
    if(mustWait){
        setState(task, LITERTOS_TASK_WAITING);
    }
    vPortExitCritical();
    if(mustWait){
        reschedule();
    }
    vPortEnterCritical();
    if(val != NULL){
        *val = task->notifyValue;
    }
    task->notifyValue = 0;
    vPortExitCritical();
}

void xTaskNotifyGiveFromISR(TaskHandle_t task, uint8_t bitMask){
    if(task == NULL){
        return;
    }
    task->notifyValue |= bitMask;
    if(task->state == LITERTOS_TASK_WAITING){
        setState(task, LITERTOS_TASK_READY);
    }
}

void vTaskRestart(TaskHandle_t task){
    if(task == NULL){
        task = currentTask;
    }
    if(task == NULL){
        return;
    }
    setState(task, LITERTOS_TASK_RESTART);
    if(task == currentTask){
        reschedule();
    }
}