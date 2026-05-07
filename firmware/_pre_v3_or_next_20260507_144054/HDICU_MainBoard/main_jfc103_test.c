/**
 * @file    main_jfc103_test.c
 * @brief   JFC103 sensor test — send 0x8A once, then listen
 *          Forwards received UART5 data to UART4 (CN16) for monitoring.
 */
#include "stm32f1xx_hal.h"
#include <string.h>

void SysTick_Handler(void) { HAL_IncTick(); }

static UART_HandleTypeDef huart4, huart5;

static void uart4_send(const char *s)
{
    HAL_UART_Transmit(&huart4, (uint8_t *)s, strlen(s), 200);
}

static void uart4_hex(uint8_t val)
{
    const char hex[] = "0123456789ABCDEF";
    char buf[3] = { hex[val >> 4], hex[val & 0x0F], ' ' };
    HAL_UART_Transmit(&huart4, (uint8_t *)buf, 3, 50);
}

int main(void)
{
    HAL_Init();

    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_AFIO_REMAP_SWJ_NOJTAG();

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_UART4_CLK_ENABLE();
    __HAL_RCC_UART5_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};

    /* UART4 TX = PC10 (CN16, monitoring output at 9600) */
    gpio.Pin = GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOC, &gpio);

    /* UART5 TX = PC12 */
    gpio.Pin = GPIO_PIN_12;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOC, &gpio);

    /* UART5 RX = PD2 */
    gpio.Pin = GPIO_PIN_2;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOD, &gpio);

    /* UART4 at 9600 for USB-TTL monitoring */
    huart4.Instance = UART4;
    huart4.Init.BaudRate = 9600;
    huart4.Init.WordLength = UART_WORDLENGTH_8B;
    huart4.Init.StopBits = UART_STOPBITS_1;
    huart4.Init.Parity = UART_PARITY_NONE;
    huart4.Init.Mode = UART_MODE_TX;
    huart4.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    HAL_UART_Init(&huart4);

    /* UART5 at 38400 for JFC103 */
    huart5.Instance = UART5;
    huart5.Init.BaudRate = 38400;
    huart5.Init.WordLength = UART_WORDLENGTH_8B;
    huart5.Init.StopBits = UART_STOPBITS_1;
    huart5.Init.Parity = UART_PARITY_NONE;
    huart5.Init.Mode = UART_MODE_TX_RX;
    huart5.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    HAL_UART_Init(&huart5);

    uart4_send("\r\n=== JFC103 Test ===\r\n");

    /* Wait 2 seconds for module to boot before sending 0x8A */
    uart4_send("Waiting 2s for module boot...\r\n");
    HAL_Delay(2000);

    uint8_t start_cmd = 0x8A;
    uint8_t rx_byte;
    uint32_t frame_count = 0;
    uint32_t byte_count = 0;
    uint32_t last_8a_time = 0;
    uint8_t got_data = 0;

    /* Send 0x8A first time */
    HAL_UART_Transmit(&huart5, &start_cmd, 1, 100);
    uart4_send("0x8A sent. Listening...\r\n");
    last_8a_time = HAL_GetTick();

    while (1) {
        /* Re-send 0x8A every 3 seconds until we get data */
        if (!got_data && (HAL_GetTick() - last_8a_time > 3000)) {
            HAL_UART_Transmit(&huart5, &start_cmd, 1, 100);
            uart4_send("[retry 0x8A]\r\n");
            last_8a_time = HAL_GetTick();
        }

        /* Poll UART5 RX for incoming data */
        if (HAL_UART_Receive(&huart5, &rx_byte, 1, 50) == HAL_OK) {
            got_data = 1;
            /* Got a byte from JFC103 */
            if (rx_byte == 0xFF && byte_count > 0) {
                /* New frame start — print frame separator */
                uart4_send("\r\n--- Frame ");
                char num[10]; int nl = 0;
                uint32_t v = frame_count;
                if (v == 0) num[nl++] = '0';
                else while (v > 0) { num[nl++] = '0' + (v % 10); v /= 10; }
                char rev[10]; for (int i = 0; i < nl; i++) rev[i] = num[nl-1-i];
                HAL_UART_Transmit(&huart4, (uint8_t *)rev, nl, 50);
                uart4_send(" ---\r\nFF ");
                frame_count++;
                byte_count = 1;
            } else {
                uart4_hex(rx_byte);
                byte_count++;
                /* Print newline every 16 bytes for readability */
                if (byte_count % 16 == 0) {
                    uart4_send("\r\n");
                }
            }
        }
    }
}
