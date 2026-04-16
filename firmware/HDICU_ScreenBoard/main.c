/**
 * @file    main.c
 * @brief   HDICU-ZKB01A Screen Board Firmware — GD32F303RCT6
 *
 * Hardware:
 *   MCU: GD32F303RCT6 (Cortex-M4, compatible with STM32F303)
 *   Crystal: 8MHz HSE
 *   LED Driver: 2× TM1640 (DIN/SCLK serial)
 *   Keys: 9 independent buttons via XH2.54 connectors (CN1-CN9)
 *     CN1=KEY1(PB12) CN3=KEY2(PB13) CN5=KEY3(PB14) CN7=KEY4(PB15)
 *     CN2=KEY5(PC6)  CN4=KEY6(PC7)  CN6=KEY7(PC8)  CN8=KEY8(PC9)
 *     CN9=KEY9(PA8)
 *   Encoder: Rotary encoder (A=PB2, B=PA6, Push=PA7)
 *   Comm: UART2(PA2/PA3) via CN12 (3.3V direct) — primary TX/RX to main board
 *         UART1(PA9/PA10) via CN11 — reserved (level shifter issue)
 *
 * Architecture: Bare-metal main loop + SysTick (no RTOS needed)
 *   - UART RX interrupt → ring buffer (both UART1 & UART2)
 *   - SysTick 1ms tick
 *   - Main loop: key scan(20ms), display update(100ms), heartbeat(1s)
 */

#include <stdint.h>
#include <string.h>

/* ========================================================================= */
/*  Register Definitions (GD32F303 / STM32F303 compatible)                   */
/* ========================================================================= */

/* RCC */
#define RCC_BASE            0x40021000
#define RCC_CR              (*(volatile uint32_t *)(RCC_BASE + 0x00))
#define RCC_CFGR            (*(volatile uint32_t *)(RCC_BASE + 0x04))
#define RCC_APB2ENR         (*(volatile uint32_t *)(RCC_BASE + 0x18))
#define RCC_APB1ENR         (*(volatile uint32_t *)(RCC_BASE + 0x1C))
#define RCC_AHBENR          (*(volatile uint32_t *)(RCC_BASE + 0x14))

/* GPIO Port A (GD32F30x: CTL0/CTL1 style, same offsets as STM32F10x) */
#define GPIOA_BASE          0x40010800
#define GPIOA_CRL           (*(volatile uint32_t *)(GPIOA_BASE + 0x00))  /* CTL0 */
#define GPIOA_CRH           (*(volatile uint32_t *)(GPIOA_BASE + 0x04))  /* CTL1 */
#define GPIOA_IDR           (*(volatile uint32_t *)(GPIOA_BASE + 0x08))  /* ISTAT */
#define GPIOA_ODR           (*(volatile uint32_t *)(GPIOA_BASE + 0x0C))  /* OCTL */
#define GPIOA_BSRR          (*(volatile uint32_t *)(GPIOA_BASE + 0x10))  /* BOP */

/* GPIO Port B */
#define GPIOB_BASE          0x40010C00
#define GPIOB_CRL           (*(volatile uint32_t *)(GPIOB_BASE + 0x00))
#define GPIOB_ODR           (*(volatile uint32_t *)(GPIOB_BASE + 0x0C))
#define GPIOB_BSRR          (*(volatile uint32_t *)(GPIOB_BASE + 0x10))

/* GPIO Port B — additional registers */
#define GPIOB_CRH           (*(volatile uint32_t *)(GPIOB_BASE + 0x04))
#define GPIOB_IDR           (*(volatile uint32_t *)(GPIOB_BASE + 0x08))

/* GPIO Port C */
#define GPIOC_BASE          0x40011000
#define GPIOC_CRL           (*(volatile uint32_t *)(GPIOC_BASE + 0x00))
#define GPIOC_CRH           (*(volatile uint32_t *)(GPIOC_BASE + 0x04))
#define GPIOC_IDR           (*(volatile uint32_t *)(GPIOC_BASE + 0x08))
#define GPIOC_ODR           (*(volatile uint32_t *)(GPIOC_BASE + 0x0C))
#define GPIOC_BSRR          (*(volatile uint32_t *)(GPIOC_BASE + 0x10))

/* GD32 USART0 = board UART1 (PA9/PA10, CN11 to main board)
 * GD32 names it USART0; STM32 names it USART1. Same address block. */
#define USART1_BASE         0x40013800  /* GD32: USART0 */
#define USART1_SR           (*(volatile uint32_t *)(USART1_BASE + 0x00))
#define USART1_DR           (*(volatile uint32_t *)(USART1_BASE + 0x04))
#define USART1_BRR          (*(volatile uint32_t *)(USART1_BASE + 0x08))
#define USART1_CR1          (*(volatile uint32_t *)(USART1_BASE + 0x0C))
#define USART1_CR2          (*(volatile uint32_t *)(USART1_BASE + 0x10))
#define USART1_CR3          (*(volatile uint32_t *)(USART1_BASE + 0x14))

/* GD32 USART1 = board UART2 (PA2/PA3, CN12 debug/3.3V) */
#define USART2_BASE         0x40004400  /* GD32: USART1 / STM32: USART2 */
#define USART2_SR           (*(volatile uint32_t *)(USART2_BASE + 0x00))
#define USART2_DR           (*(volatile uint32_t *)(USART2_BASE + 0x04))
#define USART2_BRR          (*(volatile uint32_t *)(USART2_BASE + 0x08))
#define USART2_CR1          (*(volatile uint32_t *)(USART2_BASE + 0x0C))
#define USART2_CR2          (*(volatile uint32_t *)(USART2_BASE + 0x10))
#define USART2_CR3          (*(volatile uint32_t *)(USART2_BASE + 0x14))

/* SysTick */
#define SYSTICK_BASE        0xE000E010
#define SYST_CSR            (*(volatile uint32_t *)(SYSTICK_BASE + 0x00))
#define SYST_RVR            (*(volatile uint32_t *)(SYSTICK_BASE + 0x04))
#define SYST_CVR            (*(volatile uint32_t *)(SYSTICK_BASE + 0x08))

/* NVIC */
#define NVIC_ISER1          (*(volatile uint32_t *)0xE000E104)  /* IRQ 32-63 */

/* AFIO — needed to release PB3/PB4 from JTAG */
#define AFIO_BASE           0x40010000
#define AFIO_MAPR           (*(volatile uint32_t *)(AFIO_BASE + 0x04))

/* IWDG (Independent Watchdog) — same register layout as STM32F10x */
#define IWDG_BASE           0x40003000
#define IWDG_KR             (*(volatile uint32_t *)(IWDG_BASE + 0x00))
#define IWDG_PR             (*(volatile uint32_t *)(IWDG_BASE + 0x04))
#define IWDG_RLR            (*(volatile uint32_t *)(IWDG_BASE + 0x08))
#define IWDG_SR             (*(volatile uint32_t *)(IWDG_BASE + 0x0C))

/* ========================================================================= */
/*  IWDG — ~2s timeout watchdog (P2 fix)                                     */
/* ========================================================================= */

static void IWDG_Init(void)
{
    IWDG_KR  = 0x5555;     /* Enable register write */
    IWDG_PR  = 4;          /* Prescaler /64: 40kHz/64 = 625Hz */
    IWDG_RLR = 1250;       /* Reload: 1250/625 = 2.0s timeout */
    /* Note: do NOT poll IWDG_SR — GD32F303 FWDGT_STAT may not clear as expected.
     * PR/RLR values propagate within ~5 LSI cycles (~125µs), safe to proceed. */
    IWDG_KR  = 0xCCCC;     /* Start IWDG */
}

static void IWDG_Feed(void)
{
    IWDG_KR = 0xAAAA;      /* Reload counter */
}

/* ========================================================================= */
/*  Global State                                                             */
/* ========================================================================= */

static volatile uint32_t s_tick_ms;         /* SysTick millisecond counter */

/* UART RX ring buffer */
#define UART_RX_BUF_SIZE    128
static volatile uint8_t  s_rx_buf[UART_RX_BUF_SIZE];
static volatile uint16_t s_rx_head;
static volatile uint16_t s_rx_tail;

/* Display data from main board (0x01 packet, 26 bytes) */
static uint8_t s_display_data[26];
static uint8_t s_display_valid;

typedef enum {
    HMI_PAGE_LIVE = 0,
    HMI_PAGE_SET_TEMP,
    HMI_PAGE_SET_HUMID,
    HMI_PAGE_SET_O2,
} HmiPage_t;

static HmiPage_t s_hmi_page = HMI_PAGE_LIVE;
static uint8_t   s_hmi_shadow_valid;
static uint32_t  s_hmi_last_input_tick;
static int16_t   s_set_temp_x10 = 360;
static uint16_t  s_set_hum_x10  = 500;
static uint16_t  s_set_o2_x10   = 210;

#define HMI_EDIT_TIMEOUT_MS 5000U

/* 7-segment digit font: bits = 0gfedcba (DP separate) */
static const uint8_t SEG_FONT[18] = {
    0x3F, /* 0: a+b+c+d+e+f     */
    0x06, /* 1: b+c               */
    0x5B, /* 2: a+b+d+e+g         */
    0x4F, /* 3: a+b+c+d+g         */
    0x66, /* 4: b+c+f+g           */
    0x6D, /* 5: a+c+d+f+g         */
    0x7D, /* 6: a+c+d+e+f+g       */
    0x07, /* 7: a+b+c             */
    0x7F, /* 8: a+b+c+d+e+f+g     */
    0x6F, /* 9: a+b+c+d+f+g       */
    0x77, /* A: a+b+c+e+f+g       */
    0x7C, /* b: c+d+e+f+g         */
    0x39, /* C: a+d+e+f           */
    0x5E, /* d: b+c+d+e+g         */
    0x79, /* E: a+d+e+f+g         */
    0x71, /* F: a+e+f+g           */
    0x00, /* 16: blank            */
    0x40, /* 17: dash (-)         */
};
#define SEG_BLANK  16
#define SEG_DASH   17

/* Display buffers — 16 bytes per chip (GRID0-GRID15) */
static uint8_t s_u9_buf[16];  /* U9: upper row */
static uint8_t s_u1_buf[16];  /* U1: lower row */

/* Key scan state */
#define TOTAL_KEYS          10  /* 9 buttons + encoder push */
#define KEY_LONG_PRESS_MS   2000
static uint8_t  s_key_state[TOTAL_KEYS];       /* debounced: 0=released 1=pressed */
static uint8_t  s_key_debounce[TOTAL_KEYS];    /* consecutive same-reading count */
static uint32_t s_key_press_tick[TOTAL_KEYS];  /* tick when press detected */
static uint8_t  s_key_long_sent[TOTAL_KEYS];   /* long-press event already sent */

/* Encoder rotation state */
static uint8_t  s_enc_last_ab;  /* previous A|B state (2 bits) */
static int8_t   s_enc_delta;    /* accumulated rotation: +CW, -CCW */

/* Protocol frame parser state */
#define FRAME_MAX_DATA      64
static uint8_t  s_frame_state;      /* 0=idle, 1=got AA, 2=got 55, 3=cmd, 4=len, 5=data, 6=cs */
static uint8_t  s_frame_cmd;
static uint8_t  s_frame_len;
static uint8_t  s_frame_data[FRAME_MAX_DATA];
static uint8_t  s_frame_idx;
static uint8_t  s_frame_cs;

/* System clock (will be set by clock config) */
static uint32_t s_sysclk = 8000000;  /* Default HSI */

/* ========================================================================= */
/*  Clock Configuration                                                      */
/* ========================================================================= */

static void SystemClock_Config(void)
{
    /* Try HSE 8MHz → PLL ×9 → 72MHz (same as main board) */
    RCC_CR |= (1 << 16);           /* HSEON */
    uint32_t timeout = 100000;
    while (!(RCC_CR & (1 << 17)) && --timeout) {}  /* Wait HSERDY */

    if (RCC_CR & (1 << 17)) {
        /* HSE ready — configure PLL */
        /* GD32F30x Flash (FMC) wait state config for 72MHz:
         * FMC_WS (0x40022000): bits[2:0] = WSCNT = 2 (2 wait states for 48-72MHz)
         * FMC_WSEN not needed for <=72MHz on GD32F303. */
        volatile uint32_t *fmc_ws = (volatile uint32_t *)0x40022000;
        *fmc_ws = (*fmc_ws & ~0x07) | 0x02;  /* WSCNT = 2 wait states */

        RCC_CFGR = (RCC_CFGR & ~0x3FC03) |
                    (0x4 << 8) |    /* PPRE1 = /2 (APB1 = 36MHz) */
                    (0x0 << 11) |   /* PPRE2 = /1 (APB2 = 72MHz) */
                    (0x7 << 18) |   /* PLLMUL = ×9 */
                    (1 << 16);      /* PLLSRC = HSE */

        RCC_CR |= (1 << 24);       /* PLLON */
        while (!(RCC_CR & (1 << 25))) {}  /* Wait PLLRDY */

        RCC_CFGR = (RCC_CFGR & ~0x3) | 0x2;  /* SW = PLL */
        while ((RCC_CFGR & 0xC) != 0x8) {}    /* Wait SWS = PLL */

        s_sysclk = 72000000;
    } else {
        /* HSE failed — stay on HSI 8MHz */
        s_sysclk = 8000000;
    }
}

/* ========================================================================= */
/*  SysTick (1ms tick)                                                       */
/* ========================================================================= */

void SysTick_Handler(void)
{
    s_tick_ms++;
}

static void SysTick_Init(void)
{
    SYST_RVR = (s_sysclk / 1000) - 1;  /* 1ms reload */
    SYST_CVR = 0;
    SYST_CSR = 7;  /* Enable, TickInt, ClkSource=AHB */
}

static uint32_t tick_ms(void) { return s_tick_ms; }

/* ========================================================================= */
/*  UART1 (PA9=TX, PA10=RX, 115200)                                         */
/* ========================================================================= */

static void UART1_Init(void)
{
    /* Enable clocks: GPIOA + USART1 + AFIO */
    RCC_APB2ENR |= (1 << 2) | (1 << 14) | (1 << 0);  /* GPIOA, USART1, AFIO */

    /* PA9 = AF push-pull output (USART1_TX): CNF=10, MODE=11 → 0xB */
    uint32_t crh = GPIOA_CRH;
    crh &= ~(0xF << 4);    /* Clear PA9 bits [7:4] */
    crh |=  (0xB << 4);    /* AF PP, 50MHz */
    /* PA10 = Input floating (USART1_RX): CNF=01, MODE=00 → 0x4 */
    crh &= ~(0xF << 8);    /* Clear PA10 bits [11:8] */
    crh |=  (0x4 << 8);    /* Input floating */
    GPIOA_CRH = crh;

    /* USART1 config: 115200 8N1 */
    uint32_t pclk2 = (s_sysclk == 72000000) ? 72000000 : 8000000;  /* APB2 */
    USART1_BRR = pclk2 / 115200;  /* BRR = PCLK2/baud */
    USART1_CR1 = (1 << 13) |   /* UE: USART enable */
                 (1 << 3) |    /* TE: Transmitter enable */
                 (1 << 2) |    /* RE: Receiver enable */
                 (1 << 5);     /* RXNEIE: RX interrupt enable */
    USART1_CR2 = 0;
    USART1_CR3 = 0;

    /* Enable USART1 IRQ (IRQ37) in NVIC */
    NVIC_ISER1 = (1 << (37 - 32));
}

void USART1_IRQHandler(void)
{
    if (USART1_SR & (1 << 5)) {  /* RXNE */
        uint8_t byte = (uint8_t)USART1_DR;
        uint16_t next = (s_rx_head + 1) % UART_RX_BUF_SIZE;
        if (next != s_rx_tail) {
            s_rx_buf[s_rx_head] = byte;
            s_rx_head = next;
        }
    }
    /* Clear overrun if set */
    if (USART1_SR & (1 << 3)) {
        (void)USART1_DR;
    }
}

static void uart1_send(const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        uint32_t t0 = tick_ms();
        while (!(USART1_SR & (1 << 7))) {   /* Wait TXE with timeout */
            if (tick_ms() - t0 > 10) return; /* 10ms timeout per byte — bail out */
        }
        USART1_DR = data[i];
    }
}

/* Forward declaration */
static void uart2_send(const uint8_t *data, uint16_t len);

/* Primary TX: use UART2 (CN12, 3.3V direct, bypassing broken CN11 level shifter) */
static void uart_primary_send(const uint8_t *data, uint16_t len)
{
    uart2_send(data, len);
}

static int uart1_rx_available(void)
{
    return s_rx_head != s_rx_tail;
}

static uint8_t uart1_rx_read(void)
{
    uint8_t byte = s_rx_buf[s_rx_tail];
    s_rx_tail = (s_rx_tail + 1) % UART_RX_BUF_SIZE;
    return byte;
}

/* ========================================================================= */
/*  UART2 (PA2=TX, PA3=RX, 115200) — CN12 debug output, 3.3V direct         */
/* ========================================================================= */

/* UART2 RX ring buffer */
#define UART2_RX_BUF_SIZE  128
static volatile uint8_t  s_rx2_buf[UART2_RX_BUF_SIZE];
static volatile uint16_t s_rx2_head;
static volatile uint16_t s_rx2_tail;

static void UART2_Init(void)
{
    /* Enable USART2 clock + GPIOA clock (self-contained, no init-order dependency) */
    RCC_APB2ENR |= (1 << 2);   /* GPIOA */
    RCC_APB1ENR |= (1 << 17);  /* USART2 */

    /* PA2 = AF push-pull (USART2_TX): CRL bits[11:8] = 0xB */
    uint32_t crl = GPIOA_CRL;
    crl &= ~(0xF << 8);    /* Clear PA2 bits */
    crl |=  (0xB << 8);    /* AF PP, 50MHz */
    /* PA3 = Input floating (USART2_RX): CRL bits[15:12] = 0x4 */
    crl &= ~(0xF << 12);
    crl |=  (0x4 << 12);
    GPIOA_CRL = crl;

    /* USART2 config: 115200 8N1, TX + RX + RX interrupt */
    uint32_t pclk1 = (s_sysclk == 72000000) ? 36000000 : 8000000;  /* APB1 */
    USART2_BRR = pclk1 / 115200;
    USART2_CR1 = (1 << 13) |   /* UE */
                 (1 << 3) |    /* TE */
                 (1 << 2) |    /* RE */
                 (1 << 5);     /* RXNEIE */
    USART2_CR2 = 0;
    USART2_CR3 = 0;

    /* Enable USART2 IRQ (IRQ38) in NVIC — ISER1 bit 6 */
    NVIC_ISER1 |= (1 << (38 - 32));
}

void USART2_IRQHandler(void)
{
    if (USART2_SR & (1 << 5)) {  /* RXNE */
        uint8_t byte = (uint8_t)USART2_DR;
        uint16_t next = (s_rx2_head + 1) % UART2_RX_BUF_SIZE;
        if (next != s_rx2_tail) {
            s_rx2_buf[s_rx2_head] = byte;
            s_rx2_head = next;
        }
    }
    if (USART2_SR & (1 << 3)) {
        (void)USART2_DR;  /* Clear overrun */
    }
}

static int uart2_rx_available(void)
{
    return s_rx2_head != s_rx2_tail;
}

static uint8_t uart2_rx_read(void)
{
    uint8_t byte = s_rx2_buf[s_rx2_tail];
    s_rx2_tail = (s_rx2_tail + 1) % UART2_RX_BUF_SIZE;
    return byte;
}

static void uart2_send(const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        uint32_t t0 = tick_ms();
        while (!(USART2_SR & (1 << 7))) {   /* Wait TXE with timeout */
            if (tick_ms() - t0 > 10) return; /* 10ms timeout per byte — bail out */
        }
        USART2_DR = data[i];
    }
}

/* ========================================================================= */
/*  Screen Protocol — Frame Build + Parse                                    */
/*  Frame: AA 55 [CMD] [LEN] [DATA...] [CS] ED                              */
/*  CS = (CMD + LEN + all DATA bytes) & 0xFF                                 */
/* ========================================================================= */

static void send_frame(uint8_t cmd, const uint8_t *data, uint8_t len)
{
    if (len > FRAME_MAX_DATA) return;       /* A4 fix: prevent stack overflow */
    if (len > 0 && data == NULL) return;    /* A4 fix: NULL guard */
    uint8_t buf[64 + 6];
    buf[0] = 0xAA;
    buf[1] = 0x55;
    buf[2] = cmd;
    buf[3] = len;
    uint8_t cs = cmd + len;
    for (uint8_t i = 0; i < len; i++) {
        buf[4 + i] = data[i];
        cs += data[i];
    }
    buf[4 + len] = cs;
    buf[5 + len] = 0xED;
    uart_primary_send(buf, 6 + len);  /* CN12 (UART2) — primary */
}

static void send_heartbeat(void)
{
    /* 0x84: empty payload */
    send_frame(0x84, NULL, 0);
}

static void send_param_set(uint8_t param_id, uint16_t value)
{
    uint8_t payload[3];
    payload[0] = param_id;
    payload[1] = (uint8_t)(value >> 8);
    payload[2] = (uint8_t)(value & 0xFF);
    send_frame(0x81, payload, 3);
}

static void hmi_touch(void)
{
    s_hmi_last_input_tick = tick_ms();
}

static void hmi_seed_from_display(const uint8_t *data)
{
    if (s_hmi_shadow_valid) return;

    s_set_temp_x10 = (int16_t)((data[0] << 8) | data[1]);
    s_set_hum_x10  = (uint16_t)data[2] * 10U;
    s_set_o2_x10   = (uint16_t)data[3] * 10U;

    if (s_set_temp_x10 < 100) s_set_temp_x10 = 100;
    if (s_set_temp_x10 > 400) s_set_temp_x10 = 400;
    if (s_set_hum_x10 < 300)  s_set_hum_x10  = 300;
    if (s_set_hum_x10 > 900)  s_set_hum_x10  = 900;
    if (s_set_o2_x10 < 210)   s_set_o2_x10   = 210;
    if (s_set_o2_x10 > 1000)  s_set_o2_x10   = 1000;

    s_hmi_shadow_valid = 1;
}

static void hmi_cycle_page(void)
{
    switch (s_hmi_page) {
    case HMI_PAGE_LIVE:
        s_hmi_page = HMI_PAGE_SET_TEMP;
        break;
    case HMI_PAGE_SET_TEMP:
        s_hmi_page = HMI_PAGE_SET_HUMID;
        break;
    case HMI_PAGE_SET_HUMID:
        s_hmi_page = HMI_PAGE_SET_O2;
        break;
    default:
        s_hmi_page = HMI_PAGE_LIVE;
        break;
    }
    hmi_touch();
}

static void hmi_apply_encoder_delta(int8_t delta)
{
    if (!s_hmi_shadow_valid || delta == 0) return;
    if (s_hmi_page == HMI_PAGE_LIVE) return;

    while (delta > 0) {
        switch (s_hmi_page) {
        case HMI_PAGE_SET_TEMP:
            if (s_set_temp_x10 < 400) {
                s_set_temp_x10++;
                send_param_set(0x01, (uint16_t)s_set_temp_x10);
            }
            break;
        case HMI_PAGE_SET_HUMID:
            if (s_set_hum_x10 < 900) {
                s_set_hum_x10 += 10;
                send_param_set(0x02, s_set_hum_x10);
            }
            break;
        case HMI_PAGE_SET_O2:
            if (s_set_o2_x10 < 1000) {
                s_set_o2_x10 += 10;
                send_param_set(0x03, s_set_o2_x10);
            }
            break;
        default:
            break;
        }
        delta--;
    }

    while (delta < 0) {
        switch (s_hmi_page) {
        case HMI_PAGE_SET_TEMP:
            if (s_set_temp_x10 > 100) {
                s_set_temp_x10--;
                send_param_set(0x01, (uint16_t)s_set_temp_x10);
            }
            break;
        case HMI_PAGE_SET_HUMID:
            if (s_set_hum_x10 > 300) {
                s_set_hum_x10 -= 10;
                send_param_set(0x02, s_set_hum_x10);
            }
            break;
        case HMI_PAGE_SET_O2:
            if (s_set_o2_x10 > 210) {
                s_set_o2_x10 -= 10;
                send_param_set(0x03, s_set_o2_x10);
            }
            break;
        default:
            break;
        }
        delta++;
    }

    hmi_touch();
}

static void process_rx_frame(uint8_t cmd, const uint8_t *data, uint8_t len)
{
    switch (cmd) {
    case 0x01:  /* Display data (26 bytes) */
        if (len >= 22) {
            memcpy(s_display_data, data, (len < 26) ? len : 26);
            s_display_valid = 1;
            hmi_seed_from_display(data);
        }
        break;

    case 0x04:  /* Heartbeat from main board (8 bytes) */
        break;

    default:
        break;
    }
}

/* ========================================================================= */
/*  Display Data → TM1640 buffer mapping                                     */
/*  0x01 packet layout:                                                      */
/*    [0-1] temp_avg (int16, x10)   [2] humidity%  [3] o2%                  */
/*    [4-5] co2_ppm (uint16)        [10-11] fog_rem (sec)                   */
/*    [12-13] disinf_rem (sec)      [14-15] o2_accum (sec)                  */
/* ========================================================================= */

/* Helper: write a multi-digit number into buffer at given GRID positions */
static void display_digits(uint8_t *buf, const uint8_t *grids, uint8_t ndig,
                           int32_t val, uint8_t leading_zero, int8_t dp_pos)
{
    /* dp_pos: which digit (0=leftmost) gets decimal point, -1=none */
    uint8_t digits[6];
    int32_t abs_val = (val < 0) ? -val : val;
    for (int i = ndig - 1; i >= 0; i--) {
        digits[i] = abs_val % 10;
        abs_val /= 10;
    }
    for (uint8_t i = 0; i < ndig; i++) {
        uint8_t seg = SEG_FONT[digits[i]];
        /* Suppress leading zeros (except last digit and dp position) */
        if (!leading_zero && digits[i] == 0 && i < ndig - 1 && i != dp_pos) {
            uint8_t all_zero = 1;
            for (uint8_t j = 0; j <= i; j++) {
                if (digits[j] != 0) { all_zero = 0; break; }
            }
            if (all_zero && i != dp_pos) seg = 0;  /* blank */
        }
        if (dp_pos >= 0 && i == (uint8_t)dp_pos)
            seg |= 0x80;  /* Add decimal point */
        buf[grids[i]] = seg;
    }
    /* Negative: show dash on leftmost digit if val < 0 */
    if (val < 0) buf[grids[0]] = SEG_FONT[SEG_DASH];
}

/* Forward declarations */
static void TM1640_Update(void);

static void update_display_from_data(void)
{
    const uint8_t *d = s_display_data;

    /* Parse 0x01 packet fields */
    int16_t  temp_x10  = (int16_t)((d[0] << 8) | d[1]);  /* x10 °C */
    uint8_t  humidity  = d[2];                              /* % */
    uint8_t  o2_pct    = d[3];                              /* % */
    uint16_t co2_ppm   = (uint16_t)((d[4] << 8) | d[5]);
    uint16_t fog_sec   = (uint16_t)((d[10] << 8) | d[11]);
    uint16_t dis_sec   = (uint16_t)((d[12] << 8) | d[13]);
    uint16_t o2_sec    = (uint16_t)((d[14] << 8) | d[15]);

    /* === U9 upper row: temp(3) + humidity(2) + o2(2) + co2(4) === */
    memset(s_u9_buf, 0, 16);

    /* Temperature: 3 digits with decimal point at position 1 (e.g., 12.0) */
    {
        int32_t tv = (temp_x10 < 0) ? -temp_x10 : temp_x10;
        if (tv > 999) tv = 999;
        uint8_t grids[] = {0, 1, 2};
        display_digits(s_u9_buf, grids, 3, tv, 0, 1);  /* DP after 2nd digit */
        if (temp_x10 < 0) s_u9_buf[0] = SEG_FONT[SEG_DASH];
    }

    /* Humidity: 2 digits (e.g., 50) */
    {
        uint8_t grids[] = {3, 4};
        display_digits(s_u9_buf, grids, 2, humidity, 0, -1);
    }

    /* O2%: 2 digits (e.g., 21) */
    {
        uint8_t grids[] = {5, 6};
        display_digits(s_u9_buf, grids, 2, o2_pct, 0, -1);
    }

    /* CO2 ppm: 4 digits (e.g., 3721) */
    {
        uint8_t grids[] = {7, 8, 9, 10};
        display_digits(s_u9_buf, grids, 4, co2_ppm, 0, -1);
    }

    /* === U1 lower row: fog(2) + disinf(2) + o2_time(6) === */
    memset(s_u1_buf, 0, 16);

    /* Fog remaining: seconds→minutes, 2 digits */
    {
        uint16_t fog_min = fog_sec / 60;
        if (fog_min > 99) fog_min = 99;
        uint8_t grids[] = {0, 1};
        display_digits(s_u1_buf, grids, 2, fog_min, 1, -1);  /* leading zero */
    }

    /* Disinfect remaining: seconds→minutes, 2 digits */
    {
        uint16_t dis_min = dis_sec / 60;
        if (dis_min > 99) dis_min = 99;
        uint8_t grids[] = {2, 3};
        display_digits(s_u1_buf, grids, 2, dis_min, 1, -1);
    }

    /* O2 accumulated: seconds→HH:MM:SS, 6 digits (GRID8/9 swapped) */
    {
        uint16_t total = o2_sec;
        uint8_t hh = total / 3600;
        uint8_t mm = (total % 3600) / 60;
        uint8_t ss = total % 60;
        if (hh > 99) hh = 99;
        /* GRID mapping: 4,5=HH  6,7=MM  8=SS个位 9=SS十位 (swapped!) */
        s_u1_buf[4] = SEG_FONT[hh / 10];
        s_u1_buf[5] = SEG_FONT[hh % 10];
        s_u1_buf[6] = SEG_FONT[mm / 10];
        s_u1_buf[7] = SEG_FONT[mm % 10];
        s_u1_buf[8] = SEG_FONT[ss % 10];  /* GRID8 = 秒个位 (swapped) */
        s_u1_buf[9] = SEG_FONT[ss / 10];  /* GRID9 = 秒十位 (swapped) */
    }

    /* === U1 indicator LEDs: GRID10/11/12 设备运行指示 === */
    /* Relay bitmap bits (from bsp_config.h):
     *   0=PTC, 1=JIARE(底热), 2=RED(空), 3=ZIY(UV), 4=O2,
     *   5=JIASHI(加湿), 6=FENGJI(外风机), 7=YASUO(压缩机), 8=WH(雾化) */
    {
        uint16_t alarm_flags  = (uint16_t)((d[20] << 8) | d[21]);
        uint16_t relay_status = (uint16_t)((d[16] << 8) | d[17]);

        uint8_t fog_on  = (relay_status & (1U << 8)) ? 1 : 0;  /* WH 雾化 */
        uint8_t uv_on   = (relay_status & (1U << 3)) ? 1 : 0;  /* ZIY 紫外 */
        uint8_t humid_on = (relay_status & (1U << 5)) ? 1 : 0; /* JIASHI 加湿 */
        uint8_t o2_on   = (relay_status & (1U << 4)) ? 1 : 0;  /* O2 供氧 */
        uint8_t cool_on = (relay_status & (1U << 7)) ? 1 : 0;  /* YASUO 压缩机 */
        uint8_t heat_on = (relay_status & ((1U << 0) | (1U << 1))) ? 1 : 0; /* PTC+底热 */

        /* GRID10: 报警指示 — 有报警时全亮 */
        s_u1_buf[10] = (alarm_flags != 0) ? 0xFF : 0x00;
        /* GRID11: 治疗/执行器运行 — 雾化、紫外消毒、加湿工作中 */
        s_u1_buf[11] = (fog_on || uv_on || humid_on) ? 0xFF : 0x00;
        /* GRID12: 温控/供氧运行 — O2供氧或压缩机制冷或加热中 */
        s_u1_buf[12] = (o2_on || cool_on || heat_on) ? 0xFF : 0x00;
    }

    /* Edit-mode overlay: alternate current value and setpoint every 500ms. */
    if (s_hmi_page != HMI_PAGE_LIVE && ((tick_ms() / 500U) & 0x1U) == 0U) {
        switch (s_hmi_page) {
        case HMI_PAGE_SET_TEMP: {
            int32_t tv = (s_set_temp_x10 < 0) ? -s_set_temp_x10 : s_set_temp_x10;
            uint8_t grids[] = {0, 1, 2};
            if (tv > 999) tv = 999;
            display_digits(s_u9_buf, grids, 3, tv, 0, 1);
            if (s_set_temp_x10 < 0) s_u9_buf[0] = SEG_FONT[SEG_DASH];
            break;
        }
        case HMI_PAGE_SET_HUMID: {
            uint8_t grids[] = {3, 4};
            display_digits(s_u9_buf, grids, 2, s_set_hum_x10 / 10U, 0, -1);
            break;
        }
        case HMI_PAGE_SET_O2: {
            uint8_t grids[] = {5, 6};
            display_digits(s_u9_buf, grids, 2, s_set_o2_x10 / 10U, 0, -1);
            break;
        }
        default:
            break;
        }
        s_u1_buf[11] = 0xFF;  /* simple edit indicator */
    }

    TM1640_Update();
}

static void parse_rx_byte(uint8_t byte)
{
    switch (s_frame_state) {
    case 0: /* Idle — wait for 0xAA */
        if (byte == 0xAA) s_frame_state = 1;
        break;
    case 1: /* Got AA — expect 0x55 */
        if (byte == 0x55) s_frame_state = 2;
        else s_frame_state = (byte == 0xAA) ? 1 : 0;
        break;
    case 2: /* Got AA 55 — CMD */
        s_frame_cmd = byte;
        s_frame_cs = byte;
        s_frame_state = 3;
        break;
    case 3: /* Got CMD — LEN */
        s_frame_len = byte;
        s_frame_cs += byte;
        s_frame_idx = 0;
        if (byte > FRAME_MAX_DATA) {
            s_frame_state = 0;  /* LEN too large, discard */
        } else {
            s_frame_state = (byte > 0) ? 4 : 5;
        }
        break;
    case 4: /* Collecting DATA */
        if (s_frame_idx < FRAME_MAX_DATA) {
            s_frame_data[s_frame_idx] = byte;
        }
        s_frame_cs += byte;
        s_frame_idx++;
        if (s_frame_idx >= s_frame_len) {
            s_frame_state = 5;
        }
        break;
    case 5: /* Expect CS */
        if ((s_frame_cs & 0xFF) == byte) {
            s_frame_state = 6;
        } else {
            s_frame_state = 0;  /* CS mismatch, reset */
        }
        break;
    case 6: /* Expect ED */
        if (byte == 0xED) {
            process_rx_frame(s_frame_cmd, s_frame_data, s_frame_len);
        }
        s_frame_state = 0;
        break;
    default:
        s_frame_state = 0;
        break;
    }
}

/* ========================================================================= */
/*  Debug LED — blink PC13 and PB2 to confirm firmware is running            */
/*  If your board has an LED on PC13 (common) or PB2, it will blink.         */
/*  Even without LED, GPIO toggle is measurable with multimeter.             */
/* ========================================================================= */

static void Debug_LED_Init(void)
{
    /* Enable GPIOB and GPIOC clocks */
    RCC_APB2ENR |= (1 << 3) | (1 << 4);  /* GPIOB(bit3), GPIOC(bit4) */

    /* PC13 = push-pull output, 2MHz: CRH bits[23:20] = 0x2 (MODE=10,CNF=00) */
    /* Note: PB2 was previously debug LED but now used for encoder A-phase */
    uint32_t crh = GPIOC_CRH;
    crh &= ~(0xFU << 20);
    crh |=  (0x2U << 20);
    GPIOC_CRH = crh;

    GPIOC_BSRR = (1 << 13);  /* Start HIGH (LED off) */
}

static void Debug_LED_Toggle(void)
{
    if (GPIOC_ODR & (1 << 13))
        GPIOC_BSRR = (1 << (13 + 16));  /* Reset = LOW */
    else
        GPIOC_BSRR = (1 << 13);         /* Set = HIGH */
}

/* ========================================================================= */
/*  TM1640 LED Driver — bit-bang serial for U9 and U1                        */
/*  U9: DIN=PB7, SCLK=PB6, GRID=DIG1-DIG11 (seg A-G+DP)                    */
/*  U1: DIN=PB4, SCLK=PB3, GRID=DIGA1-DIGA13 (seg A1-G1+DP1)              */
/*  Protocol: Start→CMD→Data→Stop, LSB first, CLK rising edge              */
/* ========================================================================= */

/* Chip select */
#define TM_CHIP_U9   0   /* DIN=PB7, CLK=PB6 */
#define TM_CHIP_U1   1   /* DIN=PB4, CLK=PB3 */

/* Pin bit positions */
#define U9_DIN_BIT   7
#define U9_CLK_BIT   6
#define U1_DIN_BIT   4
#define U1_CLK_BIT   3

/* Inline helpers — set/clear via BSRR for atomic operation */
static inline void tm_din_high(uint8_t chip) {
    GPIOB_BSRR = (chip == TM_CHIP_U9) ? (1 << U9_DIN_BIT) : (1 << U1_DIN_BIT);
}
static inline void tm_din_low(uint8_t chip) {
    GPIOB_BSRR = (chip == TM_CHIP_U9) ? (1 << (U9_DIN_BIT+16)) : (1 << (U1_DIN_BIT+16));
}
static inline void tm_clk_high(uint8_t chip) {
    GPIOB_BSRR = (chip == TM_CHIP_U9) ? (1 << U9_CLK_BIT) : (1 << U1_CLK_BIT);
}
static inline void tm_clk_low(uint8_t chip) {
    GPIOB_BSRR = (chip == TM_CHIP_U9) ? (1 << (U9_CLK_BIT+16)) : (1 << (U1_CLK_BIT+16));
}

static void tm_delay(void)
{
    /* ~1µs at 72MHz — TM1640 needs minimal setup/hold time */
    for (volatile int i = 0; i < 10; i++) {}
}

/* Start condition: DIN goes LOW while CLK is HIGH */
static void tm_start(uint8_t chip)
{
    tm_din_high(chip);
    tm_clk_high(chip);
    tm_delay();
    tm_din_low(chip);
    tm_delay();
    tm_clk_low(chip);
    tm_delay();
}

/* Stop condition: DIN goes HIGH while CLK is HIGH */
static void tm_stop(uint8_t chip)
{
    tm_clk_low(chip);
    tm_din_low(chip);
    tm_delay();
    tm_clk_high(chip);
    tm_delay();
    tm_din_high(chip);
    tm_delay();
}

/* Write one byte, LSB first */
static void tm_write_byte(uint8_t chip, uint8_t val)
{
    for (uint8_t i = 0; i < 8; i++) {
        tm_clk_low(chip);
        if (val & 0x01)
            tm_din_high(chip);
        else
            tm_din_low(chip);
        val >>= 1;
        tm_delay();
        tm_clk_high(chip);
        tm_delay();
    }
}

/* Write display data: auto-increment mode starting at address 0 */
static void tm1640_write_display(uint8_t chip, const uint8_t *data, uint8_t len)
{
    /* Command 1: data write mode, auto-increment */
    tm_start(chip);
    tm_write_byte(chip, 0x40);
    tm_stop(chip);

    /* Command 2: set start address 0xC0 + write data */
    tm_start(chip);
    tm_write_byte(chip, 0xC0);
    for (uint8_t i = 0; i < len; i++) {
        tm_write_byte(chip, data[i]);
    }
    tm_stop(chip);
}

/* Set brightness: 0=off, 1-8=brightness levels */
static void tm1640_set_brightness(uint8_t chip, uint8_t level)
{
    tm_start(chip);
    if (level == 0)
        tm_write_byte(chip, 0x80);       /* Display OFF */
    else
        tm_write_byte(chip, 0x87 + level); /* 0x88=dim ... 0x8F=max */
    tm_stop(chip);
}

static void TM1640_Init(void)
{
    /* Enable GPIOB + AFIO clocks (self-contained, no init-order dependency) */
    RCC_APB2ENR |= (1 << 3) | (1 << 0);   /* GPIOB + AFIO */
    /* Release PB3(JTDO) and PB4(JNTRST) from JTAG — SWJ_NOJTAG remap */
    AFIO_MAPR = (AFIO_MAPR & ~(0x7 << 24)) | (0x2 << 24);  /* SWJ_CFG=010: SWD only */

    /* Configure PB3/PB4/PB6/PB7 as push-pull output 2MHz */
    /* CRL controls PB0-PB7, each pin = 4 bits */
    uint32_t crl = GPIOB_CRL;
    crl &= ~(0xF << 12);   /* PB3 bits[15:12] */
    crl |=  (0x2 << 12);   /* PP output 2MHz */
    crl &= ~(0xF << 16);   /* PB4 bits[19:16] */
    crl |=  (0x2 << 16);
    crl &= ~(0xF << 24);   /* PB6 bits[27:24] */
    crl |=  (0x2 << 24);
    crl &= ~(0xF << 28);   /* PB7 bits[31:28] */
    crl |=  (0x2 << 28);
    GPIOB_CRL = crl;

    /* Start with all pins HIGH (idle state) */
    GPIOB_BSRR = (1 << 3) | (1 << 4) | (1 << 6) | (1 << 7);

    /* Clear display buffers */
    memset(s_u9_buf, 0, sizeof(s_u9_buf));
    memset(s_u1_buf, 0, sizeof(s_u1_buf));

    /* Initialize both chips: all segments off, brightness max */
    tm1640_write_display(TM_CHIP_U9, s_u9_buf, 16);
    tm1640_write_display(TM_CHIP_U1, s_u1_buf, 16);
    tm1640_set_brightness(TM_CHIP_U9, 4);  /* Medium brightness */
    tm1640_set_brightness(TM_CHIP_U1, 4);
}

static void TM1640_AllOn(void)
{
    memset(s_u9_buf, 0xFF, 16);
    memset(s_u1_buf, 0xFF, 16);
    tm1640_write_display(TM_CHIP_U9, s_u9_buf, 16);
    tm1640_write_display(TM_CHIP_U1, s_u1_buf, 16);
}

static void TM1640_AllOff(void)
{
    memset(s_u9_buf, 0, 16);
    memset(s_u1_buf, 0, 16);
    tm1640_write_display(TM_CHIP_U9, s_u9_buf, 16);
    tm1640_write_display(TM_CHIP_U1, s_u1_buf, 16);
}

/* Flush display buffers to both chips */
static void TM1640_Update(void)
{
    tm1640_write_display(TM_CHIP_U9, s_u9_buf, 16);
    tm1640_write_display(TM_CHIP_U1, s_u1_buf, 16);
}

/* ========================================================================= */
/*  Key Scan + Encoder — 9 buttons + rotary encoder (A/B/push)               */
/*  Pins:                                                                    */
/*    KEY1=PB12  KEY2=PB13  KEY3=PB14  KEY4=PB15                            */
/*    KEY5=PC6   KEY6=PC7   KEY7=PC8   KEY8=PC9                             */
/*    KEY9=PA8   Encoder: A=PB2  B=PA6  Push=PA7                            */
/*  Protocol key IDs (0x82 packet):                                          */
/*    0x01-0x09 = KEY1-KEY9,  0x0A = encoder push                           */
/*  Action: 0x01=click  0x02=long press                                      */
/* ========================================================================= */

/* Key active level — flip this #define if buttons read inverted.
 * 0 = active-low (pressed=GND, released=VCC) — most common wiring
 * 1 = active-high (pressed=VCC, released=GND) */
#define KEY_ACTIVE_HIGH  0

static void Key_Init(void)
{
    /* Ensure GPIO clocks are enabled (self-contained, no init-order dependency) */
    RCC_APB2ENR |= (1 << 2) | (1 << 3) | (1 << 4);  /* GPIOA, GPIOB, GPIOC */

    /* All key/encoder pins: input with pull-up (CNF=10, MODE=00 → nibble 0x8)
     * Then set ODR bit = 1 to select pull-up (vs pull-down) */

    /* --- GPIOA: PA6(enc B), PA7(enc push), PA8(KEY9) --- */
    /* PA6: CRL[27:24], PA7: CRL[31:28] */
    uint32_t crl = GPIOA_CRL;
    crl &= ~(0xFFU << 24);         /* Clear PA6, PA7 */
    crl |=  (0x88U << 24);         /* Input pull-up/down */
    GPIOA_CRL = crl;
    /* PA8: CRH[3:0] */
    uint32_t crh = GPIOA_CRH;
    crh &= ~0xFU;                  /* Clear PA8 */
    crh |=  0x8U;                  /* Input pull-up/down */
    GPIOA_CRH = crh;
    /* Enable pull-ups: set ODR bits */
    GPIOA_ODR |= (1 << 6) | (1 << 7) | (1 << 8);

    /* --- GPIOB: PB2(enc A), PB12-PB15(KEY1-KEY4) --- */
    /* PB2: CRL[11:8] — was debug LED, now encoder input */
    crl = GPIOB_CRL;
    crl &= ~(0xFU << 8);           /* Clear PB2 */
    crl |=  (0x8U << 8);           /* Input pull-up/down */
    GPIOB_CRL = crl;
    /* PB12: CRH[19:16], PB13: [23:20], PB14: [27:24], PB15: [31:28] */
    crh = GPIOB_CRH;
    crh &= ~0xFFFF0000U;           /* Clear PB12-PB15 */
    crh |=  0x88880000U;           /* All input pull-up/down */
    GPIOB_CRH = crh;
    /* Enable pull-ups */
    GPIOB_ODR |= (1 << 2) | (1 << 12) | (1 << 13) | (1 << 14) | (1 << 15);

    /* --- GPIOC: PC6-PC9 (KEY5-KEY8) --- */
    /* PC6: CRL[27:24], PC7: CRL[31:28] */
    crl = GPIOC_CRL;
    crl &= ~(0xFFU << 24);
    crl |=  (0x88U << 24);
    GPIOC_CRL = crl;
    /* PC8: CRH[3:0], PC9: CRH[7:4] */
    crh = GPIOC_CRH;
    crh &= ~0xFFU;                 /* Clear PC8, PC9 (preserve PC13 debug LED) */
    crh |=  0x88U;
    GPIOC_CRH = crh;
    GPIOC_ODR |= (1 << 6) | (1 << 7) | (1 << 8) | (1 << 9);

    /* Init encoder: read current A/B state */
    uint8_t a = (GPIOB_IDR & (1 << 2))  ? 1 : 0;
    uint8_t b = (GPIOA_IDR & (1 << 6))  ? 1 : 0;
    s_enc_last_ab = (a << 1) | b;
}

/* Read raw pin for key index 0-9, returns 1=pressed */
static uint8_t key_read_raw(uint8_t idx)
{
    uint32_t level;
    switch (idx) {
    case 0: level = GPIOB_IDR & (1 << 12); break;  /* KEY1 PB12 */
    case 1: level = GPIOB_IDR & (1 << 13); break;  /* KEY2 PB13 */
    case 2: level = GPIOB_IDR & (1 << 14); break;  /* KEY3 PB14 */
    case 3: level = GPIOB_IDR & (1 << 15); break;  /* KEY4 PB15 */
    case 4: level = GPIOC_IDR & (1 << 6);  break;  /* KEY5 PC6  */
    case 5: level = GPIOC_IDR & (1 << 7);  break;  /* KEY6 PC7  */
    case 6: level = GPIOC_IDR & (1 << 8);  break;  /* KEY7 PC8  */
    case 7: level = GPIOC_IDR & (1 << 9);  break;  /* KEY8 PC9  */
    case 8: level = GPIOA_IDR & (1 << 8);  break;  /* KEY9 PA8  */
    case 9: level = GPIOA_IDR & (1 << 7);  break;  /* Enc push PA7 */
    default: return 0;
    }
#if KEY_ACTIVE_HIGH
    return level ? 1 : 0;
#else
    return level ? 0 : 1;  /* Active-low: pin LOW = pressed */
#endif
}

/* Send 0x82 key action to main board.
 * KEY9 (0x09) click also sends 0x85 alarm acknowledge. */
static void send_key_action(uint8_t key_id, uint8_t action_type)
{
    if (key_id == 0x0A && action_type == 0x01) {
        hmi_cycle_page();
    }

    uint8_t payload[2] = { key_id, action_type };
    send_frame(0x82, payload, 2);

    /* KEY9 = alarm confirm: also send 0x85 to clear alarm latch */
    if (key_id == 0x09 && action_type == 0x01) {
        uint8_t ack = 0xFF;
        send_frame(0x85, &ack, 1);
    }
}

/* Key ID table: index 0-9 → protocol key ID 0x01-0x0A
 * Per spec (功能表.pdf + frozen spec 5.5.2):
 *   0x01 护理等级   (主板循环 1→2→3)
 *   0x02 照明灯     (主板 light_ctrl bit1 翻转)
 *   0x03 检查灯     (主板 light_ctrl bit0 翻转)
 *   0x04 红蓝光LED治疗灯 (主板 light_ctrl bit2+bit3 翻转, NOT 红外灯继电器)
 *   0x05 紫外灯     (主板启动/停止消毒定时周期, NOT 直接翻转继电器)
 *   0x06 开放式供氧 (主板 open_o2, 有互锁)
 *   0x07 内/外循环   (主板 inner_cycle)
 *   0x08 新风净化   (主板 fresh_air)
 *   0x09 报警确认   (主板 alarm.acknowledged, 同时屏幕板发0x85)
 *   0x0A 编码器按下 (HMI单击; 长按清零供氧累计) */
static const uint8_t KEY_ID_MAP[TOTAL_KEYS] = {
    0x01, 0x02, 0x03, 0x04, 0x05,  /* KEY1-KEY5 */
    0x06, 0x07, 0x08, 0x09,        /* KEY6-KEY9 */
    0x0A                            /* Encoder push */
};

/* Called every 20ms from main loop */
static void Key_Scan(void)
{
    uint32_t now = tick_ms();

    /* --- Button debounce + click/long-press detection --- */
    for (uint8_t i = 0; i < TOTAL_KEYS; i++) {
        uint8_t raw = key_read_raw(i);

        if (raw == s_key_state[i]) {
            /* Same as debounced state — reset debounce counter */
            s_key_debounce[i] = 0;

            /* Check long press while held */
            if (s_key_state[i] && !s_key_long_sent[i]) {
                if (now - s_key_press_tick[i] >= KEY_LONG_PRESS_MS) {
                    send_key_action(KEY_ID_MAP[i], 0x02);  /* Long press */
                    s_key_long_sent[i] = 1;
                }
            }
        } else {
            /* Different from debounced state — count */
            s_key_debounce[i]++;
            if (s_key_debounce[i] >= 2) {  /* 2×20ms = 40ms debounce */
                s_key_debounce[i] = 0;
                s_key_state[i] = raw;

                if (raw) {
                    /* Press detected */
                    s_key_press_tick[i] = now;
                    s_key_long_sent[i] = 0;
                } else {
                    /* Release detected — send click if no long-press was sent */
                    if (!s_key_long_sent[i]) {
                        send_key_action(KEY_ID_MAP[i], 0x01);  /* Click */
                    }
                }
            }
        }
    }

    /* --- Encoder rotation (quadrature A=PB2, B=PA6) --- */
    {
        uint8_t a = (GPIOB_IDR & (1 << 2))  ? 1 : 0;
        uint8_t b = (GPIOA_IDR & (1 << 6))  ? 1 : 0;
        uint8_t cur = (a << 1) | b;
        if (cur != s_enc_last_ab) {
            /* Gray code state table for CW rotation: 00→01→11→10→00
             * Detect direction from (last, cur) transition */
            static const int8_t enc_table[16] = {
                 0, +1, -1,  0,
                -1,  0,  0, +1,
                +1,  0,  0, -1,
                 0, -1, +1,  0
            };
            s_enc_delta += enc_table[(s_enc_last_ab << 2) | cur];
            s_enc_last_ab = cur;
        }
    }
}

/* Read and clear accumulated encoder rotation.
 * Returns: +N = N detents CW, -N = N detents CCW, 0 = no movement.
 * Call from HMI state machine (future) to adjust parameter values. */
static int8_t encoder_read_delta(void)
{
    int8_t d = s_enc_delta;
    s_enc_delta = 0;
    return d;
}

/* ========================================================================= */
/*  Main                                                                     */
/* ========================================================================= */

int main(void)
{
    SystemClock_Config();
    SysTick_Init();
    Debug_LED_Init();
    UART1_Init();
    UART2_Init();
    TM1640_Init();
    Key_Init();
    IWDG_Init();  /* P2 fix: 2s watchdog — resets MCU if main loop hangs */

    uint32_t last_heartbeat = 0;
    uint32_t last_display = 0;
    uint32_t last_blink = 0;
    uint32_t last_key_scan = 0;
    /* alarm_ack_counter removed — 0x85 now sent only on KEY9 press */

    /* Startup lamp test: all segments ON for 1s, then OFF. */
    TM1640_AllOn();
    { uint32_t t0 = tick_ms(); while (tick_ms() - t0 < 1000) { IWDG_Feed(); } }
    TM1640_AllOff();

    /* Key diagnostic removed — all 9 keys + encoder confirmed working.
     * CN mapping: CN1=KEY1 CN3=KEY2 CN5=KEY3 CN7=KEY4 CN2=KEY5
     *             CN4=KEY6 CN6=KEY7 CN8=KEY8 CN9=KEY9 */

    while (1) {
        /* Process UART RX bytes — primary on UART2 (CN12) */
        while (uart2_rx_available()) {
            parse_rx_byte(uart2_rx_read());
        }
        /* Also drain UART1 in case CN11 level shifter gets fixed later */
        while (uart1_rx_available()) {
            parse_rx_byte(uart1_rx_read());
        }

        /* Feed watchdog every loop iteration */
        IWDG_Feed();

        /* Blink debug LED every 500ms — visible proof firmware is running */
        if (tick_ms() - last_blink >= 500) {
            last_blink = tick_ms();
            Debug_LED_Toggle();
        }

        /* Key scan every 20ms (debounce + encoder) */
        if (tick_ms() - last_key_scan >= 20) {
            last_key_scan = tick_ms();
            Key_Scan();
        }

        {
            int8_t enc = encoder_read_delta();
            if (enc != 0) {
                hmi_apply_encoder_delta(enc);
            }
        }

        if (s_hmi_page != HMI_PAGE_LIVE && (tick_ms() - s_hmi_last_input_tick >= HMI_EDIT_TIMEOUT_MS)) {
            s_hmi_page = HMI_PAGE_LIVE;
        }

        /* Send heartbeat every 1s */
        if (tick_ms() - last_heartbeat >= 1000) {
            last_heartbeat = tick_ms();
            send_heartbeat();
        }

        /* Display update every 100ms */
        if (tick_ms() - last_display >= 100) {
            last_display = tick_ms();
            if (s_display_valid) {
                update_display_from_data();
            }
        }
    }
}
