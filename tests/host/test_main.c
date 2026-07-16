#include "unity.h"

/* Required by Unity framework */
void setUp(void) {}
void tearDown(void) {}

/* Test suite entry points */
extern void test_adv_parser_main(void);
extern void test_json_encoder_main(void);
extern void test_filter_engine_main(void);

int main(void)
{
    test_adv_parser_main();
    test_json_encoder_main();
    test_filter_engine_main();
    return 0;
}
