#pragma once

#include "asan_interface_internal.h"
#include "sanitizer_common/sanitizer_internal_defs.h"
#include "sanitizer_common/sanitizer_stacktrace.h"

extern "C" {
void *__asan_extra_spill_area();
}

namespace __asan {

using ::__sanitizer::StackTrace;
using ::__sanitizer::uptr;

#ifndef ASAN_MAPPING_H

#  define ASAN_SHADOW_OFFSET_CONST 0x000000007fff8000
#  define ASAN_SHADOW_OFFSET ASAN_SHADOW_OFFSET_CONST
#  define ASAN_SHADOW_SCALE 3
#  define ASAN_SHADOW_GRANULARITY (1ULL << ASAN_SHADOW_SCALE)
#  define MEM_TO_SHADOW(mem) \
    (((mem) >> ASAN_SHADOW_SCALE) + (ASAN_SHADOW_OFFSET))

#  define DO_ASAN_MAPPING_PROFILE 0  // Set to 1 to profile the functions below.

#  if DO_ASAN_MAPPING_PROFILE
#    define PROFILE_ASAN_MAPPING() AsanMappingProfile[__LINE__]++;
#  else
#    define PROFILE_ASAN_MAPPING()
#  endif

#endif  // ASAN_MAPPING_H

static inline bool AddressIsPoisoned_(uptr a) {
  PROFILE_ASAN_MAPPING();
  const uptr kAccessSize = 1;
  u8 *shadow_address = (u8 *)MEM_TO_SHADOW(a);
  s8 shadow_value = *shadow_address;
  if (shadow_value) {
    u8 last_accessed_byte =
        (a & (ASAN_SHADOW_GRANULARITY - 1)) + kAccessSize - 1;
    return (last_accessed_byte >= shadow_value);
  }
  return false;
}

// Return true if we can quickly decide that the region is unpoisoned.
// We assume that a redzone is at least 16 bytes.
static inline bool AsanQuickCheckForUnpoisonedRegion_(uptr beg, uptr size) {
  if (UNLIKELY(size == 0 || size > sizeof(uptr) * ASAN_SHADOW_GRANULARITY))
    return !size;

  uptr last = beg + size - 1;
  uptr shadow_first = MEM_TO_SHADOW(beg);
  uptr shadow_last = MEM_TO_SHADOW(last);
  uptr uptr_first = RoundDownTo(shadow_first, sizeof(uptr));
  uptr uptr_last = RoundDownTo(shadow_last, sizeof(uptr));
  if (LIKELY(((*reinterpret_cast<const uptr *>(uptr_first) |
               *reinterpret_cast<const uptr *>(uptr_last)) == 0)))
    return true;
  u8 shadow = AddressIsPoisoned_(last);
  for (; shadow_first < shadow_last; ++shadow_first)
    shadow |= *((u8 *)shadow_first);
  return !shadow;
}

// Behavior of functions like "memcpy" or "strcpy" is undefined
// if memory intervals overlap. We report error in this case.
// Macro is used to avoid creation of new frames.
static inline bool AsanRangesOverlap_(const char *offset1, uptr length1,
                                      const char *offset2, uptr length2) {
  return !((offset1 + length1 <= offset2) || (offset2 + length2 <= offset1));
}

#ifndef ASAN_MAPPING_H

#  undef ASAN_SHADOW_OFFSET_CONST
#  undef ASAN_SHADOW_OFFSET
#  undef ASAN_SHADOW_SCALE
#  undef ASAN_SHADOW_GRANULARITY
#  undef MEM_TO_SHADOW
#  undef DO_ASAN_MAPPING_PROFILE
#  undef PROFILE_ASAN_MAPPING

#endif  // ASAN_MAPPING_H

bool IsInterceptorSuppressed(const char *interceptor_name);
bool HaveStackTraceBasedSuppressions();
bool IsStackTraceSuppressed(const StackTrace *stack);
void ReportGenericError(uptr pc, uptr bp, uptr sp, uptr addr, bool is_write,
                        uptr access_size, u32 exp, bool fatal);
void ReportStringFunctionSizeOverflow(uptr offset, uptr size,
                                      BufferedStackTrace *stack);

}  // namespace __asan
