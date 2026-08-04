#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "filter_if.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * CLI Command Interface (F4.1)
 *
 * Text command parser/dispatcher over USB CDC. Parses host commands,
 * validates them against the current device state, dispatches to the
 * appropriate subsystem (scan pipeline, filter, Lua script manager)
 * and writes a single-line JSON response into a caller-supplied buffer.
 *
 * Zero allocation: this module never calls malloc/free.
 *
 * State machine:
 *   IDLE --SCAN START--> SCANNING --SCRIPT RUN--> SCRIPT_RUNNING
 *     ^                    |                            |
 *     |   SCAN STOP        |   SCAN STOP                |  SCRIPT STOP
 *     +--------------------+----------------------------+
 *
 * Error codes (module range -900 to -999):
 *   0        Success
 *   -901     Invalid command / syntax error
 *   -902     NULL pointer
 *   -903     Response buffer too small (truncated error JSON written)
 *   -911     Command not allowed in current state
 */

#define CLI_ERR_INVALID_CMD   (-901)
#define CLI_ERR_NULL          (-902)
#define CLI_ERR_BUFFER        (-903)
#define CLI_ERR_STATE         (-911)

typedef enum {
    CLI_STATE_IDLE = 0,
    CLI_STATE_SCANNING,
    CLI_STATE_SCRIPT_RUNNING
} cli_state_t;

/*
 * Initialize the CLI subsystem.
 *
 * Initializes the CLI-owned filter engine and sets state to IDLE.
 * The filter engine is owned here because it is shared between the
 * CLI (rule management) and the scan pipeline (evaluation); wire it
 * into the pipeline with pipeline_set_filter(cli_get_filter_engine()).
 *
 * @return 0 on success, negative error code on failure.
 */
int cli_init(void);

/*
 * Parse a command, validate it against the current state, dispatch it
 * and write the JSON response.
 *
 * @param cmd           Null-terminated command string from host.
 * @param response      Caller-allocated buffer for the JSON response.
 * @param response_len  Size of response in bytes.
 * @return 0 if the command was dispatched (the JSON carries subsystem
 *         success/failure), CLI_ERR_NULL (-902) on NULL parameters,
 *         CLI_ERR_INVALID_CMD (-901) on unknown/syntax-error commands,
 *         CLI_ERR_STATE (-911) when the state forbids the command,
 *         CLI_ERR_BUFFER (-903) when the response did not fit.
 */
int cli_process_command(const char *cmd, char *response, uint16_t response_len);

/*
 * Current CLI state, derived from the live subsystem state
 * (script running > scanning > idle) so it can never drift.
 */
cli_state_t cli_get_state(void);

/*
 * Access the CLI-owned filter engine (for pipeline_set_filter()).
 */
filter_engine_t *cli_get_filter_engine(void);

#ifdef __cplusplus
}
#endif
