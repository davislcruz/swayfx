#ifndef _SWAY_ICON_FALLBACK_H
#define _SWAY_ICON_FALLBACK_H

#include <wlr/types/wlr_buffer.h>

struct wlr_buffer *icon_fallback_load_buffer(const char *icon_name, int size);

#endif
