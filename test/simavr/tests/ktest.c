/* 以 -DTEST=n 編譯成不同的韌體，每個韌體自我檢查並印出 PASS / FAIL。 */
#include "sim_common.h"
#include <stdbool.h>

/* ---------------------------------------------------------------- 1 交替執行 */
#if TEST == 1
static volatile uint16_t a, b; static char order[16]; static volatile uint8_t oi;
static void logRun(char c){ if (oi < 12) order[oi++] = c; }
static void ta(void *arg){ (void)arg; for (;;){ a++; logRun('A'); vTaskDelay(10); } }
static void tb(void *arg){ (void)arg; for (;;){ b++; logRun('B'); vTaskDelay(10); } }
static void tc(void *arg){ (void)arg;
    vTaskDelay(1000);
    order[12] = 0;
    simPrint("order="); simPrint(order); simPrint(" a="); simPrintU(a); simPrint(" b="); simPrintU(b); simPutc('\n');
    simResult("T1 alternate", a >= 98 && a <= 101 && b >= 98 && b <= 101 &&
              order[0]=='A' && order[1]=='B' && order[2]=='A' && order[3]=='B' && order[4]=='A' && order[5]=='B');
    simExit();
}
int main(void){ simInit(); TASK(A,1,ta,0); TASK(B,1,tb,0); TASK(C,1,tc,0); vTaskStartScheduler(); }

/* ---------------------------------------------------------------- 2 優先權順序 */
#elif TEST == 2
static char order[16]; static volatile uint8_t oi;
static void lo(void *arg){ (void)arg; for (;;){ if (oi < 12) order[oi++] = 'L'; vTaskDelay(10); } }
static void hi(void *arg){ (void)arg; for (;;){ if (oi < 12) order[oi++] = 'H'; vTaskDelay(10); } }
static void tc(void *arg){ (void)arg;
    vTaskDelay(100);
    order[12] = 0; simPrint("order="); simPrint(order); simPutc('\n');
    bool ok = true;
    for (uint8_t i = 0; i < 12; i += 2){ if (order[i] != 'H' || order[i+1] != 'L') ok = false; }
    simResult("T2 priority order", ok);
    simExit();
}
/* LO 先建立、HI 後建立：HI 仍必須先跑 */
int main(void){ simInit(); TASK(LO,1,lo,0); TASK(HI,3,hi,0); TASK(C,5,tc,0); vTaskStartScheduler(); }

/* ---------------------------------------------------------------- 3 taskYIELD */
#elif TEST == 3
static volatile uint32_t a, b, lo_count;
static void ta(void *arg){ (void)arg; for (;;){ a++; taskYIELD(); } }
static void tb(void *arg){ (void)arg; for (;;){ b++; taskYIELD(); } }
static void low(void *arg){ (void)arg; for (;;){ lo_count++; taskYIELD(); } }
static void tc(void *arg){ (void)arg;
    vTaskDelay(100);
    simPrint("a="); simPrintU(a); simPrint(" b="); simPrintU(b); simPrint(" low="); simPrintU(lo_count); simPutc('\n');
    const uint32_t d = a > b ? a - b : b - a;
    simResult("T3a yield round-robin (equal priority)", a > 500 && b > 500 && d <= 1);
    /* 較低優先權的 task 在較高優先權 task 一直 ready 時不會被排到，這與 FreeRTOS 相同 */
    simResult("T3b yield never runs lower priority", lo_count == 0);
    simExit();
}
int main(void){ simInit(); TASK(A,2,ta,0); TASK(B,2,tb,0); TASK(L,1,low,0); TASK(C,3,tc,0); vTaskStartScheduler(); }

/* ---------------------------------------------------------------- 4 portMAX_DELAY */
#elif TEST == 4
static volatile uint8_t x, after;
static void tx(void *arg){ (void)arg; x++; vTaskDelay(portMAX_DELAY); after++; }
static void tc(void *arg){ (void)arg;
    vTaskDelay(200);
    simPrint("x="); simPrintU(x); simPrint(" after="); simPrintU(after); simPutc('\n');
    simResult("T4 portMAX_DELAY blocks forever", x == 1 && after == 0);
    simExit();
}
int main(void){ simInit(); TASK(X,2,tx,0); TASK(C,1,tc,0); vTaskStartScheduler(); }

/* ---------------------------------------------------------------- 5 xTaskDelayUntil */
#elif TEST == 5
static volatile uint16_t n; static volatile TickType_t pStart;
static volatile int8_t r1v, r2v, r3v; static volatile TickType_t t0, tAfter3, lastAfter2;
static void tp(void *arg){ (void)arg;
    TickType_t last = xTaskGetTickCount(); pStart = last;
    for (;;){ xTaskDelayUntil(&last, 10); n++; }
}
static void burn(uint16_t ms){ const TickType_t s = xTaskGetTickCount(); while ((TickType_t)(xTaskGetTickCount() - s) < ms){} }
static void tover(void *arg){ (void)arg;
    TickType_t last = xTaskGetTickCount(); t0 = last;
    burn(25);                                  /* 超時：已經錯過 start+10 */
    r1v = xTaskDelayUntil(&last, 10);          /* 期限 start+10 已過 -> pdFALSE */
    r2v = xTaskDelayUntil(&last, 10);          /* 期限 start+20 已過 -> pdFALSE */
    lastAfter2 = last;
    r3v = xTaskDelayUntil(&last, 10);          /* 期限 start+30 在未來 -> pdTRUE，等到 start+30 */
    tAfter3 = xTaskGetTickCount();
    for (;;){ vTaskDelay(portMAX_DELAY); }
}
static void tc(void *arg){ (void)arg;
    vTaskDelay(1000);
    const TickType_t nowT = xTaskGetTickCount();
    const uint16_t expected = (uint16_t)((TickType_t)(nowT - pStart) / 10);
    simPrint("expected="); simPrintU(expected); simPrint(" n="); simPrintU(n); simPrint(" r="); simPrintU((uint8_t)r1v); simPrintU((uint8_t)r2v); simPrintU((uint8_t)r3v);
    simPrint(" wake3-start="); simPrintU((TickType_t)(tAfter3 - t0)); simPutc('\n');
    simResult("T5a DelayUntil period (no drift)", n + 1 >= expected && n <= expected + 1 && expected >= 90);
    simResult("T5b DelayUntil missed deadline returns pdFALSE then catches up",
              r1v == pdFALSE && r2v == pdFALSE && r3v == pdTRUE &&
              (TickType_t)(lastAfter2 - t0) == 20 && (TickType_t)(tAfter3 - t0) >= 30 && (TickType_t)(tAfter3 - t0) <= 31);
    simExit();
}
int main(void){ simInit(); TASK(P,2,tp,0); TASK(O,2,tover,0); TASK(C,1,tc,0); vTaskStartScheduler(); }

/* ---------------------------------------------------------------- 6 暫存器保存 */
#elif TEST == 6
/* 在 asm 裡把 r2–r17、r28、r29 填成 seed+i，呼叫真正的 yield（兩個 task 會互相覆蓋暫存器），
 * 回來後把這 18 個暫存器寫到 out[]，由 C 檢查。 */
void regtest(uint8_t seed, uint8_t *out) __attribute__((naked, noinline));
void regtest(uint8_t seed, uint8_t *out){
    (void)seed; (void)out;
    __asm__ __volatile__(
        "push r2 \n push r3 \n push r4 \n push r5 \n push r6 \n push r7 \n push r8 \n push r9 \n"
        "push r10 \n push r11 \n push r12 \n push r13 \n push r14 \n push r15 \n push r16 \n push r17 \n"
        "push r28 \n push r29 \n"
        "push r22 \n push r23 \n"
        "mov r2, r24 \n subi r24, 0xFF \n mov r3, r24 \n subi r24, 0xFF \n mov r4, r24 \n subi r24, 0xFF \n"
        "mov r5, r24 \n subi r24, 0xFF \n mov r6, r24 \n subi r24, 0xFF \n mov r7, r24 \n subi r24, 0xFF \n"
        "mov r8, r24 \n subi r24, 0xFF \n mov r9, r24 \n subi r24, 0xFF \n mov r10, r24 \n subi r24, 0xFF \n"
        "mov r11, r24 \n subi r24, 0xFF \n mov r12, r24 \n subi r24, 0xFF \n mov r13, r24 \n subi r24, 0xFF \n"
        "mov r14, r24 \n subi r24, 0xFF \n mov r15, r24 \n subi r24, 0xFF \n mov r16, r24 \n subi r24, 0xFF \n"
        "mov r17, r24 \n subi r24, 0xFF \n mov r28, r24 \n subi r24, 0xFF \n mov r29, r24 \n"
        "call vPortYield \n"
        "pop r31 \n pop r30 \n"
        "st Z+, r2 \n st Z+, r3 \n st Z+, r4 \n st Z+, r5 \n st Z+, r6 \n st Z+, r7 \n st Z+, r8 \n st Z+, r9 \n"
        "st Z+, r10 \n st Z+, r11 \n st Z+, r12 \n st Z+, r13 \n st Z+, r14 \n st Z+, r15 \n st Z+, r16 \n st Z+, r17 \n"
        "st Z+, r28 \n st Z+, r29 \n"
        "pop r29 \n pop r28 \n"
        "pop r17 \n pop r16 \n pop r15 \n pop r14 \n pop r13 \n pop r12 \n pop r11 \n pop r10 \n"
        "pop r9 \n pop r8 \n pop r7 \n pop r6 \n pop r5 \n pop r4 \n pop r3 \n pop r2 \n"
        "ret \n");
}
static volatile uint16_t badA, badB, roundsA, roundsB;
static void body(uint8_t seed, volatile uint16_t *bad, volatile uint16_t *rounds){
    for (uint8_t r = 0; r < 200; ++r){
        uint8_t out[18];
        regtest((uint8_t)(seed + r), out);
        for (uint8_t i = 0; i < 18; ++i){ if (out[i] != (uint8_t)(seed + r + i)) (*bad)++; }
        (*rounds)++;
    }
}
static void ta(void *arg){ (void)arg; body(0x10, &badA, &roundsA); for (;;){ vTaskDelay(portMAX_DELAY); } }
static void tb(void *arg){ (void)arg; body(0x80, &badB, &roundsB); for (;;){ vTaskDelay(portMAX_DELAY); } }
static void tc(void *arg){ (void)arg;
    vTaskDelay(300);
    simPrint("rounds="); simPrintU(roundsA); simPrint("/"); simPrintU(roundsB);
    simPrint(" bad="); simPrintU(badA); simPrint("/"); simPrintU(badB); simPutc('\n');
    simResult("T6 callee-saved registers preserved across switches", roundsA == 200 && roundsB == 200 && badA == 0 && badB == 0);
    simExit();
}
int main(void){ simInit(); TASK(A,1,ta,0); TASK(B,1,tb,0); TASK(C,2,tc,0); vTaskStartScheduler(); }

/* ---------------------------------------------------------------- 7 臨界區巢狀 */
#elif TEST == 7
static volatile uint8_t s1, s2, s3, s4, s5;
static void tt(void *arg){ (void)arg;
    sei();
    taskENTER_CRITICAL(); s1 = SREG & 0x80;
    taskENTER_CRITICAL(); s2 = SREG & 0x80;
    taskEXIT_CRITICAL();  s3 = SREG & 0x80;      /* 內層離開：仍在外層臨界區內，必須是關的 */
    taskEXIT_CRITICAL();  s4 = SREG & 0x80;      /* 最外層離開：還原為開 */
    cli();                                       /* 進入前中斷就是關的 */
    taskENTER_CRITICAL(); taskEXIT_CRITICAL();
    s5 = SREG & 0x80;                            /* 必須仍是關的 */
    sei();
    simPrint("s="); simPrintU(s1?1:0); simPrintU(s2?1:0); simPrintU(s3?1:0); simPrintU(s4?1:0); simPrintU(s5?1:0); simPutc('\n');
    simResult("T7 nested critical sections", !s1 && !s2 && !s3 && s4 && !s5);
    simExit();
}
int main(void){ simInit(); TASK(T,1,tt,0); vTaskStartScheduler(); }

/* ---------------------------------------------------------------- 8 stack 水位 */
#elif TEST == 8
static volatile UBaseType_t hwm0, hwm1;
static __attribute__((noinline)) uint8_t deep(void){
    volatile uint8_t buf[48];
    for (uint8_t i = 0; i < 48; ++i) buf[i] = i;
    uint8_t s = 0; for (uint8_t i = 0; i < 48; ++i) s += buf[i];
    return s;
}
static volatile uint8_t sink;
static void tt(void *arg){ (void)arg;
    hwm0 = uxTaskGetStackHighWaterMark(0);
    sink = deep();
    hwm1 = uxTaskGetStackHighWaterMark(0);
    simPrint("stack="); simPrintU(STACK); simPrint(" hwm before="); simPrintU(hwm0); simPrint(" after="); simPrintU(hwm1); simPutc('\n');
    const uint16_t used = STACK - hwm1;          /* deep() 用了 48 + 2(r28,r29) + 2(返回位址) = 52 byte */
    simPrint("used="); simPrintU(used); simPutc('\n');
    simResult("T8 stack high-water mark tracks usage", hwm1 < hwm0 && used >= 52 && used <= 70 && hwm1 > 0);
    simExit();
}
int main(void){ simInit(); TASK(T,1,tt,0); vTaskStartScheduler(); }

/* ---------------------------------------------------------------- 9 task 返回、建立上限 */
#elif TEST == 9
#include "LiteRTOS_internal.h"   /* 取得 LITERTOS_MAX_TASKS */
static volatile uint8_t ran, other;
static void once(void *arg){ (void)arg; ran++; }                    /* 直接返回 */
static void alive(void *arg){ (void)arg; for (;;){ other++; vTaskDelay(10); } }
static StaticTask_t extra_tcb[LITERTOS_MAX_TASKS + 1]; static StackType_t extra_stack[LITERTOS_MAX_TASKS + 1][STACK];
static void tc(void *arg){ (void)arg;
    vTaskDelay(200);
    simPrint("ran="); simPrintU(ran); simPrint(" other="); simPrintU(other); simPutc('\n');
    simResult("T9a returning task ends cleanly, others keep running", ran == 1 && other >= 15);
    simExit();
}
int main(void){
    simInit();
    TASK(R,1,once,0); TASK(O,1,alive,0); TASK(C,2,tc,0);
    bool ok = true;
    /* 已有 3 個，再建立 1 個應成功，第 5 個必須失敗；另外檢查錯誤參數 */
    ok &= xTaskCreateStatic(alive, "e0", STACK, 0, 1, extra_stack[0], &extra_tcb[0]) != 0;
    ok &= xTaskCreateStatic(alive, "e1", STACK, 0, 1, extra_stack[1], &extra_tcb[1]) == 0;
    ok &= xTaskCreateStatic(0,     "e2", STACK, 0, 1, extra_stack[2], &extra_tcb[2]) == 0;
    ok &= xTaskCreateStatic(alive, "e3", 10,    0, 1, extra_stack[3], &extra_tcb[3]) == 0;
    simResult("T9b creation limits and argument checks", ok);
    vTaskStartScheduler();
}

/* ---------------------------------------------------------------- 10 task handle */
#elif TEST == 10
static TaskHandle_t created;
static void tt(void *arg){ (void)arg;
    const bool same = (xTaskGetCurrentTaskHandle() == created) && created != 0;
    const bool hwm  = (uxTaskGetStackHighWaterMark(created) == uxTaskGetStackHighWaterMark(0));
    simResult("T10 current task handle and HWM(NULL) == HWM(self)", same && hwm);
    simExit();
}
static StaticTask_t t_tcb; static StackType_t t_stack[STACK];
int main(void){
    simInit();
    created = xTaskCreateStatic(tt, "T", STACK, 0, 1, t_stack, &t_tcb);
    if (xTaskGetCurrentTaskHandle() != 0){ simResult("T10 handle is NULL before scheduler", false); simExit(); }
    vTaskStartScheduler();
}

/* ---------------------------------------------------------------- 11 vTaskSuspend / vTaskResume */
#elif TEST == 11
static volatile uint16_t cnt, sx;
static TaskHandle_t ha;
static StaticTask_t a_tcb, s_tcb; static StackType_t a_stack[STACK], s_stack[STACK];
static void ta(void *arg){ (void)arg; for (;;){ cnt++; vTaskDelay(10); } }
static void ts(void *arg){ (void)arg; sx++; vTaskSuspend(0); sx++; for (;;){ vTaskDelay(portMAX_DELAY); } }   /* 暫停自己 */
static void tc(void *arg){ (void)arg;
    vTaskDelay(100);
    const bool runs = cnt >= 8;
    const bool selfSuspended = (sx == 1);
    vTaskSuspend(ha); const uint16_t s = cnt; vTaskDelay(100);
    const bool stopped = (cnt == s);
    vTaskResume(ha);
    vTaskDelay(100);
    const bool again = cnt >= s + 8;
    simPrint("cnt="); simPrintU(cnt); simPrint(" stopped_at="); simPrintU(s); simPrint(" sx="); simPrintU(sx); simPutc('\n');
    simResult("T11a suspend stops a task, resume restarts it", runs && stopped && again);
    simResult("T11b a task can suspend itself", selfSuspended);
    simExit();
}
int main(void){
    simInit();
    ha = xTaskCreateStatic(ta, "A", STACK, 0, 1, a_stack, &a_tcb);
    xTaskCreateStatic(ts, "S", STACK, 0, 1, s_stack, &s_tcb);
    TASK(C,2,tc,0);
    vTaskStartScheduler();
}

/* ---------------------------------------------------------------- 12 vTaskDelete */
#elif TEST == 12
static volatile uint16_t ca, cb, cd;
static TaskHandle_t ha;
static StaticTask_t a_tcb; static StackType_t a_stack[STACK];
static void ta(void *arg){ (void)arg; for (;;){ ca++; vTaskDelay(10); } }
static void tb(void *arg){ (void)arg; for (;;){ cb++; vTaskDelay(10); } }
static void td(void *arg){ (void)arg; cd++; vTaskDelete(0); cd += 100; }     /* 刪自己，後面不該執行 */
static void tc(void *arg){ (void)arg;
    vTaskDelay(100);
    const bool runs = ca >= 8;
    vTaskDelete(ha); const uint16_t a0 = ca, b0 = cb; vTaskDelay(100);
    simPrint("ca="); simPrintU(ca); simPrint(" a0="); simPrintU(a0); simPrint(" cb="); simPrintU(cb); simPrint(" cd="); simPrintU(cd); simPutc('\n');
    simResult("T12a deleting another task stops it, others keep running", runs && ca == a0 && cb >= b0 + 8);
    simResult("T12b a task can delete itself", cd == 1);
    simExit();
}
int main(void){
    simInit();
    ha = xTaskCreateStatic(ta, "A", STACK, 0, 1, a_stack, &a_tcb);
    TASK(B,1,tb,0); TASK(D,1,td,0); TASK(C,2,tc,0);
    vTaskStartScheduler();
}

/* ---------------------------------------------------------------- 13 刪除後輪流順序不跳過 */
#elif TEST == 13
static char order[16]; static volatile uint8_t oi, rn;
static TaskHandle_t hp;
static StaticTask_t p_tcb; static StackType_t p_stack[STACK];
static void logRun(char c){ if (oi < 12) order[oi++] = c; }
static void tp(void *arg){ (void)arg; for (;;){ logRun('P'); taskYIELD(); } }
static void tq(void *arg){ (void)arg; for (;;){ logRun('Q'); taskYIELD(); } }
static void tr(void *arg){ (void)arg; for (;;){ logRun('R'); if (++rn == 2){ vTaskDelete(hp); } taskYIELD(); } }
static void ts(void *arg){ (void)arg; for (;;){
    logRun('S');
    if (oi >= 11){
        order[11] = 0; simPrint("order="); simPrint(order); simPutc('\n');
        /* R（第 3 個）刪掉排在它前面的 P 之後，下一個必須是 S，而不是從頭的 Q */
        simResult("T13 round-robin order survives deleting an earlier task", order[0]=='P' && order[1]=='Q' && order[2]=='R' && order[3]=='S' &&
                  order[4]=='P' && order[5]=='Q' && order[6]=='R' && order[7]=='S' && order[8]=='Q' && order[9]=='R' && order[10]=='S');
        simExit();
    }
    taskYIELD(); } }
int main(void){
    simInit();
    hp = xTaskCreateStatic(tp, "P", STACK, 0, 1, p_stack, &p_tcb);
    TASK(Q,1,tq,0); TASK(R,1,tr,0); TASK(S,1,ts,0);
    vTaskStartScheduler();
}

/* ---------------------------------------------------------------- 14 vTaskResume 不會提早叫醒延遲中的 task */
#elif TEST == 14
static volatile TickType_t wokeAt;
static TaskHandle_t ht;
static StaticTask_t t_tcb; static StackType_t t_stack[STACK];
static void tt(void *arg){ (void)arg; vTaskDelay(200); wokeAt = xTaskGetTickCount(); for (;;){ vTaskDelay(portMAX_DELAY); } }
static void tc(void *arg){ (void)arg;
    vTaskDelay(50);
    vTaskResume(ht);                        /* 它沒有被暫停，什麼事都不該發生 */
    vTaskDelay(300);
    simPrint("wokeAt="); simPrintU(wokeAt); simPutc('\n');
    simResult("T14 vTaskResume on a non-suspended task is a no-op", wokeAt >= 195 && wokeAt <= 215);
    simExit();
}
int main(void){
    simInit();
    ht = xTaskCreateStatic(tt, "T", STACK, 0, 1, t_stack, &t_tcb);
    TASK(C,1,tc,0);
    vTaskStartScheduler();
}
#elif TEST == 15
/* portENTER_CRITICAL / portEXIT_CRITICAL：關中斷、巢狀、還原進入前的 SREG（含 I 旗標）。 */
static void tc(void *arg){ (void)arg;
    const uint8_t sp0 = SPL;
    bool ok = true;
    sei();
    portENTER_CRITICAL();
    ok &= !(SREG & (1 << SREG_I));
    portENTER_CRITICAL();
    ok &= !(SREG & (1 << SREG_I));
    portEXIT_CRITICAL();
    ok &= !(SREG & (1 << SREG_I));          /* 內層結束，外層仍在臨界區 */
    portEXIT_CRITICAL();
    ok &= (SREG & (1 << SREG_I)) != 0;      /* 外層結束，I 旗標回來 */
    ok &= (SPL == sp0);                      /* push/pop 成對，stack 沒有漏 */
    cli();                                   /* 進入前 I = 0 的情形：結束後仍應為 0 */
    portENTER_CRITICAL();
    portEXIT_CRITICAL();
    ok &= !(SREG & (1 << SREG_I));
    sei();
    simResult("T15 portENTER/EXIT_CRITICAL nest and restore SREG", ok);
    simExit();
}
int main(void){
    simInit();
    TASK(C,1,tc,0);
    vTaskStartScheduler();
}
#endif
