#include "unity.h"

/* Required by Unity framework */
void setUp(void) {}
void tearDown(void) {}

/* Test suite entry points — each returns Unity's failure count */
extern int test_adv_parser_main(void);
extern int test_json_encoder_main(void);
extern int test_filter_engine_main(void);
extern int test_cli_main(void);
extern int test_bridge_main(void);
extern int test_lua_pool_main(void);
extern int test_ble_conn_main(void);
extern int test_cli_responses_main(void);
extern int test_fuzz_main(void);
extern int test_pack_main(void);
extern int test_lua_chunk_main(void);

int main(void)
{
    int failures = 0;
    failures += test_adv_parser_main();
    failures += test_json_encoder_main();
    failures += test_filter_engine_main();
    failures += test_cli_main();
    failures += test_bridge_main();
    failures += test_lua_pool_main();
    failures += test_ble_conn_main();
    failures += test_cli_responses_main();
    failures += test_fuzz_main();
    failures += test_pack_main();
    failures += test_lua_chunk_main();
    return failures;
}
