#ifndef LITE_RTOS_H
#define LITE_RTOS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

    typedef int8_t   BaseType_t;              // 8-bit AVR
    typedef uint8_t  UBaseType_t;             // priority, counter
    typedef uint8_t  StackType_t;
    typedef uint16_t configSTACK_DEPTH_TYPE;
    typedef uint16_t TickType_t;

    typedef void (*TaskFunction_t)(void *);
    struct LiteRTOS_TCB;
    typedef struct LiteRTOS_TCB * TaskHandle_t;

    #ifndef LITERTOS_TCB_SIZE
    #define LITERTOS_TCB_SIZE 18              // AVR TCB size 
    #endif

    typedef struct {
        uint8_t storage[LITERTOS_TCB_SIZE];
    } StaticTask_t;

    #ifndef configTICK_RATE_HZ
    #define configTICK_RATE_HZ 1000U          // 1ms
    #endif

    #define pdTRUE  ((BaseType_t)1)
    #define pdFALSE ((BaseType_t)0)
    #define pdPASS  pdTRUE
    #define pdFAIL  pdFALSE

    #define portMAX_DELAY      ((TickType_t)UINT16_MAX)
    #define portBYTE_ALIGNMENT 1
    #define tskIDLE_PRIORITY   ((UBaseType_t)0U)

    #define pdMS_TO_TICKS(milliseconds) \
        ((TickType_t)(((uint32_t)(milliseconds) * (uint32_t)configTICK_RATE_HZ) / 1000U))

    TaskHandle_t xTaskCreateStatic(
        TaskFunction_t taskCode,
        const char *const taskName,
        configSTACK_DEPTH_TYPE stackDepth,
        void *taskParameters,
        UBaseType_t priority,
        StackType_t *stackBuffer,
        StaticTask_t *taskBuffer
    );

    void vTaskStartScheduler(void);
    void vTaskDelay(TickType_t ticksToDelay);
    BaseType_t xTaskDelayUntil(
        TickType_t *previousWakeTime,
        TickType_t timeIncrement
    );

    void vPortYield(void);
    #define portYIELD() vPortYield()
    #define taskYIELD() portYIELD()

    TickType_t xTaskGetTickCount(void);
    TaskHandle_t xTaskGetCurrentTaskHandle(void);
    UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task);
    
    void vPortEnterCritical(void);
    void vPortExitCritical(void);
    #define taskENTER_CRITICAL() vPortEnterCritical()
    #define taskEXIT_CRITICAL()  vPortExitCritical()

    #define portENTER_CRITICAL()                        \
        __asm__ __volatile__(                           \
            "in   __tmp_reg__, __SREG__ \n\t"           \
            "cli                        \n\t"           \
            "push __tmp_reg__           \n\t"           \
            ::: "memory"                                \
        )

    #define portEXIT_CRITICAL()                         \
        __asm__ __volatile__(                           \
            "pop  __tmp_reg__           \n\t"           \
            "out  __SREG__, __tmp_reg__ \n\t"           \
            ::: "memory"                                \
        )
    
    #define LITERTOS_UNSUPPORTED(api) \
        __attribute__((error("LiteRTOS: " api " 不支援，見 docs/COMPAT.md")))

    BaseType_t xTaskCreate(
        TaskFunction_t taskCode,
        const char * const taskName,
        configSTACK_DEPTH_TYPE stackDepth,
        void * const taskParameters,
        UBaseType_t priority,
        TaskHandle_t * const createdTask) LITERTOS_UNSUPPORTED("xTaskCreate");
        
    void vTaskDelayUntil(
        TickType_t * const previousWakeTime,
        const TickType_t timeIncrement) LITERTOS_UNSUPPORTED("vTaskDelayUntil");
        
    void vTaskPrioritySet(
        TaskHandle_t task,
        UBaseType_t newPriority) LITERTOS_UNSUPPORTED("vTaskPrioritySet");
        
    UBaseType_t uxTaskPriorityGet(
        const TaskHandle_t task) LITERTOS_UNSUPPORTED("uxTaskPriorityGet");

    void vTaskDelete(TaskHandle_t task);
    void vTaskSuspend(TaskHandle_t task);
    void vTaskResume(TaskHandle_t task);

#ifdef __cplusplus
}
#endif

#endif // LITE_RTOS_H