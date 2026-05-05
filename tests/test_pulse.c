#include <unity.h>
#include <familib.h>

static fam_Apu *apu;

void setUp(void) {
    apu = fam_apu_init();
    // Mirror setup_apu from blargg's apu_util.asm
    fam_apu_write_register(apu, FAM_REGISTER_STATUS,          0x01); // enable pulse 1
    fam_apu_write_register(apu, FAM_REGISTER_PULSE1_DUTY,     0x10); // no halt, const vol
    fam_apu_write_register(apu, FAM_REGISTER_PULSE1_SWEEP,    0x7F); // sweep disabled
    fam_apu_write_register(apu, FAM_REGISTER_PULSE1_TIMER_LO, 0xFF); // timer period lo
}

void tearDown(void) {
    fam_apu_free(apu);
}

// Mirror count_length from blargg's apu_util.asm:
// Checks $4015 bit first, then clocks $4017=$C0 until bit clears, counting clocks.
static void assert_length_pulse1(int expected) {
    int count = 0;
    uint8_t status;
    fam_apu_read_register(apu, FAM_REGISTER_STATUS, &status);
    while (status & 0x01) {
        TEST_ASSERT_LESS_OR_EQUAL_INT_MESSAGE(expected, count,
            "Length counter did not reach zero in expected number of clocks");
        fam_apu_write_register(apu, FAM_REGISTER_FRAME_COUNTER, 0xC0);
        fam_apu_clock(apu, NULL);
        fam_apu_read_register(apu, FAM_REGISTER_STATUS, &status);
        count++;
    }
    TEST_ASSERT_EQUAL_INT(expected, count);
}

void test_len_table(void) {
    static const uint8_t expected[32] = {
        10, 254, 20, 2, 40, 4, 80, 6,
        160, 8, 60, 10, 14, 12, 26, 14,
        12, 16, 24, 18, 48, 20, 96, 22,
        192, 24, 72, 26, 16, 28, 32, 30
    };

    for (int i = 0; i < 32; i++) {
        fam_apu_write_register(apu, FAM_REGISTER_FRAME_COUNTER,      0x00); // sync
        fam_apu_write_register(apu, FAM_REGISTER_PULSE1_TIMER_HI, (uint8_t)(i << 3));
        assert_length_pulse1(expected[i]);
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_len_table);
    return UNITY_END();
}
