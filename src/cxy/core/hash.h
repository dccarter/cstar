#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t HashCode;

HashCode hashInit();
HashCode hashPtr(HashCode, const void *);
HashCode hashUint8(HashCode, uint8_t);
HashCode hashUint16(HashCode, uint16_t);
HashCode hashUint32(HashCode, uint32_t);
HashCode hashUint64(HashCode, uint64_t);
HashCode hashStr(HashCode, const char *);

HashCode hashRawBytes(HashCode, const void *, size_t);

#ifdef __cplusplus
}
#endif
