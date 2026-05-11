// Based on APU tests by Chris "100th_Coin" Siebert:
// https://github.com/100thCoin/AccuracyCoin
// The AccuracyCoin APU tests are in turn largely based on APU tests by blargg.

#include <unity.h>
#include <familib.h>

static fam_Apu *apu;

typedef struct {
    fam_Register loop_reg;    // contains the loop bit ($4000/$4004/$4008)
    fam_Register sweep_reg;   // sweep for pulses, unused for triangle ($4001/$4005/$4009)
    fam_Register timer_lo;
    fam_Register note_on;     // writing this triggers note-on and loads length counter
    uint8_t      enable_mask; // channel's bit in $4015
    uint8_t      no_loop;     // init value for loop_reg with loop disabled
    uint8_t      loop_val;    // value for loop_reg with loop enabled
    uint8_t      sweep_init;  // init value for sweep_reg
} ChannelConfig;

static const ChannelConfig PULSE1 = {
    FAM_REGISTER_PULSE1_DUTY,     FAM_REGISTER_PULSE1_SWEEP,
    FAM_REGISTER_PULSE1_TIMER_LO, FAM_REGISTER_PULSE1_TIMER_HI,
    0x01, 0x10, 0x30, 0x7F
};
static const ChannelConfig PULSE2 = {
    FAM_REGISTER_PULSE2_DUTY,     FAM_REGISTER_PULSE2_SWEEP,
    FAM_REGISTER_PULSE2_TIMER_LO, FAM_REGISTER_PULSE2_TIMER_HI,
    0x02, 0x10, 0x30, 0x7F
};
static const ChannelConfig TRIANGLE = {
    FAM_REGISTER_TRIANGLE_COUNTER,  FAM_REGISTER_TRIANGLE_UNUSED,
    FAM_REGISTER_TRIANGLE_TIMER_LO, FAM_REGISTER_TRIANGLE_TIMER_HI,
    0x04, 0x7F, 0xFF, 0x00
};

void setUp(void) {
    apu = fam_apu_init();
}

void tearDown(void) {
    fam_apu_free(apu);
}

static void init_channel(const ChannelConfig* ch) {
    fam_apu_write_register(apu, FAM_REGISTER_STATUS, ch->enable_mask);
    fam_apu_write_register(apu, ch->loop_reg,        ch->no_loop);
    fam_apu_write_register(apu, ch->sweep_reg,       ch->sweep_init);
    fam_apu_write_register(apu, ch->timer_lo,        0xFF);
}

static void assert_length_channel(const ChannelConfig* ch, int expected) {
    int count = 0;
    uint8_t status;
    fam_apu_read_register(apu, FAM_REGISTER_STATUS, &status);
    while (status & ch->enable_mask) {
        TEST_ASSERT_LESS_OR_EQUAL_INT_MESSAGE(expected, count,
            "Length counter did not reach zero in expected number of clocks");
        fam_apu_write_register(apu, FAM_REGISTER_FRAME_COUNTER, 0xC0);
        fam_apu_clock(apu, NULL);
        fam_apu_read_register(apu, FAM_REGISTER_STATUS, &status);
        count++;
    }
    TEST_ASSERT_EQUAL_INT(expected, count);
}

// ---- Test implementations ----

// $4015 should not report the channel active before any note-on write
static void test_status_before_note_on_impl(const ChannelConfig* ch) {
    init_channel(ch);
    uint8_t status = 0;
    fam_apu_read_register(apu, FAM_REGISTER_STATUS, &status);
    TEST_ASSERT_EQUAL_UINT8(0, status & ch->enable_mask);
}

// $4015 should report the channel active after a note-on write
static void test_status_after_note_on_impl(const ChannelConfig* ch) {
    init_channel(ch);
    fam_apu_write_register(apu, ch->note_on, 0x18); // length_load=3 -> 2
    uint8_t status = 0;
    fam_apu_read_register(apu, FAM_REGISTER_STATUS, &status);
    TEST_ASSERT_EQUAL_UINT8(ch->enable_mask, status & ch->enable_mask);
}

// Length counter should count down and expire, silencing the channel
static void test_length_counter_expires_impl(const ChannelConfig* ch) {
    init_channel(ch);
    fam_apu_write_register(apu, FAM_REGISTER_FRAME_COUNTER, 0x00); // sync
    fam_apu_write_register(apu, ch->note_on, 0x18); // length=2
    assert_length_channel(ch, 2);
}

// Writing $4017=$80 (mode 1) should immediately clock the length counter
static void test_mode1_clocks_length_impl(const ChannelConfig* ch) {
    init_channel(ch);
    fam_apu_write_register(apu, FAM_REGISTER_FRAME_COUNTER, 0x00); // sync
    fam_apu_write_register(apu, ch->note_on,                0x18); // length=2
    fam_apu_write_register(apu, FAM_REGISTER_FRAME_COUNTER, 0x80); // clock -> 1
    fam_apu_write_register(apu, FAM_REGISTER_FRAME_COUNTER, 0x80); // clock -> 0
    uint8_t status = 0;
    fam_apu_read_register(apu, FAM_REGISTER_STATUS, &status);
    TEST_ASSERT_EQUAL_UINT8(0, status & ch->enable_mask);
}

// Writing $4017=$00 (mode 0) should not clock the length counter
static void test_mode0_does_not_clock_length_impl(const ChannelConfig* ch) {
    init_channel(ch);
    fam_apu_write_register(apu, ch->note_on,                0x18); // length=2
    fam_apu_write_register(apu, FAM_REGISTER_FRAME_COUNTER, 0x00);
    fam_apu_write_register(apu, FAM_REGISTER_FRAME_COUNTER, 0x00);
    uint8_t status = 0;
    fam_apu_read_register(apu, FAM_REGISTER_STATUS, &status);
    TEST_ASSERT_EQUAL_UINT8(ch->enable_mask, status & ch->enable_mask);
}

// Disabling a channel via $4015 should immediately clear its length counter
static void test_disable_clears_length_impl(const ChannelConfig* ch) {
    init_channel(ch);
    fam_apu_write_register(apu, ch->note_on,         0x18); // length=2
    fam_apu_write_register(apu, FAM_REGISTER_STATUS, 0x00); // disable
    fam_apu_write_register(apu, FAM_REGISTER_STATUS, ch->enable_mask); // re-enable
    uint8_t status = 0;
    fam_apu_read_register(apu, FAM_REGISTER_STATUS, &status);
    TEST_ASSERT_EQUAL_UINT8(0, status & ch->enable_mask);
}

// Writing the note-on register while the channel is disabled should not load the length counter
static void test_length_not_loaded_when_disabled_impl(const ChannelConfig* ch) {
    init_channel(ch);
    fam_apu_write_register(apu, FAM_REGISTER_STATUS, 0x00); // disable
    fam_apu_write_register(apu, ch->note_on,         0x18); // attempt load
    fam_apu_write_register(apu, FAM_REGISTER_STATUS, ch->enable_mask); // re-enable
    uint8_t status = 0;
    fam_apu_read_register(apu, FAM_REGISTER_STATUS, &status);
    TEST_ASSERT_EQUAL_UINT8(0, status & ch->enable_mask);
}

// Loop flag should prevent the length counter from decrementing
static void test_loop_prevents_length_decrement_impl(const ChannelConfig* ch) {
    init_channel(ch);
    fam_apu_write_register(apu, ch->note_on,                0x18);        // length=2
    fam_apu_write_register(apu, ch->loop_reg,               ch->loop_val); // loop=1
    fam_apu_write_register(apu, FAM_REGISTER_FRAME_COUNTER, 0x80);
    fam_apu_write_register(apu, FAM_REGISTER_FRAME_COUNTER, 0x80);
    fam_apu_write_register(apu, ch->loop_reg,               ch->no_loop);  // loop=0
    uint8_t status = 0;
    fam_apu_read_register(apu, FAM_REGISTER_STATUS, &status);
    TEST_ASSERT_EQUAL_UINT8(ch->enable_mask, status & ch->enable_mask);
}

// After clearing the loop flag, the length counter should resume decrementing
static void test_loop_off_resumes_length_decrement_impl(const ChannelConfig* ch) {
    init_channel(ch);
    fam_apu_write_register(apu, ch->note_on,                0x18);
    fam_apu_write_register(apu, ch->loop_reg,               ch->loop_val);
    fam_apu_write_register(apu, FAM_REGISTER_FRAME_COUNTER, 0x80);
    fam_apu_write_register(apu, FAM_REGISTER_FRAME_COUNTER, 0x80);
    fam_apu_write_register(apu, ch->loop_reg,               ch->no_loop);
    fam_apu_write_register(apu, FAM_REGISTER_FRAME_COUNTER, 0x80); // clock -> 1
    fam_apu_write_register(apu, FAM_REGISTER_FRAME_COUNTER, 0x80); // clock -> 0
    uint8_t status = 0;
    fam_apu_read_register(apu, FAM_REGISTER_STATUS, &status);
    TEST_ASSERT_EQUAL_UINT8(0, status & ch->enable_mask);
}

// All 32 length table entries should map to the correct counter values
static void test_length_table_impl(const ChannelConfig* ch) {
    static const uint8_t expected[32] = {
        10, 254, 20, 2, 40, 4, 80, 6,
        160, 8, 60, 10, 14, 12, 26, 14,
        12, 16, 24, 18, 48, 20, 96, 22,
        192, 24, 72, 26, 16, 28, 32, 30
    };

    for (int i = 0; i < 32; i++) {
        init_channel(ch);
        fam_apu_write_register(apu, FAM_REGISTER_FRAME_COUNTER, 0x00);
        fam_apu_write_register(apu, ch->note_on, (uint8_t)(i << 3));
        assert_length_channel(ch, expected[i]);
    }
}

// ---- Per-channel wrappers ----

#define CHANNEL_TESTS(suffix, config) \
    void test_status_before_note_on_##suffix(void)             { test_status_before_note_on_impl(&config); } \
    void test_status_after_note_on_##suffix(void)              { test_status_after_note_on_impl(&config); } \
    void test_length_counter_expires_##suffix(void)            { test_length_counter_expires_impl(&config); } \
    void test_mode1_clocks_length_##suffix(void)               { test_mode1_clocks_length_impl(&config); } \
    void test_mode0_does_not_clock_length_##suffix(void)       { test_mode0_does_not_clock_length_impl(&config); } \
    void test_disable_clears_length_##suffix(void)             { test_disable_clears_length_impl(&config); } \
    void test_length_not_loaded_when_disabled_##suffix(void)   { test_length_not_loaded_when_disabled_impl(&config); } \
    void test_loop_prevents_length_decrement_##suffix(void)    { test_loop_prevents_length_decrement_impl(&config); } \
    void test_loop_off_resumes_length_decrement_##suffix(void) { test_loop_off_resumes_length_decrement_impl(&config); } \
    void test_length_table_##suffix(void)                      { test_length_table_impl(&config); }

CHANNEL_TESTS(pulse1,   PULSE1)
CHANNEL_TESTS(pulse2,   PULSE2)
CHANNEL_TESTS(triangle, TRIANGLE)

// ---- Frame counter IRQ tests ----

// Helper: put the frame counter in mode 0 and run past one frame boundary to trigger IRQ
static void trigger_irq(void) {
    fam_apu_write_register(apu, FAM_REGISTER_FRAME_COUNTER, 0x00);
    for (int i = 0; i < 15000; i++) fam_apu_clock(apu, NULL);
}

// In mode 0 with inhibit clear, the IRQ flag should be set after a frame boundary
void test_irq_set_in_mode0(void) {
    trigger_irq();
    uint8_t status = 0;
    fam_apu_read_register(apu, FAM_REGISTER_STATUS, &status);
    TEST_ASSERT_EQUAL_UINT8(0x40, status & 0x40);
}

// In mode 0 with inhibit set, the IRQ flag should not be set
void test_irq_not_set_when_inhibited(void) {
    fam_apu_write_register(apu, FAM_REGISTER_FRAME_COUNTER, 0x40);
    for (int i = 0; i < 15000; i++) fam_apu_clock(apu, NULL);
    uint8_t status = 0;
    fam_apu_read_register(apu, FAM_REGISTER_STATUS, &status);
    TEST_ASSERT_EQUAL_UINT8(0, status & 0x40);
}

// In mode 1 with inhibit clear, the IRQ flag should not be set
void test_irq_not_set_in_mode1(void) {
    fam_apu_write_register(apu, FAM_REGISTER_FRAME_COUNTER, 0x80);
    for (int i = 0; i < 15000; i++) fam_apu_clock(apu, NULL);
    uint8_t status = 0;
    fam_apu_read_register(apu, FAM_REGISTER_STATUS, &status);
    TEST_ASSERT_EQUAL_UINT8(0, status & 0x40);
}

// In mode 1 with inhibit set, the IRQ flag should not be set
void test_irq_not_set_in_mode1_inhibited(void) {
    fam_apu_write_register(apu, FAM_REGISTER_FRAME_COUNTER, 0xC0);
    for (int i = 0; i < 15000; i++) fam_apu_clock(apu, NULL);
    uint8_t status = 0;
    fam_apu_read_register(apu, FAM_REGISTER_STATUS, &status);
    TEST_ASSERT_EQUAL_UINT8(0, status & 0x40);
}

// Reading $4015 should return the IRQ flag and then clear it
void test_irq_cleared_by_status_read(void) {
    trigger_irq();
    uint8_t status = 0;
    fam_apu_read_register(apu, FAM_REGISTER_STATUS, &status);
    TEST_ASSERT_EQUAL_UINT8(0x40, status & 0x40); // flag was set
    fam_apu_read_register(apu, FAM_REGISTER_STATUS, &status);
    TEST_ASSERT_EQUAL_UINT8(0, status & 0x40);    // cleared by first read
}

// Switching to mode 1 should not clear an already-set IRQ flag
void test_irq_not_cleared_by_mode1_switch(void) {
    trigger_irq();
    fam_apu_write_register(apu, FAM_REGISTER_FRAME_COUNTER, 0x80);
    uint8_t status = 0;
    fam_apu_read_register(apu, FAM_REGISTER_STATUS, &status);
    TEST_ASSERT_EQUAL_UINT8(0x40, status & 0x40);
}

// Setting the interrupt inhibit bit should clear the IRQ flag
void test_irq_cleared_by_inhibit(void) {
    trigger_irq();
    fam_apu_write_register(apu, FAM_REGISTER_FRAME_COUNTER, 0x40);
    uint8_t status = 0;
    fam_apu_read_register(apu, FAM_REGISTER_STATUS, &status);
    TEST_ASSERT_EQUAL_UINT8(0, status & 0x40);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_status_before_note_on_pulse1);
    RUN_TEST(test_status_before_note_on_pulse2);
    RUN_TEST(test_status_before_note_on_triangle);
    RUN_TEST(test_status_after_note_on_pulse1);
    RUN_TEST(test_status_after_note_on_pulse2);
    RUN_TEST(test_status_after_note_on_triangle);
    RUN_TEST(test_length_counter_expires_pulse1);
    RUN_TEST(test_length_counter_expires_pulse2);
    RUN_TEST(test_length_counter_expires_triangle);
    RUN_TEST(test_mode1_clocks_length_pulse1);
    RUN_TEST(test_mode1_clocks_length_pulse2);
    RUN_TEST(test_mode1_clocks_length_triangle);
    RUN_TEST(test_mode0_does_not_clock_length_pulse1);
    RUN_TEST(test_mode0_does_not_clock_length_pulse2);
    RUN_TEST(test_mode0_does_not_clock_length_triangle);
    RUN_TEST(test_disable_clears_length_pulse1);
    RUN_TEST(test_disable_clears_length_pulse2);
    RUN_TEST(test_disable_clears_length_triangle);
    RUN_TEST(test_length_not_loaded_when_disabled_pulse1);
    RUN_TEST(test_length_not_loaded_when_disabled_pulse2);
    RUN_TEST(test_length_not_loaded_when_disabled_triangle);
    RUN_TEST(test_loop_prevents_length_decrement_pulse1);
    RUN_TEST(test_loop_prevents_length_decrement_pulse2);
    RUN_TEST(test_loop_prevents_length_decrement_triangle);
    RUN_TEST(test_loop_off_resumes_length_decrement_pulse1);
    RUN_TEST(test_loop_off_resumes_length_decrement_pulse2);
    RUN_TEST(test_loop_off_resumes_length_decrement_triangle);
    RUN_TEST(test_length_table_pulse1);
    RUN_TEST(test_length_table_pulse2);
    RUN_TEST(test_length_table_triangle);
    RUN_TEST(test_irq_set_in_mode0);
    RUN_TEST(test_irq_not_set_when_inhibited);
    RUN_TEST(test_irq_not_set_in_mode1);
    RUN_TEST(test_irq_not_set_in_mode1_inhibited);
    RUN_TEST(test_irq_cleared_by_status_read);
    RUN_TEST(test_irq_not_cleared_by_mode1_switch);
    RUN_TEST(test_irq_cleared_by_inhibit);
    return UNITY_END();
}
