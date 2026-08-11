#include "usb_if.h"
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
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

    /* Set stdin to non-blocking so getchar() returns EOF when no data */
    fcntl(0, F_SETFL, O_NONBLOCK);

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
        /* Try to read one byte from stdin (non-blocking via VFS) */
        int c = getchar();
        if (c == EOF) {
            uint32_t elapsed = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) - start_ms;
            if (elapsed >= timeout_ms) {
                /* Timeout without a terminating '\n': the contract requires a
                 * complete line or -503 (M1 fix). Discard the partial data —
                 * executing a truncated command would be worse than dropping it.
                 * Also fixes an infinite busy-loop for timeout_ms==0 with
                 * partial data. */
                return -503;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (c == 0x03) {
            /* Ctrl+C: immediate one-byte interrupt line (no Enter
             * needed) so a flooded terminal can always be stopped. */
            buf[0] = '\x03';
            buf[1] = '\0';
            return 1;
        }

        if (c == '\n' || c == '\r') {
            /* Both CR and LF terminate a line so interactive terminals
             * work (PuTTY/Tera Term send bare CR on Enter; scripts send
             * LF). A CRLF pair yields one command plus an empty line,
             * which the caller ignores (length 0). */
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
