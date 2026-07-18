#include "unity.h"

/* Required by Unity framework */
void setUp(void) {}
void tearDown(void) {}

/* Test suite entry points — each returns Unity's failure count */
extern int test_adv_parser_main(void);
extern int test_json_encoder_main(void);
extern int test_filter_engine_main(void);

int main(void)
{
    int failures = 0;
    failures += test_adv_parser_main();
    failures += test_json_encoder_main();
    failures += test_filter_engine_main();
    return failures;
}
