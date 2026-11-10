#pragma once
#include <cstdint>
#include <cstring>
#ifndef PROGMEM
#define PROGMEM
#endif
#define PSTR(s) (s)
#define pgm_read_byte(a) (*reinterpret_cast<const uint8_t*>(a))
#define pgm_read_sbyte(a) (*reinterpret_cast<const int8_t*>(a))
#define pgm_read_word(a) ([](const void* p){ uint16_t v; std::memcpy(&v, p, 2); return v; }(a))
#define pgm_read_dword(a) ([](const void* p){ uint32_t v; std::memcpy(&v, p, 4); return v; }(a))
#define pgm_read_ptr(a) ([](const void* p){ const void* v; std::memcpy(&v, p, sizeof(v)); return v; }(a))
#define pgm_read_float(a) ([](const void* p){ float v; std::memcpy(&v, p, 4); return v; }(a))
#define memcpy_P std::memcpy
#define strlen_P std::strlen
