#pragma once
#include <stdint.h>

/* Opaque identifier; this is neither a system socket nor a pointer. */
typedef uint64_t GSY_SessionHandle;
#define GSY_INVALID_SESSION_HANDLE ((GSY_SessionHandle)0)
