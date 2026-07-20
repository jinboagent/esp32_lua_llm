#include "usb_if.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static bool s_initialized = false;
static SemaphoreHandle_t s_tx_mutex = NULL;

int usb_console_init(void)
{
    if (s_initialized) {
        return 0;
    }

    s_tx_mutex = xSemaphoreCreateMutex();
    if (s_tx_mutex == NULL) {
        return -500;
    }

    s_initialized = true;
    return 0;
}

int usb_console_send_line(const char *line)
{
    if (line == NULL) {
        return -502;
    }
    if (!s_initialized) {
        return -501;
    }

    if (xSemaphoreTake(s_tx_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return -500;
    }

    printf("%s\n", line);
    fflush(stdout);

    xSemaphoreGive(s_tx_mutex);
    return 0;
}

int usb_console_send_json(const char *json_line)
{
    return usb_console_send_line(json_line);
}

int usb_console_read_line(char *buf, uint16_t buf_len, uint32_t timeout_ms)
{
    if (buf == NULL || buf_len == 0) {
        return -502;
    }
    if (!s_initialized) {
        return -501;
    }

    /* Use non-blocking read with timeout loop */
    uint32_t start_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    uint16_t pos = 0;

    while (1) {
        /* Check timeout */
        if (timeout_ms > 0) {
            uint32_t elapsed = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) - start_ms;
            if (elapsed >= timeout_ms) {
                if (pos > 0) {
                    buf[pos] = '\0';
                    return (int)pos;
                }
                return -503; /* Timeout */
            }
        }

        /* Try to read one byte from stdin (non-blocking via VFS) */
        int c = getchar();
        if (c == EOF) {
            vTaskDelay(pdMS_TO_TICKS(10));
            if (timeout_ms == 0 && pos == 0) {
                return -503;
            }
            continue;
        }

        if (c == '\r') {
            continue; /* Ignore CR */
        }

        if (c == '\n') {
            buf[pos] = '\0';
            return (int)pos;
        }

        if (pos < buf_len - 1) {
            buf[pos++] = (char)c;
        } else {
            /* Buffer overflow - truncate */
            buf[pos] = '\0';
            return -504;
        }
    }
}
