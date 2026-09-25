#pragma once
// Minimal Arduino stand-in for host (native) tests only.
// Covers exactly what the transport-agnostic sources under test use:
// fixed-width types, libc string/stdio, and a test-controlled millis().
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

uint32_t millis(void);
