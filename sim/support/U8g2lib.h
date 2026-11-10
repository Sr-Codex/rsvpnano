#pragma once
// Arduino_GFX implements its own decoder; this feature-detection header enables it.
#define U8G2_FONT_SECTION(name)
#ifndef U8G2_WITH_UNICODE
#define U8G2_WITH_UNICODE
#endif
