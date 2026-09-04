#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "watchy/captive_portal.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

static const uint8_t EXAMPLE_A_QUERY[] = {
    0x12, 0x34, 0x01, 0x00,
    0x00, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x07, 'e', 'x', 'a', 'm', 'p', 'l', 'e',
    0x03, 'c', 'o', 'm', 0x00,
    0x00, 0x01, 0x00, 0x01,
};

static int test_valid_a_query_preserves_question_and_builds_compressed_answer(void) {
    static const uint8_t address[] = {192u, 168u, 4u, 1u};
    static const uint8_t expected_answer[] = {
        0xc0, 0x0c, 0x00, 0x01, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x04,
        192u, 168u, 4u, 1u,
    };
    uint8_t reply[512];
    size_t reply_size = 0u;

    CHECK(watchy_captive_dns_build_reply(EXAMPLE_A_QUERY,
                                         sizeof(EXAMPLE_A_QUERY), address,
                                         reply, sizeof(reply), &reply_size));
    CHECK(reply_size == sizeof(EXAMPLE_A_QUERY) + sizeof(expected_answer));
    CHECK(reply[0] == EXAMPLE_A_QUERY[0] && reply[1] == EXAMPLE_A_QUERY[1]);
    CHECK(reply[2] == 0x85u && reply[3] == 0x00u);
    CHECK(reply[4] == 0u && reply[5] == 1u);
    CHECK(reply[6] == 0u && reply[7] == 1u);
    CHECK(reply[8] == 0u && reply[9] == 0u);
    CHECK(reply[10] == 0u && reply[11] == 0u);
    CHECK(memcmp(reply + 12u, EXAMPLE_A_QUERY + 12u,
                 sizeof(EXAMPLE_A_QUERY) - 12u) == 0);
    CHECK(memcmp(reply + sizeof(EXAMPLE_A_QUERY), expected_answer,
                 sizeof(expected_answer)) == 0);
    CHECK(memcmp(reply + reply_size - 4u, address, 4u) == 0);
    return 0;
}

static int test_malformed_and_unsupported_queries_do_not_get_fabricated_answers(void) {
    static const uint8_t address[] = {192u, 168u, 4u, 1u};
    uint8_t query[sizeof(EXAMPLE_A_QUERY)];
    uint8_t reply[512];
    size_t reply_size = 99u;

    memcpy(query, EXAMPLE_A_QUERY, sizeof(query));
    CHECK(!watchy_captive_dns_build_reply(query, 13u, address,
                                          reply, sizeof(reply), &reply_size));

    memcpy(query, EXAMPLE_A_QUERY, sizeof(query));
    query[12] = 0xc0u;
    query[13] = 0x0cu;
    CHECK(!watchy_captive_dns_build_reply(query, sizeof(query), address,
                                          reply, sizeof(reply), &reply_size));

    memcpy(query, EXAMPLE_A_QUERY, sizeof(query));
    query[5] = 2u;
    CHECK(!watchy_captive_dns_build_reply(query, sizeof(query), address,
                                          reply, sizeof(reply), &reply_size));

    memcpy(query, EXAMPLE_A_QUERY, sizeof(query));
    query[2] = 0x09u;
    CHECK(!watchy_captive_dns_build_reply(query, sizeof(query), address,
                                          reply, sizeof(reply), &reply_size));

    memcpy(query, EXAMPLE_A_QUERY, sizeof(query));
    query[sizeof(query) - 3u] = 0x1cu;
    CHECK(!watchy_captive_dns_build_reply(query, sizeof(query), address,
                                          reply, sizeof(reply), &reply_size));

    memcpy(query, EXAMPLE_A_QUERY, sizeof(query));
    query[sizeof(query) - 1u] = 0x03u;
    CHECK(!watchy_captive_dns_build_reply(query, sizeof(query), address,
                                          reply, sizeof(reply), &reply_size));
    return 0;
}

static int test_reply_capacity_must_include_the_complete_answer(void) {
    static const uint8_t address[] = {192u, 168u, 4u, 1u};
    uint8_t reply[sizeof(EXAMPLE_A_QUERY) + 16u];
    size_t reply_size = 0u;

    CHECK(!watchy_captive_dns_build_reply(EXAMPLE_A_QUERY,
                                          sizeof(EXAMPLE_A_QUERY), address,
                                          reply, sizeof(reply) - 1u,
                                          &reply_size));
    return 0;
}

int main(void) {
    int failures = 0;
    failures += test_valid_a_query_preserves_question_and_builds_compressed_answer();
    failures += test_malformed_and_unsupported_queries_do_not_get_fabricated_answers();
    failures += test_reply_capacity_must_include_the_complete_answer();
    if (failures == 0) {
        puts("captive portal tests passed");
    }
    return failures == 0 ? 0 : 1;
}
