#include "test.h"

int g_test_failed;

static const struct {
    const char *name;
    void (*fn)(void);
} k_tests[] = {
#define X(name) {#name, test_##name},
    TEST_LIST
#undef X
};

int main(void)
{
    int failed = 0;
    const int total = (int)(sizeof(k_tests) / sizeof(k_tests[0]));
    for (int i = 0; i < total; i++) {
        g_test_failed = 0;
        k_tests[i].fn();
        printf("%s %s\n", g_test_failed ? "FAIL" : "ok  ", k_tests[i].name);
        failed += g_test_failed;
    }
    printf("\n%d/%d tests passed\n", total - failed, total);
    return failed ? 1 : 0;
}
