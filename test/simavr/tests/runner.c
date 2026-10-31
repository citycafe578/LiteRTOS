/* 以 libsimavr 載入韌體並執行；把對 GPIOR0 的寫入當成 console 輸出。
 * 結束條件：韌體在中斷關閉下睡眠（simavr 視為 cpu_Done），或超過模擬時間上限。 */
#include <stdio.h>
#include <stdlib.h>
#include <simavr/sim_avr.h>
#include <simavr/sim_elf.h>
#include <simavr/sim_io.h>

#define GPIOR0_ADDR 0x3E            /* ATmega328P 的 GPIOR0，資料空間位址 */
#define FREQ        16000000UL
#define MAX_SECONDS 30

static void console_write(struct avr_t *avr, avr_io_addr_t addr, uint8_t v, void *param){
    (void)avr; (void)addr; (void)param;
    putchar(v);
}

int main(int argc, char **argv){
    if (argc < 2){ fprintf(stderr, "usage: %s firmware.elf\n", argv[0]); return 2; }

    elf_firmware_t fw;
    if (elf_read_firmware(argv[1], &fw) != 0){ fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }

    avr_t *avr = avr_make_mcu_by_name("atmega328p");
    if (!avr){ fprintf(stderr, "no such mcu\n"); return 2; }
    avr_init(avr);
    avr->frequency = FREQ;
    avr_load_firmware(avr, &fw);
    avr_register_io_write(avr, GPIOR0_ADDR, console_write, NULL);

    int state = cpu_Running;
    while (state != cpu_Done && state != cpu_Crashed){
        state = avr_run(avr);
        if (avr->cycle > (avr_cycle_count_t)FREQ * MAX_SECONDS){
            printf("TIMEOUT (simulated %d s)\n", MAX_SECONDS);
            fflush(stdout);
            return 3;
        }
    }
    fflush(stdout);
    if (state == cpu_Crashed){ printf("CRASHED\n"); return 4; }
    return 0;
}
