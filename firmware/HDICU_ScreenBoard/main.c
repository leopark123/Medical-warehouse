/**
 * @file    main.c
 * @brief   HDICU-ZKB01A Screen Board Firmware — GD32F303RCT6
 *
 * Hardware:
 *   MCU: GD32F303RCT6 (Cortex-M4, compatible with STM32F303)
 *   Crystal: 8MHz HSE
 *   LED Driver: 2× TM1640 (DIN/SCLK serial)
 *   Keys: 9 independent buttons (KEY1-KEY9)
 *   Encoder: Rotary encoder (A/B/push)
 *   UART: UART1 115200 to main board via CN3
 *
 * Architecture: Bare-metal main loop + SysTick (no RTOS needed)
 *   - UART1 RX interrupt → ring buffer
 *   - SysTick 1ms tick
 *   - Main loop: key scan(20ms), display update(100ms), heartbeat(1s)
 *
 * First milestone: Send 0x84 heartbeat every 1s to eliminate COMM_FAULT buzzer.
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

/* GPIO Port C */
#define GPIOC_BASE          0x40011000
#define GPIOC_CRH           (*(volatile uint32_t *)(GPIOC_BASE + 0x04))
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
        while (!(USART1_SR & (1 << 7))) {}  /* Wait TXE */
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
    /* Enable USART2 clock (APB1, bit17) + GPIOA already enabled */
    RCC_APB1ENR |= (1 << 17);

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
        while (!(USART2_SR & (1 << 7))) {}  /* Wait TXE */
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

static void process_rx_frame(uint8_t cmd, const uint8_t *data, uint8_t len)
{
    switch (cmd) {
    case 0x01:  /* Display data (26 bytes) */
        if (len >= 26) {
            memcpy(s_display_data, data, 26);
            s_display_valid = 1;
        }
        break;

    case 0x04:  /* Heartbeat from main board (8 bytes) */
        /* We could extract runtime/uptime, but for now just acknowledge */
        break;

    default:
        break;
    }
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
    uint32_t crh = GPIOC_CRH;
    crh &= ~(0xFU << 20);
    crh |=  (0x2U << 20);
    GPIOC_CRH = crh;

    /* PB2 = push-pull output, 2MHz: CRL bits[11:8] = 0x2 */
    uint32_t crl = GPIOB_CRL;
    crl &= ~(0xFU << 8);
    crl |=  (0x2U << 8);
    GPIOB_CRL = crl;

    /* Start both HIGH (LED off on active-low boards) */
    GPIOC_BSRR = (1 << 13);
    GPIOB_BSRR = (1 << 2);
}

static void Debug_LED_Toggle(void)
{
    /* Toggle PC13 */
    if (GPIOC_ODR & (1 << 13))
        GPIOC_BSRR = (1 << (13 + 16));  /* Reset = LOW */
    else
        GPIOC_BSRR = (1 << 13);         /* Set = HIGH */

    /* Toggle PB2 */
    if (GPIOB_ODR & (1 << 2))
        GPIOB_BSRR = (1 << (2 + 16));
    else
        GPIOB_BSRR = (1 << 2);
}

/* ========================================================================= */
/*  TM1640 LED Driver (placeholder — will implement full driver later)       */
/* ========================================================================= */

/* TODO: Implement TM1640 bit-bang driver for U1 and U9 */
/* For now, just a stub that does nothing */

static void TM1640_Init(void)
{
    /* TODO: Configure DIN/SCLK GPIO pins as output */
    /* TODO: Send initialization commands to both TM1640 chips */
}

static void TM1640_AllOn(void)
{
    /* TODO: Turn on all segments for testing */
}

static void TM1640_AllOff(void)
{
    /* TODO: Turn off all segments */
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

    uint32_t last_heartbeat = 0;
    uint32_t last_display = 0;
    uint32_t last_blink = 0;
    uint8_t alarm_ack_counter = 0;

    /* Fast blink 5 times at startup to confirm boot (100ms on/off) */
    for (int i = 0; i < 10; i++) {
        Debug_LED_Toggle();
        uint32_t t0 = tick_ms();
        while (tick_ms() - t0 < 100) {}
    }

    while (1) {
        /* Process UART RX bytes — primary on UART2 (CN12) */
        while (uart2_rx_available()) {
            parse_rx_byte(uart2_rx_read());
        }
        /* Also drain UART1 in case CN11 level shifter gets fixed later */
        while (uart1_rx_available()) {
            parse_rx_byte(uart1_rx_read());
        }

        /* Blink debug LED every 500ms — visible proof firmware is running */
        if (tick_ms() - last_blink >= 500) {
            last_blink = tick_ms();
            Debug_LED_Toggle();
        }

        /* Send heartbeat every 1s */
        if (tick_ms() - last_heartbeat >= 1000) {
            last_heartbeat = tick_ms();
            send_heartbeat();

            /* Send alarm ack every 5s to keep COMM_FAULT cleared.
             * Main board resets acknowledged=false when alarm_flags reaches 0,
             * so we must re-send periodically in case COMM_FAULT re-latches. */
            alarm_ack_counter++;
            if (alarm_ack_counter >= 5) {
                alarm_ack_counter = 0;
                uint8_t ack_data = 0xFF;  /* 0xFF = acknowledge all alarms */
                send_frame(0x85, &ack_data, 1);
            }
        }

        /* Display update every 100ms (when TM1640 is implemented) */
        if (tick_ms() - last_display >= 100) {
            last_display = tick_ms();
            if (s_display_valid) {
                /* TODO: Parse s_display_data and update TM1640 */
            }
        }
    }
}
