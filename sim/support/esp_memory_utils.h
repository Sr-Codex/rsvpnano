#pragma once
inline bool esp_ptr_external_ram(const void*) { return true; }
inline bool esp_ptr_internal(const void*) { return false; }
