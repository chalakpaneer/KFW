/*
 * test_sha256_vectors.c — SHA-256 known-answer tests (FIPS 180-4 / NIST)
 * Copyright (C) 2026 Kishan G A
 */
#include "integrity/kfw_integrity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int run = 0, ok = 0;
#define CHECK(desc, expr) do { ++run; if (expr) { ++ok; printf("[PASS] %s\n", desc); } \
    else printf("[FAIL] %s (line %d)\n", desc, __LINE__); } while (0)

int main(void)
{
    char hex[KFW_SHA256_HEX + 1];

    CHECK("empty string", kfw_sha256_buf("", 0, hex) == 0 &&
          strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0);

    CHECK("\"abc\"", kfw_sha256_buf("abc", 3, hex) == 0 &&
          strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);

    const char *two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    CHECK("448-bit message (padding boundary)", kfw_sha256_buf(two, strlen(two), hex) == 0 &&
          strcmp(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1") == 0);

    const char *three = "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";
    CHECK("896-bit message (two blocks)", kfw_sha256_buf(three, strlen(three), hex) == 0 &&
          strcmp(hex, "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1") == 0);

    char *mil = (char *)malloc(1000000);
    memset(mil, 'a', 1000000);
    CHECK("one million 'a'", kfw_sha256_buf(mil, 1000000, hex) == 0 &&
          strcmp(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0") == 0);

    /* streaming file hash must equal one-shot hash */
    FILE *f = fopen("/tmp/kfw_sha_test.bin", "wb");
    fwrite(mil, 1, 1000000, f); fclose(f);
    char hex2[KFW_SHA256_HEX + 1]; int64_t n = 0;
    CHECK("file hash == buffer hash", kfw_sha256_file("/tmp/kfw_sha_test.bin", hex2, &n) == 0
          && n == 1000000 && strcmp(hex, hex2) == 0);
    remove("/tmp/kfw_sha_test.bin");
    free(mil);

    printf("\n%d/%d passed\n", ok, run);
    return ok == run ? 0 : 1;
}
