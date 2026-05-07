/**
 * @file    main.c
 * @brief   HDICU-ZKB01A Screen Board Firmware — GD32F303RCT6
 *
 * Hardware:
 *   MCU: GD32F303RCT6 (Cortex-M4, compatible with STM32F303)
 *   Crystal: 8MHz HSE
 *   LED Driver: 2× TM1640 (DIN/SCLK serial)
 *   Keys: 9 independent buttons via XH2.54 connectors (CN1-CN9)
 *     CN1(PB12)=护理等级  CN3(PB13)=照明灯(断路)  CN5(PB14)=检查灯  CN7(PB15)=红蓝光
 *     CN2(PC6)=开放供氧   CN4(PC7)=内循环         CN6(PC8)=新风系统
 *     CN8(PC9)=照明灯(替代CN3)  CN9(PA8)=报警确认
 *   Encoder: Physical encoder on MAIN BOARD (CN17), not screen board.
 *            Screen board receives encoder events via UART protocol (0x86).
 *   Indicator LEDs (active-low, GPIO LOW=ON):
 *     LEDA1=PA5  LEDA2=PC4  LEDA3=PC5  LEDA4=PB10
 *     LEDA5=PC13 LEDA6=PA0  LEDA7=PC15 LEDA8=PA15
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

/* GPIO Port D — Stage 2 (2026-05-05): 启用以驱动 PD2 = LED10 风速3档指示灯
 * 其他 PDx 暂不使用。RCC_APB2ENR bit 5 在 LED_Fan_Init() 中开启。 */
#define GPIOD_BASE          0x40011400
#define GPIOD_CRL           (*(volatile uint32_t *)(GPIOD_BASE + 0x00))
#define GPIOD_BSRR          (*(volatile uint32_t *)(GPIOD_BASE + 0x10))

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

/* Display data from main board (0x01 packet).
 * Stage 8 (2026-05-06): 26 → 39 字节 (Codex v2 问题 1).
 *   byte 0-25:  v2.1 现有字段
 *   byte 26-37: setpoint (target_temp/humid/o2/co2/fog_time/disinfect_time)
 *   byte 38:    高 4 bit protocol_version (Stage 8 = 0x1) + 低 4 bit enable bitmap
 * 旧主板帧 (len=26) 仍可解析现有字段, 但 hmi_seed_from_display 不 seed setpoint,
 * shadow_valid=0, commit 被拒绝 (避免硬编码默认值被提交). */
#define SCR_DISPLAY_DATA_LEN_NEW 39
static uint8_t s_display_data[SCR_DISPLAY_DATA_LEN_NEW];
static uint8_t s_display_data_len;   /* 实际收到的字节数, 用于判断主板是否是 Stage 8 */
static uint8_t s_display_valid;

/* Stage 4 (2026-05-05): HMI 页面扩展为 8 页 (LIVE + 7 SET).
 * 编码器单击循环 LIVE → TEMP → HUMID → O2 → CO2 → FOG → DISINFECT → RESET_O2 → LIVE.
 * 详见改动确认书 v1.2 节 6.1. */
typedef enum {
    HMI_PAGE_LIVE = 0,
    HMI_PAGE_SET_TEMP,         /* 温度 setpoint, 步长 0.1°C */
    HMI_PAGE_SET_HUMID,        /* 湿度 setpoint, 步长 1% */
    HMI_PAGE_SET_O2,           /* O2 setpoint, 步长 1% */
    HMI_PAGE_SET_CO2,          /* CO2 阈值, 步长 100ppm */
    HMI_PAGE_SET_FOG,          /* 雾化时长, 步长 60s */
    HMI_PAGE_SET_DISINFECT,    /* 消毒时长, 步长 60s */
    HMI_PAGE_RESET_O2_TIME,    /* 清供氧累计 — 触发动作, 不是数字调节 */
    HMI_PAGE_COUNT
} HmiPage_t;

static HmiPage_t s_hmi_page = HMI_PAGE_LIVE;
static uint8_t   s_setpoint_seed_done;   /* Stage 8 重做: 改名自 s_hmi_shadow_valid (Codex v5 修正 3).
                                          * 1 = 已收到有效 Stage 8 39B 帧 (含版本字节 = 0x1).
                                          * 含义: 是否允许向主板下发 send_param_set / send_timer_ctrl,
                                          *       不再用作"阻挡用户操作"的开关. */
static uint32_t  s_hmi_last_input_tick;

/* shadow 设定值.
 * Stage 8 重做 (2026-05-07): 默认值改回合理范围内 (旋转旋出来的值不会触及边界).
 *   - 上电后第一帧未到时, seed_done=0, 旋转改 shadow 但不下发主板, 不影响安全
 *   - 第一帧来了之后 LIVE 页持续从 byte 26-37 同步 setpoint (H3 修正)
 *   - SET 页编辑期间不再被覆盖, 用户旋转才改 shadow */
static int16_t   s_set_temp_x10   = 360;     /* 36.0°C 默认 */
static uint16_t  s_set_hum_x10    = 500;     /* 50.0% 默认 */
static uint16_t  s_set_o2_x10     = 210;     /* 21.0% 默认 */
static uint16_t  s_set_co2_ppm    = 1000;    /* 1000 ppm 默认 (CO2 SET 页 D2=A 跳过, 此值仅作占位) */
static uint16_t  s_set_fog_sec    = 0;
static uint16_t  s_set_disinf_sec = 0;
/* RESET_O2_TIME 页是动作不是数值, 不需要 shadow */

/* Stage 8 重做 (Codex v4 H1 修订): 进入 SET 页时备份 setpoint 原值 + enable bit,
 * 长按取消时回滚到原状态 (D1=b 撤销).
 * 备份时机: hmi_cycle_page() 进入新 SET 页瞬间.
 * 翻页 SET_TEMP→SET_HUMID 时重新备份 humidity 原值 (前一页 temp 备份失效, 但已在主板生效). */
static int16_t   s_orig_temp_x10;
static uint16_t  s_orig_hum_x10;
static uint16_t  s_orig_o2_x10;
static uint8_t   s_orig_enable_temp;
static uint8_t   s_orig_enable_humid;
static uint8_t   s_orig_enable_o2;

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
#define TOTAL_KEYS          9   /* 9 buttons only (encoder is on main board) */
#define KEY_LONG_PRESS_MS   2000
static uint8_t  s_key_state[TOTAL_KEYS];       /* debounced: 0=released 1=pressed */
static uint8_t  s_key_debounce[TOTAL_KEYS];    /* consecutive same-reading count */
static uint32_t s_key_press_tick[TOTAL_KEYS];  /* tick when press detected */
static uint8_t  s_key_long_sent[TOTAL_KEYS];   /* long-press event already sent */

/* Encoder is on MAIN BOARD — screen board receives events via 0x86 command */

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

/* Primary TX: dual-send to CN11 (UART1, 主路, 经电平转换器→主板) +
 *             CN12 (UART2, 3.3V 直发→调试 PC).
 *
 * Stage 1 改动 (2026-05-05): 双发恢复 CN11 主路, 同时保留 CN12 调试备份.
 *   前提: CN11 旁电平转换器硬件已修复.
 *   主板从 UART_CH_SCREEN (USART1) 收 CN11 字节, 不接 CN12.
 *   即使 CN11 链路异常, CN12 上的字节流仍可在 PC 监控. */
static void uart_primary_send(const uint8_t *data, uint16_t len)
{
    uart1_send(data, len);   /* CN11 → 主板 */
    uart2_send(data, len);   /* CN12 → 调试 PC */
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

/* Stage 4: 0x83 TimerType + Cmd + Duration (复用现有主板协议)
 *   timer_type: 0x01=雾化 0x02=消毒 0x03=供氧累计
 *   cmd:        0x01=开始 0x02=停止 0x03=清零
 *   duration:   秒 (Cmd=0x03 清零时忽略, 但仍占 2 字节) */
static void send_timer_ctrl(uint8_t timer_type, uint8_t cmd, uint16_t duration)
{
    uint8_t payload[4];
    payload[0] = timer_type;
    payload[1] = cmd;
    payload[2] = (uint8_t)(duration >> 8);
    payload[3] = (uint8_t)(duration & 0xFF);
    send_frame(0x83, payload, 4);
}

static void hmi_touch(void)
{
    s_hmi_last_input_tick = tick_ms();
}

/* Stage 8 重做 (2026-05-07) hmi_seed_from_display 角色重定义:
 *   Codex v4 修正 3: shadow_valid → seed_done, 不阻挡用户操作, 仅控制是否下发主板.
 *   Codex v5 H3:    LIVE 页持续同步 setpoint (每 100ms 一帧都覆盖),
 *                   SET 页编辑期间不覆盖 shadow (避免破坏用户正在旋转的编辑).
 *   版本字节验证:   byte 38 高 4 bit 必须 = 0x1, 否则不是 Stage 8 主板, 拒绝 seed_done.
 *
 * 帧长度兼容性:
 *   len < 39        → 旧主板/异常帧, seed_done=0, 不下发, 但不阻挡用户操作
 *   len >= 39 + 版本=0x1 → seed_done=1, 允许旋转下发主板; LIVE 页同步 setpoint shadow */
static void hmi_seed_from_display(const uint8_t *data, uint8_t len)
{
    if (len < 39) {
        s_setpoint_seed_done = 0;
        return;
    }
    uint8_t version = (uint8_t)((data[38] >> 4) & 0x0F);
    if (version != 0x1) {
        s_setpoint_seed_done = 0;
        return;
    }

    /* H3: 仅 LIVE 页持续同步 shadow. SET 页 shadow 由用户旋转维护, 不被外部覆盖.
     * 副作用: iPad 在用户编辑期间改 setpoint, 用户旋转看不到; 用户长按撤销时回滚到"进 SET 页瞬间"的值,
     *         不是 iPad 最新值. 这是 last-writer-wins 设计 (用户主动操作优先于后台同步). */
    if (s_hmi_page == HMI_PAGE_LIVE) {
        s_set_temp_x10   = (int16_t)((data[26] << 8) | data[27]);
        s_set_hum_x10    = (uint16_t)((data[28] << 8) | data[29]);
        s_set_o2_x10     = (uint16_t)((data[30] << 8) | data[31]);
        s_set_co2_ppm    = (uint16_t)((data[32] << 8) | data[33]);
        s_set_fog_sec    = (uint16_t)((data[34] << 8) | data[35]);
        s_set_disinf_sec = (uint16_t)((data[36] << 8) | data[37]);

        /* clamp 防御 (主板自己也已 clamp, 这里是双保险) */
        if (s_set_temp_x10 < 100) s_set_temp_x10 = 100;
        if (s_set_temp_x10 > 400) s_set_temp_x10 = 400;
        if (s_set_hum_x10 < 300)  s_set_hum_x10  = 300;
        if (s_set_hum_x10 > 900)  s_set_hum_x10  = 900;
        if (s_set_o2_x10 < 210)   s_set_o2_x10   = 210;
        if (s_set_o2_x10 > 1000)  s_set_o2_x10   = 1000;
        if (s_set_co2_ppm > 5000) s_set_co2_ppm  = 5000;
        if (s_set_fog_sec > 3600) s_set_fog_sec  = 3600;
        if (s_set_disinf_sec > 3600) s_set_disinf_sec = 3600;
    }

    s_setpoint_seed_done = 1;
}

/* Stage 4: 8 页循环 (Stage 1 之前是 4 页)
 * Stage 8 (2026-05-06): 跳过 HMI_PAGE_SET_CO2 (Codex v2 问题 6, D2=A 保留).
 * Stage 8 重做 (2026-05-07): 进入 SET 页瞬间备份原 setpoint + 原 enable bit (Codex v4 H1, v5 M5).
 *   长按撤销时用 s_orig_xxx 回滚.
 *   翻页 SET_TEMP→SET_HUMID 时, 重新备份 humidity 原值. */
static void hmi_cycle_page(void)
{
    do {
        if (s_hmi_page + 1 >= HMI_PAGE_COUNT) {
            s_hmi_page = HMI_PAGE_LIVE;
        } else {
            s_hmi_page = (HmiPage_t)(s_hmi_page + 1);
        }
    } while (s_hmi_page == HMI_PAGE_SET_CO2);

    /* Stage 8 重做: 进入 SET 页时备份原 setpoint + 原 enable bit (D1=b 撤销用).
     * 仅备份 LIVE 页持续同步过来的最新 shadow + s_display_data[38] 解析的 enable bitmap.
     * seed_done=0 时备份的是默认 360/500/210, 不影响后续撤销 (没下发就没什么可撤销的). */
    switch (s_hmi_page) {
    case HMI_PAGE_SET_TEMP:
        s_orig_temp_x10    = s_set_temp_x10;
        s_orig_enable_temp = (uint8_t)(s_display_data[38] & 0x01);
        break;
    case HMI_PAGE_SET_HUMID:
        s_orig_hum_x10      = s_set_hum_x10;
        s_orig_enable_humid = (uint8_t)((s_display_data[38] & 0x02) ? 1 : 0);
        break;
    case HMI_PAGE_SET_O2:
        s_orig_o2_x10    = s_set_o2_x10;
        s_orig_enable_o2 = (uint8_t)((s_display_data[38] & 0x04) ? 1 : 0);
        break;
    /* 雾化/消毒/氧气计时不备份 — 长按是关停继电器/清累计, 不是回滚 setpoint */
    default:
        break;
    }
    hmi_touch();
}

/* Stage 8 重做 (2026-05-07) hmi_apply_encoder_delta:
 *   编码器旋转 → 改 shadow + **立即下发主板** (按客户流程图 C3 旋转即启动).
 *   - 温/湿/O2: send_param_set (主板 0x81 自动 enable_xxx_ctrl=1)
 *   - 雾化/消毒: send_timer_ctrl 立即启动继电器 (流程图: "旋转, 计时增加同时雾化继电器启动")
 *   - 氧气计时: 不响应旋转 (流程图仅长按操作)
 *   - CO2 SET 页: D2=A 跳过, 但保留 case 兼容 Stage 9
 *
 *   下发保护 (Codex v5 修正 3): 仅当 s_setpoint_seed_done=1 才下发, 否则改 shadow 但不发,
 *   防止上电首帧未到时把屏幕板默认 360/500/210 误写入主板. */
static void hmi_apply_encoder_delta(int8_t delta)
{
    if (delta == 0) return;
    if (s_hmi_page == HMI_PAGE_LIVE || s_hmi_page == HMI_PAGE_RESET_O2_TIME) return;

    while (delta > 0) {
        switch (s_hmi_page) {
        case HMI_PAGE_SET_TEMP:
            if (s_set_temp_x10 < 400) {
                s_set_temp_x10++;
                if (s_setpoint_seed_done) send_param_set(0x01, (uint16_t)s_set_temp_x10);
            }
            break;
        case HMI_PAGE_SET_HUMID:
            if (s_set_hum_x10 < 900) {
                s_set_hum_x10 += 10;
                if (s_setpoint_seed_done) send_param_set(0x02, s_set_hum_x10);
            }
            break;
        case HMI_PAGE_SET_O2:
            if (s_set_o2_x10 < 1000) {
                s_set_o2_x10 += 10;
                if (s_setpoint_seed_done) send_param_set(0x03, s_set_o2_x10);
            }
            break;
        case HMI_PAGE_SET_CO2:
            if (s_set_co2_ppm <= 5000 - 100) {
                s_set_co2_ppm += 100;
                if (s_setpoint_seed_done) send_param_set(0x06, s_set_co2_ppm);
            }
            break;
        case HMI_PAGE_SET_FOG:
            if (s_set_fog_sec <= 3600 - 60) {
                s_set_fog_sec += 60;
                if (s_setpoint_seed_done) send_timer_ctrl(0x01, 0x01, s_set_fog_sec);
            }
            break;
        case HMI_PAGE_SET_DISINFECT:
            if (s_set_disinf_sec <= 3600 - 60) {
                s_set_disinf_sec += 60;
                if (s_setpoint_seed_done) send_timer_ctrl(0x02, 0x01, s_set_disinf_sec);
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
                if (s_setpoint_seed_done) send_param_set(0x01, (uint16_t)s_set_temp_x10);
            }
            break;
        case HMI_PAGE_SET_HUMID:
            if (s_set_hum_x10 > 300) {
                s_set_hum_x10 -= 10;
                if (s_setpoint_seed_done) send_param_set(0x02, s_set_hum_x10);
            }
            break;
        case HMI_PAGE_SET_O2:
            if (s_set_o2_x10 > 210) {
                s_set_o2_x10 -= 10;
                if (s_setpoint_seed_done) send_param_set(0x03, s_set_o2_x10);
            }
            break;
        case HMI_PAGE_SET_CO2:
            if (s_set_co2_ppm >= 100) {
                s_set_co2_ppm -= 100;
                if (s_setpoint_seed_done) send_param_set(0x06, s_set_co2_ppm);
            }
            break;
        case HMI_PAGE_SET_FOG:
            if (s_set_fog_sec >= 60) {
                s_set_fog_sec -= 60;
                if (s_setpoint_seed_done) {
                    /* fog_sec=0 时, 改发停止 (cmd=0x02) */
                    send_timer_ctrl(0x01, (s_set_fog_sec > 0) ? 0x01 : 0x02, s_set_fog_sec);
                }
            }
            break;
        case HMI_PAGE_SET_DISINFECT:
            if (s_set_disinf_sec >= 60) {
                s_set_disinf_sec -= 60;
                if (s_setpoint_seed_done) {
                    send_timer_ctrl(0x02, (s_set_disinf_sec > 0) ? 0x01 : 0x02, s_set_disinf_sec);
                }
            }
            break;
        default:
            break;
        }
        delta++;
    }

    hmi_touch();
}

/* Stage 8 重做 (2026-05-07): 长按 SET 页撤销, 回滚 setpoint + enable 到进入 SET 页瞬间的原值.
 * Codex v4 H1: 必须同时回滚 setpoint 和 enable, 否则原本 enable=0 的通道会因旋转被自动 enable=1.
 * Codex v5 H1+: 没收到 39B 帧时不能回滚 (s_orig_enable_xxx 来自旧/0 的 byte 38, 不可靠).
 *
 * 实现方式 (两条命令, 不原子, 200ms 闪启动可接受):
 *   1. send_param_set(原 setpoint) → 主板 0x81 自动 enable=1
 *   2. 若原 enable=0, 再发虚拟 KeyID 0x0C/0x0D/0x0E action=0x02 → 主板 enable 清 0
 * 闪启动 ≤ 200ms (主板 ControlTask 周期), 单次极短, 继电器最多动作 1 次, 可接受. */
static void send_key_action(uint8_t key_id, uint8_t action_type);   /* forward decl 提前到 hmi_revert 之前 */

static uint8_t hmi_revert_current_set_page(void)
{
    if (!s_setpoint_seed_done) return 0;   /* 没收到有效 39B 帧, 原值不可信, 拒绝撤销 */
    switch (s_hmi_page) {
    case HMI_PAGE_SET_TEMP:
        s_set_temp_x10 = s_orig_temp_x10;
        send_param_set(0x01, (uint16_t)s_orig_temp_x10);
        if (s_orig_enable_temp == 0) send_key_action(0x0C, 0x02);   /* 虚拟 KeyID 清 enable_temp */
        return 1;
    case HMI_PAGE_SET_HUMID:
        s_set_hum_x10 = s_orig_hum_x10;
        send_param_set(0x02, s_orig_hum_x10);
        if (s_orig_enable_humid == 0) send_key_action(0x0D, 0x02);
        return 1;
    case HMI_PAGE_SET_O2:
        s_set_o2_x10 = s_orig_o2_x10;
        send_param_set(0x03, s_orig_o2_x10);
        if (s_orig_enable_o2 == 0) send_key_action(0x0E, 0x02);
        return 1;
    /* CO2 / 雾化 / 消毒 / 氧气计时 长按是另一种处理, 不走撤销 */
    default:
        return 0;
    }
}

static void send_key_action(uint8_t key_id, uint8_t action_type);  /* forward decl */

static void process_rx_frame(uint8_t cmd, const uint8_t *data, uint8_t len)
{
    switch (cmd) {
    case 0x01:  /* Display data — Stage 8 (2026-05-06): 26 → 39 字节
                 *   旧主板 (Stage 7) 帧 len=26 仍可解析现有字段 (byte 0-25),
                 *   但 hmi_seed 不 seed setpoint, shadow_valid=0, commit 拒绝.
                 *   新主板 (Stage 8) 帧 len=39, 完整 seed setpoint + 暴露 enable bitmap. */
        if (len >= 22) {
            uint8_t copy_len = (len < SCR_DISPLAY_DATA_LEN_NEW) ? len : SCR_DISPLAY_DATA_LEN_NEW;
            memcpy(s_display_data, data, copy_len);
            s_display_data_len = copy_len;
            s_display_valid = 1;
            hmi_seed_from_display(data, copy_len);
        }
        break;

    case 0x04:  /* Heartbeat from main board (8 bytes) */
        break;

    case 0x06:  /* Encoder event from main board
                 * data[0] = event type: 0x01=push click, 0x02=push long, 0x03=rotation
                 * data[1] = rotation delta (int8, signed: +CW, -CCW) — only for type 0x03
                 *
                 * Stage 8 重做 (2026-05-07) 状态机 — 按客户流程图重写:
                 *   单击 (流程图: 仅翻页, 不 commit):
                 *     LIVE → 翻 SET_TEMP
                 *     SET_TEMP → SET_HUMID → SET_O2 → [SET_CO2 跳过] → SET_FOG → SET_DISINFECT → 氧气计时 → LIVE
                 *     RESET_O2_TIME(=氧气计时) → 翻 LIVE (流程图: 长按才清累计, 单击是翻页)
                 *   长按 (流程图按页面分情况):
                 *     LIVE → 0x0A action=0x02 (清供氧累计, b 方案保留)
                 *     温/湿/O2 SET → 撤销: 回滚 setpoint + 回滚 enable + 回 LIVE (D1=b)
                 *     CO2 SET → 跳过 (D2=A 不可达)
                 *     雾化 SET → 0x83 type=1 cmd=2 关继电器 + 时间清 0 + 回 LIVE
                 *     消毒 SET → 0x83 type=2 cmd=2 关 + 回 LIVE
                 *     氧气计时(RESET_O2_TIME) → 0x83 type=3 cmd=4 清累计+关 O2 阀+回 LIVE (Stage 8 新)
                 *   旋转:
                 *     温/湿/O2/CO2 SET → 改 shadow + 立即 send_param_set (Stage 7 行为, C3)
                 *     雾化/消毒 SET → 改 shadow + 立即 send_timer_ctrl (流程图: 旋转即启动)
                 *     氧气计时 / LIVE → 不响应 */
        if (len >= 1) {
            uint8_t evt = data[0];
            if (evt == 0x01) {
                /* 单击: 仅翻页 */
                hmi_cycle_page();
            } else if (evt == 0x02) {
                /* 长按 */
                switch (s_hmi_page) {
                case HMI_PAGE_LIVE:
                    /* b 方案保留: LIVE 页长按 = 清供氧累计 */
                    send_key_action(0x0A, 0x02);
                    break;
                case HMI_PAGE_SET_TEMP:
                case HMI_PAGE_SET_HUMID:
                case HMI_PAGE_SET_O2:
                    /* D1=b 撤销: 回滚 setpoint + enable, 回 LIVE.
                     * 失败 (seed_done=0) 时仅退页, 不发任何命令. */
                    (void)hmi_revert_current_set_page();
                    s_hmi_page = HMI_PAGE_LIVE;
                    hmi_touch();
                    break;
                case HMI_PAGE_SET_CO2:
                    /* D2=A 跳过, 不可达 — 防御性退页 */
                    s_hmi_page = HMI_PAGE_LIVE;
                    hmi_touch();
                    break;
                case HMI_PAGE_SET_FOG:
                    /* 流程图: 长按取消雾化, 关继电器 + 时间清 0 */
                    if (s_setpoint_seed_done) {
                        send_timer_ctrl(0x01, 0x02, 0);
                    }
                    s_set_fog_sec = 0;
                    s_hmi_page = HMI_PAGE_LIVE;
                    hmi_touch();
                    break;
                case HMI_PAGE_SET_DISINFECT:
                    if (s_setpoint_seed_done) {
                        send_timer_ctrl(0x02, 0x02, 0);
                    }
                    s_set_disinf_sec = 0;
                    s_hmi_page = HMI_PAGE_LIVE;
                    hmi_touch();
                    break;
                case HMI_PAGE_RESET_O2_TIME:
                    /* 氧气计时长按: 清累计 + 关 O2 阀 + open_o2=0 + enable_o2_ctrl=0 */
                    if (s_setpoint_seed_done) {
                        send_timer_ctrl(0x03, 0x04, 0);
                    }
                    s_hmi_page = HMI_PAGE_LIVE;
                    hmi_touch();
                    break;
                default:
                    s_hmi_page = HMI_PAGE_LIVE;
                    hmi_touch();
                    break;
                }
            } else if (evt == 0x03 && len >= 2) {
                /* 旋转 → hmi_apply_encoder_delta 内部处理立即下发 + seed_done 保护 */
                int8_t delta = (int8_t)data[1];
                hmi_apply_encoder_delta(delta);
            }
        }
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

    /* === U1 GRID10-13: 报警 + 进度条 + 供氧指示 === */
    {
        uint16_t relay_status = (uint16_t)((d[16] << 8) | d[17]);
        uint16_t fog_total    = (uint16_t)((d[22] << 8) | d[23]);
        uint16_t dis_total    = (uint16_t)((d[24] << 8) | d[25]);
        uint8_t  o2_on        = (relay_status & (1U << 4)) ? 1 : 0;

        /* DIGA11 (buf[10]): 雾化进度条 — 4档 (remaining/total) */
        if (fog_sec > 0 && fog_total > 0) {
            uint8_t pct = (uint8_t)((uint32_t)fog_sec * 100 / fog_total);
            if (pct > 100) pct = 100;
            if      (pct >= 75) s_u1_buf[10] = 0xFF;  /* 4/4 全亮 */
            else if (pct >= 50) s_u1_buf[10] = 0x7F;  /* 3/4 */
            else if (pct >= 25) s_u1_buf[10] = 0x3F;  /* 2/4 */
            else                s_u1_buf[10] = 0x06;   /* 1/4 少量 */
        } else {
            s_u1_buf[10] = 0x00;
        }

        /* DIGA12 (buf[11]): 消毒进度条 — 4档 (remaining/total) */
        if (dis_sec > 0 && dis_total > 0) {
            uint8_t pct = (uint8_t)((uint32_t)dis_sec * 100 / dis_total);
            if (pct > 100) pct = 100;
            if      (pct >= 75) s_u1_buf[11] = 0xFF;
            else if (pct >= 50) s_u1_buf[11] = 0x7F;
            else if (pct >= 25) s_u1_buf[11] = 0x3F;
            else                s_u1_buf[11] = 0x06;
        } else {
            s_u1_buf[11] = 0x00;
        }

        /* DIGA13 (buf[12]): 供氧指示灯 — O2阀开=亮, 关=灭 */
        s_u1_buf[12] = o2_on ? 0xFF : 0x00;
    }

    /* === Stage 4 (2026-05-05): 仅当前编辑项 GRID 闪烁 (0.5Hz 全灭/全亮) ===
     * 旧逻辑 = 实测值↔设定值交替 500ms 切换.
     * 新逻辑:
     *   blink_phase=0 (亮 0.5s): 当前页对应的 GRID 位写 setpoint 数字
     *   blink_phase=1 (灭 0.5s): 当前页对应的 GRID 位写 0x00
     *   其他 GRID 位不动 (实测温/湿/氧/CO2 等照常显示, 不受闪烁影响)
     *
     * GRID 位映射 (与上面 update_display_from_data 的 LIVE 显示位一致):
     *   温度    → s_u9_buf[0..2]   (3 位含 DP)
     *   湿度    → s_u9_buf[3..4]   (2 位)
     *   O2      → s_u9_buf[5..6]   (2 位)
     *   CO2     → s_u9_buf[7..10]  (4 位)
     *   雾化    → s_u1_buf[0..1]   (2 位, 显示为分钟)
     *   消毒    → s_u1_buf[2..3]   (2 位, 显示为分钟)
     *   清供氧  → s_u1_buf[4..9]   (6 位, 显示当前累计 HH:MM:SS, 仅闪烁,
     *                                按编码器键触发清零)
     */
    if (s_hmi_page != HMI_PAGE_LIVE) {
        uint8_t blink_off = (uint8_t)((tick_ms() / 500U) & 0x1U);   /* 0=亮 1=灭 */

        switch (s_hmi_page) {
        case HMI_PAGE_SET_TEMP: {
            uint8_t grids[] = {0, 1, 2};
            if (blink_off) {
                s_u9_buf[0] = s_u9_buf[1] = s_u9_buf[2] = 0x00;
            } else {
                int32_t tv = (s_set_temp_x10 < 0) ? -s_set_temp_x10 : s_set_temp_x10;
                if (tv > 999) tv = 999;
                display_digits(s_u9_buf, grids, 3, tv, 0, 1);
                if (s_set_temp_x10 < 0) s_u9_buf[0] = SEG_FONT[SEG_DASH];
            }
            break;
        }
        case HMI_PAGE_SET_HUMID: {
            uint8_t grids[] = {3, 4};
            if (blink_off) {
                s_u9_buf[3] = s_u9_buf[4] = 0x00;
            } else {
                display_digits(s_u9_buf, grids, 2, s_set_hum_x10 / 10U, 0, -1);
            }
            break;
        }
        case HMI_PAGE_SET_O2: {
            uint8_t grids[] = {5, 6};
            if (blink_off) {
                s_u9_buf[5] = s_u9_buf[6] = 0x00;
            } else {
                display_digits(s_u9_buf, grids, 2, s_set_o2_x10 / 10U, 0, -1);
            }
            break;
        }
        case HMI_PAGE_SET_CO2: {
            uint8_t grids[] = {7, 8, 9, 10};
            if (blink_off) {
                s_u9_buf[7] = s_u9_buf[8] = s_u9_buf[9] = s_u9_buf[10] = 0x00;
            } else {
                display_digits(s_u9_buf, grids, 4, s_set_co2_ppm, 0, -1);
            }
            break;
        }
        case HMI_PAGE_SET_FOG: {
            uint8_t grids[] = {0, 1};
            uint16_t fog_min = s_set_fog_sec / 60;
            if (fog_min > 99) fog_min = 99;
            if (blink_off) {
                s_u1_buf[0] = s_u1_buf[1] = 0x00;
            } else {
                display_digits(s_u1_buf, grids, 2, fog_min, 1, -1);
            }
            break;
        }
        case HMI_PAGE_SET_DISINFECT: {
            uint8_t grids[] = {2, 3};
            uint16_t dis_min = s_set_disinf_sec / 60;
            if (dis_min > 99) dis_min = 99;
            if (blink_off) {
                s_u1_buf[2] = s_u1_buf[3] = 0x00;
            } else {
                display_digits(s_u1_buf, grids, 2, dis_min, 1, -1);
            }
            break;
        }
        case HMI_PAGE_RESET_O2_TIME:
            /* 显示当前累计 (来自 0x01 包 byte 14-15 = o2_accumulated 秒, 已由 LIVE
             * 显示时写到 s_u1_buf[4..9]) — 闪烁全灭/恢复. 按编码器键触发清零. */
            if (blink_off) {
                s_u1_buf[4] = s_u1_buf[5] = s_u1_buf[6] = 0x00;
                s_u1_buf[7] = s_u1_buf[8] = s_u1_buf[9] = 0x00;
            }
            /* 不灭时保持 LIVE 显示已写入的 HH:MM:SS, 不另写 */
            break;
        default:
            break;
        }
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
/*  LED_Fan — 风速档位指示灯 LED8/LED9/LED10 (Stage 2, 2026-05-05)            */
/*  Circuit: 3V3 → R → LED → GPIO pin  (active-low: LOW=ON, 与 LEDA1-8 一致)  */
/*  LED8  = PA4  (风速 1 档亮)                                                */
/*  LED9  = PB9  (风速 2 档亮)                                                */
/*  LED10 = PD2  (风速 3 档亮)                                                */
/*  显示语义 = B 累加: fan=1 亮 LED8, fan=2 亮 LED8+LED9, fan=3 三颗全亮       */
/* ========================================================================= */

static void LED_Fan_Init(void)
{
    /* Enable GPIO clocks: A(bit2), B(bit3), C(bit4), D(bit5) */
    RCC_APB2ENR |= (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5);

    /* PA4 (LED8) — CRL bits[19:16], output PP 2MHz (nibble = 0x2) */
    {
        uint32_t crl = GPIOA_CRL;
        crl &= ~(0xFU << 16);
        crl |=  (0x2U << 16);
        GPIOA_CRL = crl;
        GPIOA_BSRR = (1 << 4);     /* HIGH = off (active-low) */
    }

    /* PB9 (LED9) — CRH bits[7:4] */
    {
        uint32_t crh = GPIOB_CRH;
        crh &= ~(0xFU << 4);
        crh |=  (0x2U << 4);
        GPIOB_CRH = crh;
        GPIOB_BSRR = (1 << 9);     /* HIGH = off */
    }

    /* PD2 (LED10) — CRL bits[11:8] */
    {
        uint32_t crl = GPIOD_CRL;
        crl &= ~(0xFU << 8);
        crl |=  (0x2U << 8);
        GPIOD_CRL = crl;
        GPIOD_BSRR = (1 << 2);     /* HIGH = off */
    }
}

/* ========================================================================= */
/*  LEDA Indicator LEDs — 8 green LEDs next to panel buttons                 */
/*  Circuit: 3V3 → R(680Ω) → LED → GPIO pin  (active-low: LOW=ON)          */
/*  LEDA1=PA5(KEY1) LEDA2=PC4(KEY2) LEDA3=PC5(KEY3) LEDA4=PB10(KEY4)       */
/*  LEDA5=PC13(KEY5) LEDA6=PA0(KEY6) LEDA7=PC15(KEY7) LEDA8=PA15(KEY8)     */
/* ========================================================================= */

static void LEDA_Init(void)
{
    /* Enable GPIO clocks: A(bit2), B(bit3), C(bit4) */
    RCC_APB2ENR |= (1 << 2) | (1 << 3) | (1 << 4);

    /* All LEDA pins: push-pull output, 2MHz (nibble = 0x2) */

    /* --- GPIOA: PA0[3:0], PA5[23:20], PA15 via CRH[31:28] --- */
    uint32_t crl = GPIOA_CRL;
    crl &= ~(0xFU << 0);    /* PA0 */
    crl |=  (0x2U << 0);
    crl &= ~(0xFU << 20);   /* PA5 */
    crl |=  (0x2U << 20);
    GPIOA_CRL = crl;
    uint32_t crh = GPIOA_CRH;
    crh &= ~(0xFU << 28);   /* PA15 */
    crh |=  (0x2U << 28);
    GPIOA_CRH = crh;
    /* Default HIGH = LEDs off */
    GPIOA_BSRR = (1 << 0) | (1 << 5) | (1 << 15);

    /* --- GPIOB: PB10[11:8] (CRH) — LEDA4 红蓝光指示 --- */
    {
        uint32_t b_crh = GPIOB_CRH;
        b_crh &= ~(0xFU << 8);     /* PB10 bits[11:8] in CRH */
        b_crh |=  (0x2U << 8);     /* Output PP 2MHz */
        GPIOB_CRH = b_crh;
        GPIOB_BSRR = (1 << 10);    /* HIGH = off */
    }

    /* --- GPIOC: PC4[19:16], PC5[23:20], PC13 via CRH[23:20], PC15 via CRH[31:28] --- */
    crl = GPIOC_CRL;
    crl &= ~(0xFU << 16);   /* PC4 */
    crl |=  (0x2U << 16);
    crl &= ~(0xFU << 20);   /* PC5 */
    crl |=  (0x2U << 20);
    GPIOC_CRL = crl;
    crh = GPIOC_CRH;
    crh &= ~(0xFU << 20);   /* PC13 */
    crh |=  (0x2U << 20);
    crh &= ~(0xFU << 28);   /* PC15 */
    crh |=  (0x2U << 28);
    GPIOC_CRH = crh;
    GPIOC_BSRR = (1 << 4) | (1 << 5) | (1 << 13) | (1 << 15);  /* HIGH = off */
}

/* Drive LEDA1-8 from main board 0x01 packet data.
 * Active-low: BSRR set bit = HIGH = off, reset bit = LOW = on. */
static void LEDA_Update(void)
{
    if (!s_display_valid) return;
    const uint8_t *d = s_display_data;

    uint8_t  nursing   = d[9];                                    /* nursing_level */
    uint16_t relay_st  = (uint16_t)((d[16] << 8) | d[17]);       /* relay_status */
    uint8_t  light_st  = d[18];                                   /* light_status */
    uint8_t  switch_st = d[19];                                   /* switch_status */

    /* LEDA1 PA5:  KEY1 护理等级 — 任何等级激活时亮 */
    if (nursing > 0)  GPIOA_BSRR = (1 << (5 + 16));   /* LOW=on */
    else              GPIOA_BSRR = (1 << 5);           /* HIGH=off */

    /* LEDA2 PC4:  KEY2 照明灯 — light_status bit1 */
    if (light_st & (1 << 1)) GPIOC_BSRR = (1 << (4 + 16));
    else                     GPIOC_BSRR = (1 << 4);

    /* LEDA3 PC5:  KEY3 检查灯 — light_status bit0 */
    if (light_st & (1 << 0)) GPIOC_BSRR = (1 << (5 + 16));
    else                     GPIOC_BSRR = (1 << 5);

    /* LEDA4 PB10: KEY4 红蓝光 — light_status bit2 or bit3 */
    if (light_st & ((1 << 2) | (1 << 3))) GPIOB_BSRR = (1 << (10 + 16));
    else                                   GPIOB_BSRR = (1 << 10);

    /* LEDA5 PC13: CN2 开放式供氧 — relay O2 bit4 */
    if (relay_st & (1 << 4)) GPIOC_BSRR = (1 << (13 + 16));
    else                     GPIOC_BSRR = (1 << 13);

    /* LEDA6 PA0:  CN4 内循环 — switch_status bit0 */
    if (switch_st & (1 << 0)) GPIOA_BSRR = (1 << (0 + 16));
    else                      GPIOA_BSRR = (1 << 0);

    /* LEDA7 PC15: CN6 新风系统 — switch_status bit1 */
    if (switch_st & (1 << 1)) GPIOC_BSRR = (1 << (15 + 16));
    else                      GPIOC_BSRR = (1 << 15);

    /* === Stage 5 (2026-05-05): LEDA8 + LED8/9/10 改基于"等效 PTC duty" ===
     *
     * 含义: 屏幕板没有直接 PTC duty 字段, 但能从 0x01 包推算 PTC 风机当前实际转速:
     *   user_duty:  fan_speed_actual (0/1/2/3 → 0/30/60/100)
     *   fresh:      switch_status bit1 (新风) → 100
     *   heating:    relay_status bit 0 (BSP_RELAY_PTC_IO) → 80 (主板 max3 仲裁的 safety_min)
     *   等效 duty = max3(user, fresh ? 100 : 0, heating ? 80 : 0)
     *
     * 与主板 ControlTask 末尾 pwm_set_ptc_arbiter() 完全等价 (镜像计算).
     *
     * LED 显示语义 (累加, B 方案):
     *   LED8  on if 等效 >= 30   (任何风机活动: fan>=1 / 加热 / 新风)
     *   LED9  on if 等效 >= 60   (中速以上: fan>=2 / 加热 80 / 新风)
     *   LED10 on if 等效 >= 100  (满速: fan=3 / 新风, 加热 80% 不点)
     *   LEDA8 (PA15) on if 等效 >= 1   (任何风机活动)
     *
     * Stage 5 同步解绑: LEDA8 不再跟 light_status bit1 (原"CN8 替代照明"语义已废弃).
     */
    {
        uint8_t fan      = d[8];                          /* fan_speed_actual 0~3 */
        uint8_t heating  = (relay_st & (1U << 0)) ? 1 : 0;  /* BSP_RELAY_PTC_IO bit 0 */
        uint8_t fresh    = (switch_st & (1U << 1)) ? 1 : 0; /* SW_BIT_FRESH_AIR */

        /* fan_speed → 用户 duty (与主板 pwm_fan_level_duty[] 一致) */
        uint8_t user_duty = (fan == 0) ? 0 :
                            (fan == 1) ? 30 :
                            (fan == 2) ? 60 : 100;
        uint8_t eff_duty = user_duty;
        if (fresh   && 100 > eff_duty) eff_duty = 100;
        if (heating &&  80 > eff_duty) eff_duty = 80;

        /* LED8  PA4  — 风机活动起步 (等效 ≥ 30) */
        if (eff_duty >= 30) GPIOA_BSRR = (1 << (4 + 16));
        else                GPIOA_BSRR = (1 << 4);

        /* LED9  PB9  — 中速以上 (等效 ≥ 60) */
        if (eff_duty >= 60) GPIOB_BSRR = (1 << (9 + 16));
        else                GPIOB_BSRR = (1 << 9);

        /* LED10 PD2  — 满速 (等效 = 100, 仅 fan=3 或新风触发) */
        if (eff_duty >= 100) GPIOD_BSRR = (1 << (2 + 16));
        else                 GPIOD_BSRR = (1 << 2);

        /* LEDA8 PA15 — 风机活动总指示 (任何 ≥ 1) */
        if (eff_duty >= 1) GPIOA_BSRR = (1 << (15 + 16));
        else               GPIOA_BSRR = (1 << 15);
    }
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
/*    KEY9=PA8   (Encoder is on main board, not screen board)              */
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

    /* --- GPIOA: PA8(KEY9) --- */
    /* Encoder (PA6/PA7) is on MAIN BOARD, not screen board */
    uint32_t crl;  /* reused below for GPIOC_CRL */
    /* PA8: CRH[3:0] */
    uint32_t crh = GPIOA_CRH;
    crh &= ~0xFU;                  /* Clear PA8 */
    crh |=  0x8U;                  /* Input pull-up/down */
    GPIOA_CRH = crh;
    /* Enable pull-ups: set ODR bits */
    GPIOA_ODR |= (1 << 8);  /* Pull-up for PA8 (KEY9) */

    /* --- GPIOB: PB12-PB15(KEY1-KEY4) --- */
    /* PB2: not used on screen board (encoder is on main board) */
    /* PB12: CRH[19:16], PB13: [23:20], PB14: [27:24], PB15: [31:28] */
    crh = GPIOB_CRH;
    crh &= ~0xFFFF0000U;           /* Clear PB12-PB15 */
    crh |=  0x88880000U;           /* All input pull-up/down */
    GPIOB_CRH = crh;
    /* Enable pull-ups */
    GPIOB_ODR |= (1 << 12) | (1 << 13) | (1 << 14) | (1 << 15);

    /* --- GPIOC: PC6-PC9 (KEY5-KEY8) --- */
    /* PC6: CRL[27:24], PC7: CRL[31:28] */
    crl = GPIOC_CRL;
    crl &= ~(0xFFU << 24);
    crl |=  (0x88U << 24);
    GPIOC_CRL = crl;
    /* PC8: CRH[3:0], PC9: CRH[7:4] */
    crh = GPIOC_CRH;
    crh &= ~0xFFU;                 /* Clear PC8, PC9 (PC13 now LEDA5, configured by LEDA_Init) */
    crh |=  0x88U;
    GPIOC_CRH = crh;
    GPIOC_ODR |= (1 << 6) | (1 << 7) | (1 << 8) | (1 << 9);

    /* Encoder on main board — no local init needed */
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

/* Key ID table: index 0-9 → protocol key ID 0x01-0x0B
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
 *   0x0A 编码器按下 (HMI单击; 长按清零供氧累计)
 *   0x0B 风速档位键 (主板 fan_speed 循环 0→1→2→3→0) — Stage 1 (2026-05-05) */
static const uint8_t KEY_ID_MAP[TOTAL_KEYS] = {
    0x01,  /* [0] CN1/PB12: 护理等级 */
    0x02,  /* [1] CN3/PB13: 照明灯 (CN3硬件已修复, Stage 1 恢复原映射) */
    0x03,  /* [2] CN5/PB14: 检查灯 */
    0x04,  /* [3] CN7/PB15: 红蓝光 */
    0x06,  /* [4] CN2/PC6:  开放式供氧 (原理图标注: 开放式供养) */
    0x07,  /* [5] CN4/PC7:  内循环 */
    0x08,  /* [6] CN6/PC8:  新风系统 */
    0x0B,  /* [7] CN8/PC9:  风速档位键 (Stage 1 改: 不再做照明灯替代) */
    0x09,  /* [8] CN9/PA8:  报警确认 */
    /* 紫外灯(0x05)无面板按键, 仅iPad/定时器控制 */
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

    /* Encoder rotation is on main board — screen board receives events via 0x86 */
}

/* Encoder is on main board. Screen board receives encoder events via 0x86 command.
 * HMI: push=cycle pages, rotate=adjust parameter value, 5s timeout. */

/* ========================================================================= */
/*  Main                                                                     */
/* ========================================================================= */

int main(void)
{
    SystemClock_Config();
    SysTick_Init();
    LEDA_Init();
    LED_Fan_Init();   /* Stage 2: 风速档位 LED8/9/10 (PA4/PB9/PD2) + GPIOD 时钟 */
    UART1_Init();
    UART2_Init();
    TM1640_Init();
    Key_Init();
    IWDG_Init();  /* P2 fix: 2s watchdog — resets MCU if main loop hangs */

    uint32_t last_heartbeat = 0;
    uint32_t last_display = 0;
    uint32_t last_key_scan = 0;

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

        /* Key scan every 20ms */
        if (tick_ms() - last_key_scan >= 20) {
            last_key_scan = tick_ms();
            Key_Scan();
        }

        if (s_hmi_page != HMI_PAGE_LIVE && (tick_ms() - s_hmi_last_input_tick >= HMI_EDIT_TIMEOUT_MS)) {
            s_hmi_page = HMI_PAGE_LIVE;
            /* Stage 8 重做 (2026-05-07): 5s 超时仅退页, 不动 seed_done.
             * 因为旋转已立即下发, setpoint 已生效; 超时不算"撤销", 仅退出页面.
             * 仅长按才是真正的"撤销 + 回滚" (Codex v5 M9). */
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
                LEDA_Update();
            }
        }
    }
}
