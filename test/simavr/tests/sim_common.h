#pragma once
/* simavr 測試的共用部分：1 kHz 的 millis()、console 輸出、結束方式。 */
#include <stdint.h>
#include <stdbool.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include "LiteRTOS.h"

static volatile uint32_t g_ms;

ISR(TIMER0_COMPA_vect){
    g_ms++;
}

unsigned long millis(void){
    const uint8_t s = SREG;
    cli();
    const unsigned long m = g_ms;
    SREG = s;
    return m;
}

/* Timer0 CTC：16 MHz / 64 / 250 = 1 kHz */
static __attribute__((unused)) void simInit(void){
    TCCR0A = (1 << WGM01);
    OCR0A = 249;
    TCCR0B = (1 << CS01) | (1 << CS00);
    TIMSK0 = (1 << OCIE0A);
    sei();
}

static __attribute__((unused)) void simPutc(char c){ GPIOR0 = (uint8_t)c; }
static __attribute__((unused)) void simPrint(const char *s){ while (*s){ simPutc(*s++); } }
static __attribute__((unused)) void simPrintU(uint32_t v){
    char buf[11]; uint8_t n = 0;
    if (v == 0){ simPutc('0'); return; }
    while (v){ buf[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n){ simPutc(buf[--n]); }
}
static __attribute__((unused)) void simResult(const char *name, bool ok){
    simPrint(name); simPrint(ok ? " PASS" : " FAIL"); simPutc('\n');
}
/* 中斷關閉時睡眠，simavr 會正常結束 */
static __attribute__((unused)) void simExit(void){
    cli();
    sleep_enable();
    sleep_cpu();
    for (;;){}
}

#define STACK 160
#define TASK(name, prio, fn, arg) \
    static StaticTask_t name##_tcb; static StackType_t name##_stack[STACK]; \
    xTaskCreateStatic(fn, #name, STACK, arg, prio, name##_stack, &name##_tcb)
