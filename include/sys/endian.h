#ifndef _RODNIX_COMPAT_SYS_ENDIAN_H
#define _RODNIX_COMPAT_SYS_ENDIAN_H

#include <stdint.h>

static inline uint16_t bswap16(uint16_t v){ return (uint16_t)((v>>8)|(v<<8)); }
static inline uint32_t bswap32(uint32_t v){ return ((v & 0x000000FFu)<<24)|((v & 0x0000FF00u)<<8)|((v & 0x00FF0000u)>>8)|((v & 0xFF000000u)>>24); }

#define htobe16(_v) bswap16((uint16_t)(_v))
#define be16toh(_v) bswap16((uint16_t)(_v))
#define htobe32(_v) bswap32((uint32_t)(_v))
#define be32toh(_v) bswap32((uint32_t)(_v))
#define htole16(_v) ((uint16_t)(_v))
#define le16toh(_v) ((uint16_t)(_v))
#define htole32(_v) ((uint32_t)(_v))
#define le32toh(_v) ((uint32_t)(_v))

#endif
