#ifndef ENCORE_ADSP_H
#define ENCORE_ADSP_H

#include <stdint.h>
#include <stdbool.h>

typedef int32_t (*P2KAdspRxCallback)(int port);
typedef void (*P2KAdspTxCallback)(int port, int32_t data);

enum {
    P2K_ADSP_PC = 1,
    P2K_ADSP_AX0, P2K_ADSP_AX1, P2K_ADSP_AY0, P2K_ADSP_AY1,
    P2K_ADSP_AR, P2K_ADSP_AF,
    P2K_ADSP_MX0, P2K_ADSP_MX1, P2K_ADSP_MY0, P2K_ADSP_MY1,
    P2K_ADSP_MR0, P2K_ADSP_MR1, P2K_ADSP_MR2, P2K_ADSP_MF,
    P2K_ADSP_SI, P2K_ADSP_SE, P2K_ADSP_SB, P2K_ADSP_SR0, P2K_ADSP_SR1,
    P2K_ADSP_I0, P2K_ADSP_I1, P2K_ADSP_I2, P2K_ADSP_I3,
    P2K_ADSP_I4, P2K_ADSP_I5, P2K_ADSP_I6, P2K_ADSP_I7,
    P2K_ADSP_L0, P2K_ADSP_L1, P2K_ADSP_L2, P2K_ADSP_L3,
    P2K_ADSP_L4, P2K_ADSP_L5, P2K_ADSP_L6, P2K_ADSP_L7,
    P2K_ADSP_M0, P2K_ADSP_M1, P2K_ADSP_M2, P2K_ADSP_M3,
    P2K_ADSP_M4, P2K_ADSP_M5, P2K_ADSP_M6, P2K_ADSP_M7,
};

enum {
    P2K_ADSP_IRQ0 = 0,
    P2K_ADSP_IRQ1 = 1,
    P2K_ADSP_IRQ2 = 2,
};

void p2k_adsp2105_init(uint16_t (*data_read)(uint16_t),
                       void (*data_write)(uint16_t, uint16_t),
                       uint32_t (*program_read)(uint16_t),
                       void (*program_write)(uint16_t, uint32_t));
void p2k_adsp2105_reset(void);
int  p2k_adsp2105_execute(int cycles);
void p2k_adsp2105_set_irq_line(int irqline, int state);
uint32_t p2k_adsp2105_get_reg(int regnum);
void p2k_adsp2105_set_reg(int regnum, uint32_t value);
void p2k_adsp2105_set_tx_callback(P2KAdspTxCallback callback);
void p2k_adsp2105_load_boot_data(const uint8_t *source, uint32_t *program);

int      adsp_init(void);
void     adsp_cleanup(void);
void     adsp_host_reset(void);
uint8_t  adsp_flag_byte(void);
uint16_t adsp_read_response(void);
void     adsp_write_cmd(uint16_t command);
uint8_t  adsp_get_echo(void);
void     adsp_set_echo(uint8_t value);
bool     adsp_accepts_boot_byte(void);
uint32_t adsp_bar_read(uint32_t off, int size);
void     adsp_bar_write(uint32_t off, uint32_t value, int size);

#endif /* ENCORE_ADSP_H */
