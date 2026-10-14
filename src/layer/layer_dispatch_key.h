#pragma once
#include <cstdint>
#include <type_traits>

template <typename T>
inline void* dispatch_key(T handle) {
    // State/ownership maps identify resources, not their shared dispatch table.
    // Non-dispatchable handles must never be dereferenced either.
    if constexpr (std::is_pointer_v<T>) return reinterpret_cast<void*>(handle);
    else return reinterpret_cast<void*>(static_cast<uintptr_t>(handle));
}
