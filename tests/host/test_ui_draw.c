#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif

#include "watchy/ui_draw.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#if defined(MAP_ANONYMOUS)
#define WATCHY_TEST_MAP_ANONYMOUS MAP_ANONYMOUS
#elif defined(MAP_ANON)
#define WATCHY_TEST_MAP_ANONYMOUS MAP_ANON
#endif
#endif

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

enum {
    DISPLAY_WIDTH = 200,
    DISPLAY_HEIGHT = 200,
    DISPLAY_STRIDE = 25,
    FRAMEBUFFER_SIZE = DISPLAY_HEIGHT * DISPLAY_STRIDE,
    GUARD_SIZE = 16,
};

typedef struct {
    uint8_t before[GUARD_SIZE];
    uint8_t framebuffer[FRAMEBUFFER_SIZE];
    uint8_t after[GUARD_SIZE];
    watchy_canvas_t canvas;
} guarded_canvas_t;

static const uint8_t fixture_bitmap[] = {
    0x80,
    0xe0, 0xe0, 0xe0,
    0x80, 0x80, 0x80,
    0x80,
    0x80,
};

static const watchy_font_glyph_t fixture_glyphs[] = {
    {32u, 0u, 0, 0, 0u, 0u, 2u, 0u},
    {63u, 0u, 0, 1, 1u, 1u, 3u, 1u},
    {65u, 1u, 0, 3, 3u, 3u, 4u, 1u},
    {66u, 4u, 0, 3, 1u, 3u, 5u, 1u},
    {78u, 7u, -2, 1, 1u, 1u, 3u, 1u},
    {80u, 8u, 2, 1, 1u, 1u, 3u, 1u},
};

static const watchy_font_t fixture_font = {
    fixture_glyphs, fixture_bitmap, 6u, 4u, 3, -1, 4u,
};

static const uint8_t unicode_fixture_bitmap[] = {
    0x80, /* ? */
    0xc0, /* degree */
    0xa0, /* middle dot */
    0xf0, /* em dash */
    0x88, /* right arrow */
};

static const watchy_font_glyph_t unicode_fixture_glyphs[] = {
    {63u, 0u, 0, 1, 1u, 1u, 2u, 1u},
    {176u, 1u, 0, 1, 2u, 1u, 3u, 1u},
    {183u, 2u, 0, 1, 3u, 1u, 4u, 1u},
    {8212u, 3u, 0, 1, 4u, 1u, 5u, 1u},
    {8594u, 4u, 0, 1, 5u, 1u, 6u, 1u},
};

static const watchy_font_t unicode_fixture_font = {
    unicode_fixture_glyphs, unicode_fixture_bitmap, 5u, 6u, 1, 0, 1u,
};

static const uint8_t questionless_bitmap[] = {0x80};
static const watchy_font_glyph_t questionless_glyphs[] = {
    {65u, 0u, 0, 1, 1u, 1u, 4u, 1u},
};
static const watchy_font_t questionless_font = {
    questionless_glyphs, questionless_bitmap, 1u, 4u, 1, 0, 1u,
};

static void init_canvas(guarded_canvas_t *fixture, bool black) {
    memset(fixture->before, 0xa5, sizeof(fixture->before));
    memset(fixture->framebuffer, black ? 0x00 : 0xff, sizeof(fixture->framebuffer));
    memset(fixture->after, 0x5a, sizeof(fixture->after));
    fixture->canvas = (watchy_canvas_t){
        .width = DISPLAY_WIDTH,
        .height = DISPLAY_HEIGHT,
        .stride = DISPLAY_STRIDE,
        .format = WATCHY_PIXEL_MONO,
        .pixels = fixture->framebuffer,
    };
}

static bool pixel_black(const guarded_canvas_t *fixture, int x, int y) {
    const size_t offset = (size_t)y * DISPLAY_STRIDE + (size_t)x / 8u;
    const uint8_t mask = (uint8_t)(0x80u >> ((unsigned)x & 7u));
    return (fixture->framebuffer[offset] & mask) == 0u;
}

static size_t black_pixel_count(const guarded_canvas_t *fixture) {
    size_t count = 0u;
    for (int y = 0; y < DISPLAY_HEIGHT; ++y) {
        for (int x = 0; x < DISPLAY_WIDTH; ++x) {
            count += pixel_black(fixture, x, y) ? 1u : 0u;
        }
    }
    return count;
}

static bool guards_unchanged(const guarded_canvas_t *fixture) {
    for (size_t index = 0u; index < GUARD_SIZE; ++index) {
        if (fixture->before[index] != 0xa5 || fixture->after[index] != 0x5a) {
            return false;
        }
    }
    return true;
}

static int test_pixels_and_shapes_clip_at_all_four_edges_and_preserve_guards(void) {
    guarded_canvas_t fixture;
    const watchy_text_style_t style = {
        .font = &fixture_font, .tracking = 0, .black = true, .outlined = false,
    };
    init_canvas(&fixture, false);

    watchy_ui_pixel(&fixture.canvas, -1, 50, true);
    watchy_ui_pixel(&fixture.canvas, 200, 50, true);
    watchy_ui_pixel(&fixture.canvas, 50, -1, true);
    watchy_ui_pixel(&fixture.canvas, 50, 200, true);
    watchy_ui_rect(&fixture.canvas, -2, 50, 3, 1, true);
    watchy_ui_rect(&fixture.canvas, 199, 60, 3, 1, true);
    watchy_ui_rect(&fixture.canvas, 70, -2, 1, 3, true);
    watchy_ui_rect(&fixture.canvas, 80, 199, 1, 3, true);
    watchy_ui_circle(&fixture.canvas, -1, -1, 3, true, true);
    watchy_ui_draw_text_font(&fixture.canvas, -2, 1, "A", &style);
    watchy_ui_draw_text_font(&fixture.canvas, 199, 201, "A", &style);

    CHECK(pixel_black(&fixture, 0, 50));
    CHECK(pixel_black(&fixture, 199, 60));
    CHECK(pixel_black(&fixture, 70, 0));
    CHECK(pixel_black(&fixture, 80, 199));
    CHECK(guards_unchanged(&fixture));

    fixture.canvas.width = UINT16_MAX;
    fixture.canvas.height = UINT16_MAX;
    watchy_ui_rect(&fixture.canvas, 199, 199, 100, 100, true);
    CHECK(guards_unchanged(&fixture));
    return 0;
}

static int test_filled_and_outlined_rectangles_have_distinct_geometry(void) {
    guarded_canvas_t fixture;
    init_canvas(&fixture, false);

    watchy_ui_rect(&fixture.canvas, 2, 3, 4, 3, true);
    CHECK(black_pixel_count(&fixture) == 12u);
    CHECK(pixel_black(&fixture, 3, 4));

    init_canvas(&fixture, false);
    watchy_ui_rect_outline(&fixture.canvas, 2, 3, 6, 6, 2u, true);
    CHECK(black_pixel_count(&fixture) == 32u);
    CHECK(pixel_black(&fixture, 2, 3));
    CHECK(pixel_black(&fixture, 3, 7));
    CHECK(!pixel_black(&fixture, 4, 5));
    CHECK(!pixel_black(&fixture, 5, 6));
    return 0;
}

static int test_rules_honor_one_two_and_three_pixel_thickness(void) {
    guarded_canvas_t fixture;
    init_canvas(&fixture, false);

    watchy_ui_rule(&fixture.canvas, 10, 10, 5, 1u, true);
    watchy_ui_rule(&fixture.canvas, 20, 20, 5, 2u, true);
    watchy_ui_rule(&fixture.canvas, 30, 30, 5, 3u, true);
    CHECK(black_pixel_count(&fixture) == 30u);
    CHECK(pixel_black(&fixture, 14, 10));
    CHECK(!pixel_black(&fixture, 14, 11));
    CHECK(pixel_black(&fixture, 24, 21));
    CHECK(!pixel_black(&fixture, 24, 22));
    CHECK(pixel_black(&fixture, 34, 32));
    CHECK(!pixel_black(&fixture, 34, 33));
    return 0;
}

static int test_outline_and_filled_circles_render_expected_pixels(void) {
    guarded_canvas_t fixture;
    init_canvas(&fixture, false);

    watchy_ui_circle(&fixture.canvas, 20, 20, 3, false, true);
    CHECK(pixel_black(&fixture, 20, 17));
    CHECK(pixel_black(&fixture, 23, 20));
    CHECK(pixel_black(&fixture, 20, 23));
    CHECK(pixel_black(&fixture, 17, 20));
    CHECK(pixel_black(&fixture, 22, 22));
    CHECK(!pixel_black(&fixture, 20, 20));
    CHECK(!pixel_black(&fixture, 21, 21));

    init_canvas(&fixture, false);
    watchy_ui_circle(&fixture.canvas, 20, 20, 2, true, true);
    CHECK(pixel_black(&fixture, 20, 20));
    CHECK(pixel_black(&fixture, 20, 18));
    CHECK(pixel_black(&fixture, 22, 20));
    CHECK(!pixel_black(&fixture, 22, 22));
    return 0;
}

static int test_measurement_uses_advances_tracking_and_no_trailing_tracking(void) {
    const watchy_text_style_t style = {
        .font = &fixture_font, .tracking = 2, .black = true, .outlined = false,
    };
    const watchy_text_metrics_t empty = watchy_ui_measure_text(&style, "");
    const watchy_text_metrics_t one = watchy_ui_measure_text(&style, "A");
    const watchy_text_metrics_t two = watchy_ui_measure_text(&style, "AB");

    CHECK(empty.width == 0 && empty.height == 0u);
    CHECK(one.width == 4);
    CHECK(one.height == 4u && one.ascent == 3 && one.descent == -1);
    CHECK(two.width == 11);
    return 0;
}

static int test_text_uses_baseline_bearings_normal_and_outlined_glyphs(void) {
    guarded_canvas_t fixture;
    watchy_text_style_t style = {
        .font = &fixture_font, .tracking = 0, .black = true, .outlined = false,
    };
    init_canvas(&fixture, false);

    watchy_ui_draw_text_font(&fixture.canvas, 4, 8, "A", &style);
    CHECK(pixel_black(&fixture, 4, 5));
    CHECK(pixel_black(&fixture, 6, 7));
    CHECK(!pixel_black(&fixture, 4, 4));
    CHECK(!pixel_black(&fixture, 4, 8));
    CHECK(black_pixel_count(&fixture) == 9u);

    init_canvas(&fixture, false);
    style.outlined = true;
    watchy_ui_draw_text_font(&fixture.canvas, 4, 8, "A", &style);
    CHECK(pixel_black(&fixture, 4, 5));
    CHECK(pixel_black(&fixture, 6, 7));
    CHECK(!pixel_black(&fixture, 5, 6));
    CHECK(black_pixel_count(&fixture) == 8u);
    return 0;
}

static int test_text_honors_positive_and_negative_horizontal_bearings(void) {
    guarded_canvas_t fixture;
    const watchy_text_style_t style = {
        .font = &fixture_font, .tracking = 0, .black = true, .outlined = false,
    };
    init_canvas(&fixture, false);

    watchy_ui_draw_text_font(&fixture.canvas, 10, 4, "N", &style);
    CHECK(pixel_black(&fixture, 8, 3));
    CHECK(!pixel_black(&fixture, 9, 3));
    CHECK(!pixel_black(&fixture, 10, 3));

    init_canvas(&fixture, false);
    watchy_ui_draw_text_font(&fixture.canvas, 10, 4, "P", &style);
    CHECK(pixel_black(&fixture, 12, 3));
    CHECK(!pixel_black(&fixture, 10, 3));
    CHECK(!pixel_black(&fixture, 11, 3));
    return 0;
}

static int test_centered_and_right_aligned_text_use_measured_advance(void) {
    guarded_canvas_t fixture;
    const watchy_text_style_t style = {
        .font = &fixture_font, .tracking = 1, .black = true, .outlined = false,
    };
    init_canvas(&fixture, false);

    watchy_ui_draw_text_centered(&fixture.canvas, 20, 8, "AA", &style);
    CHECK(pixel_black(&fixture, 16, 5));
    CHECK(pixel_black(&fixture, 21, 5));
    CHECK(!pixel_black(&fixture, 15, 5));

    init_canvas(&fixture, false);
    watchy_ui_draw_text_right(&fixture.canvas, 30, 8, "AA", &style);
    CHECK(pixel_black(&fixture, 21, 5));
    CHECK(pixel_black(&fixture, 26, 5));
    CHECK(!pixel_black(&fixture, 20, 5));
    CHECK(!pixel_black(&fixture, 29, 5));
    return 0;
}

static int test_white_text_on_black_canvas_inverts_only_glyph_pixels(void) {
    guarded_canvas_t fixture;
    const watchy_text_style_t style = {
        .font = &fixture_font, .tracking = 0, .black = false, .outlined = false,
    };
    init_canvas(&fixture, true);

    watchy_ui_draw_text_font(&fixture.canvas, 4, 8, "A", &style);
    CHECK(!pixel_black(&fixture, 4, 5));
    CHECK(!pixel_black(&fixture, 5, 6));
    CHECK(pixel_black(&fixture, 3, 5));
    CHECK(pixel_black(&fixture, 7, 5));
    CHECK(guards_unchanged(&fixture));
    return 0;
}

static int test_configured_unicode_uses_each_independent_glyph(void) {
    guarded_canvas_t fixture;
    const watchy_text_style_t style = {
        .font = &unicode_fixture_font,
        .tracking = 1,
        .black = true,
        .outlined = false,
    };
    init_canvas(&fixture, false);

    CHECK(watchy_ui_measure_text(
        &style, "\xc2\xb0\xc2\xb7\xe2\x80\x94\xe2\x86\x92").width == 21);
    watchy_ui_draw_text_font(
        &fixture.canvas, 0, 1, "\xc2\xb0\xc2\xb7\xe2\x80\x94\xe2\x86\x92", &style);
    CHECK(black_pixel_count(&fixture) == 10u);
    CHECK(pixel_black(&fixture, 0, 0));
    CHECK(pixel_black(&fixture, 1, 0));
    CHECK(!pixel_black(&fixture, 2, 0));
    CHECK(pixel_black(&fixture, 4, 0));
    CHECK(!pixel_black(&fixture, 5, 0));
    CHECK(pixel_black(&fixture, 6, 0));
    CHECK(pixel_black(&fixture, 9, 0));
    CHECK(pixel_black(&fixture, 12, 0));
    CHECK(pixel_black(&fixture, 15, 0));
    CHECK(!pixel_black(&fixture, 16, 0));
    CHECK(pixel_black(&fixture, 19, 0));
    CHECK(guards_unchanged(&fixture));
    return 0;
}

static int test_valid_absent_scalar_uses_distinct_question_mark_fallback(void) {
    guarded_canvas_t fixture;
    const watchy_text_style_t style = {
        .font = &unicode_fixture_font,
        .tracking = 0,
        .black = true,
        .outlined = false,
    };
    init_canvas(&fixture, false);

    CHECK(watchy_ui_measure_text(&style, "\xe2\x98\x83").width == 2);
    watchy_ui_draw_text_font(&fixture.canvas, 7, 1, "\xe2\x98\x83", &style);
    CHECK(black_pixel_count(&fixture) == 1u);
    CHECK(pixel_black(&fixture, 7, 0));
    CHECK(!pixel_black(&fixture, 8, 0));
    return 0;
}

static int test_invalid_utf8_consumes_one_bad_byte_and_uses_question_mark(void) {
    const watchy_text_style_t style = {
        .font = &fixture_font, .tracking = 0, .black = true, .outlined = false,
    };

    CHECK(watchy_ui_measure_text(&style, "\x80").width == 3);
    CHECK(watchy_ui_measure_text(&style, "\xc0\xaf").width == 6);
    CHECK(watchy_ui_measure_text(&style, "\xed\xa0\x80").width == 9);
    CHECK(watchy_ui_measure_text(&style, "\xf4\x90\x80\x80").width == 12);
    CHECK(watchy_ui_measure_text(&style, "\xe2\x82").width == 6);
    CHECK(watchy_ui_measure_text(&style, "\xe2\x98\x83").width == 3);
    CHECK(watchy_ui_measure_text(&style, "A").width == 4);
    return 0;
}

#if defined(WATCHY_TEST_MAP_ANONYMOUS)
static int test_truncated_utf8_never_reads_past_page_boundary_nul(void) {
    static const uint8_t sequences[][4] = {
        {0xc2u, 0xb0u, 0u, 0u},
        {0xe2u, 0x80u, 0x94u, 0u},
        {0xf0u, 0x9fu, 0x98u, 0x80u},
    };
    static const size_t sequence_lengths[] = {2u, 3u, 4u};
    const watchy_text_style_t style = {
        .font = &unicode_fixture_font,
        .tracking = 0,
        .black = true,
        .outlined = false,
    };
    guarded_canvas_t fixture;
    const long page_size_value = sysconf(_SC_PAGESIZE);
    uint8_t *mapping;
    uint8_t *boundary;
    bool passed = true;

    CHECK(page_size_value > 0);
    mapping = mmap(NULL, (size_t)page_size_value * 2u, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | WATCHY_TEST_MAP_ANONYMOUS, -1, 0);
    CHECK(mapping != MAP_FAILED);
    boundary = mapping + page_size_value;
    if (mprotect(boundary, (size_t)page_size_value, PROT_NONE) != 0) {
        (void)munmap(mapping, (size_t)page_size_value * 2u);
        CHECK(false);
    }

    for (size_t sequence = 0u; sequence < 3u && passed; ++sequence) {
        for (size_t prefix = 0u; prefix < sequence_lengths[sequence]; ++prefix) {
            char *text = (char *)(boundary - prefix - 1u);
            memset(mapping, 0, (size_t)page_size_value);
            memcpy(text, sequences[sequence], prefix);
            text[prefix] = '\0';
            init_canvas(&fixture, false);
            if (watchy_ui_measure_text(&style, text).width != (int32_t)(prefix * 2u)) {
                passed = false;
                break;
            }
            watchy_ui_draw_text_font(&fixture.canvas, 0, 1, text, &style);
            if (black_pixel_count(&fixture) != prefix || !guards_unchanged(&fixture)) {
                passed = false;
                break;
            }
        }
    }
    CHECK(munmap(mapping, (size_t)page_size_value * 2u) == 0);
    CHECK(passed);
    return 0;
}
#else
static int test_truncated_utf8_never_reads_past_page_boundary_nul(void) {
    return 0;
}
#endif

static int test_missing_question_mark_skips_missing_and_invalid_input(void) {
    guarded_canvas_t fixture;
    const watchy_text_style_t style = {
        .font = &questionless_font,
        .tracking = 3,
        .black = true,
        .outlined = false,
    };
    init_canvas(&fixture, false);

    CHECK(watchy_ui_measure_text(&style, "\xe2\x98\x83").width == 0);
    CHECK(watchy_ui_measure_text(&style, "\x80").width == 0);
    CHECK(watchy_ui_measure_text(&style, "A\xe2\x98\x83").width == 4);
    watchy_ui_draw_text_font(&fixture.canvas, 0, 1, "\xe2\x98\x83\x80", &style);
    CHECK(black_pixel_count(&fixture) == 0u);
    CHECK(guards_unchanged(&fixture));
    return 0;
}

static int test_tabular_font_digits_have_equal_nonzero_advances(void) {
    const watchy_font_glyph_t *zero = watchy_font_find_glyph(&watchy_font_heros_62_bold, '0');
    CHECK(zero != NULL && zero->advance > 0u);
    for (uint32_t codepoint = '1'; codepoint <= '9'; ++codepoint) {
        const watchy_font_glyph_t *digit =
            watchy_font_find_glyph(&watchy_font_heros_62_bold, codepoint);
        CHECK(digit != NULL);
        CHECK(digit->advance == zero->advance);
    }
    return 0;
}

static int test_null_invalid_and_missing_inputs_are_deterministic(void) {
    guarded_canvas_t fixture;
    const watchy_text_style_t no_font = {
        .font = NULL, .tracking = 0, .black = true, .outlined = false,
    };
    const watchy_text_metrics_t null_style = watchy_ui_measure_text(NULL, "A");
    const watchy_text_metrics_t null_text = watchy_ui_measure_text(&no_font, NULL);
    init_canvas(&fixture, false);

    CHECK(null_style.width == 0 && null_style.height == 0u);
    CHECK(null_text.width == 0 && null_text.height == 0u);
    watchy_ui_draw_text_font(&fixture.canvas, 0, 0, NULL, &no_font);
    fixture.canvas.format = (watchy_pixel_format_t)99;
    watchy_ui_fill(&fixture.canvas, true);
    CHECK(black_pixel_count(&fixture) == 0u);
    CHECK(guards_unchanged(&fixture));
    return 0;
}

int main(void) {
    int failures = 0;
    failures += test_pixels_and_shapes_clip_at_all_four_edges_and_preserve_guards();
    failures += test_filled_and_outlined_rectangles_have_distinct_geometry();
    failures += test_rules_honor_one_two_and_three_pixel_thickness();
    failures += test_outline_and_filled_circles_render_expected_pixels();
    failures += test_measurement_uses_advances_tracking_and_no_trailing_tracking();
    failures += test_text_uses_baseline_bearings_normal_and_outlined_glyphs();
    failures += test_text_honors_positive_and_negative_horizontal_bearings();
    failures += test_centered_and_right_aligned_text_use_measured_advance();
    failures += test_white_text_on_black_canvas_inverts_only_glyph_pixels();
    failures += test_configured_unicode_uses_each_independent_glyph();
    failures += test_valid_absent_scalar_uses_distinct_question_mark_fallback();
    failures += test_invalid_utf8_consumes_one_bad_byte_and_uses_question_mark();
    failures += test_truncated_utf8_never_reads_past_page_boundary_nul();
    failures += test_missing_question_mark_skips_missing_and_invalid_input();
    failures += test_tabular_font_digits_have_equal_nonzero_advances();
    failures += test_null_invalid_and_missing_inputs_are_deterministic();
    if (failures == 0) {
        puts("ui draw tests passed");
    }
    return failures == 0 ? 0 : 1;
}
