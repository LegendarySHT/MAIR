# xsan_config.cmake
# This file is used to configure whether sub sanitizer should be enabled.

# This file is used to configure whether sub sanitizer should be enabled.

# Define a list of sanitizers that XSan can potentially contain.
# You can add or remove sanitizers from this list.
set(XSAN_SANITIZERS TSAN ASAN UBSAN)

# Define XSAN_CONTAINS_TSAN to ON or OFF TSan
# This option is used to enable or disable ThreadSanitizer (TSan) globally.
option(XSAN_CONTAINS_TSAN "Enable ThreadSanitizer (TSan) globally" ON)

# Define XSAN_CONTAINS_ASAN to ON or OFF ASan
# This option is used to enable or disable AddressSanitizer (ASan) globally.
option(XSAN_CONTAINS_ASAN "Enable AddressSanitizer (ASan) globally" ON)

# Define XSAN_CONTAINS_ASAN to ON or OFF UBSan
# This option is used to enable or disable UndefinedBehaviorSanitizer (UBSan) globally.
option(XSAN_CONTAINS_UBSAN "Enable UndefinedBehaviorSanitizer (UBSan) globally" ON)

# Because we use this to generate the xsan_hooks_gen.h,in the xsan_hooks_gen.h,
# We use Xxxx such as Asan, Tsan to represent the enum that contains the hooks for the macro
set(XSAN_DELEGATED_SANITIZERS Asan Tsan)
