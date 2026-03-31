#include "config.h"
#include <drm_fourcc.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <cairo/cairo.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_buffer.h>
#include "log.h"
#include "sway/icon_fallback.h"

#if defined(HAVE_GDK_PIXBUF) && HAVE_GDK_PIXBUF
#include <gdk-pixbuf/gdk-pixbuf.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

/* -- wlr_buffer implementation -- */

struct icon_buffer {
	struct wlr_buffer base;
	unsigned char *data;
	size_t stride;
};

static void icon_buffer_destroy(struct wlr_buffer *wlr_buffer) {
	struct icon_buffer *buffer = wl_container_of(wlr_buffer, buffer, base);
	free(buffer->data);
	free(buffer);
}

static bool icon_buffer_begin_data_ptr_access(struct wlr_buffer *wlr_buffer,
		uint32_t flags, void **data, uint32_t *format, size_t *stride) {
	struct icon_buffer *buffer = wl_container_of(wlr_buffer, buffer, base);
	(void)flags;
	*data = buffer->data;
	*stride = buffer->stride;
	*format = DRM_FORMAT_ARGB8888;
	return true;
}

static void icon_buffer_end_data_ptr_access(struct wlr_buffer *wlr_buffer) {
	(void)wlr_buffer;
}

static const struct wlr_buffer_impl icon_buffer_impl = {
	.destroy = icon_buffer_destroy,
	.begin_data_ptr_access = icon_buffer_begin_data_ptr_access,
	.end_data_ptr_access = icon_buffer_end_data_ptr_access,
};

static struct icon_buffer *icon_buffer_create(int width, int height) {
	if (width <= 0 || height <= 0) {
		return NULL;
	}
	struct icon_buffer *buffer = calloc(1, sizeof(*buffer));
	if (!buffer) {
		return NULL;
	}
	buffer->stride = (size_t)width * 4;
	buffer->data = calloc((size_t)height, buffer->stride);
	if (!buffer->data) {
		free(buffer);
		return NULL;
	}
	wlr_buffer_init(&buffer->base, &icon_buffer_impl, width, height);
	return buffer;
}

/* -- pixel conversion -- */

static bool copy_pixbuf_to_argb8888(const GdkPixbuf *pixbuf,
		unsigned char *dst, size_t dst_stride) {
	int channels = gdk_pixbuf_get_n_channels(pixbuf);
	if (channels != 3 && channels != 4) {
		return false;
	}
	int width = gdk_pixbuf_get_width(pixbuf);
	int height = gdk_pixbuf_get_height(pixbuf);
	int src_stride = gdk_pixbuf_get_rowstride(pixbuf);
	const guint8 *src = gdk_pixbuf_read_pixels(pixbuf);
	if (!src) {
		return false;
	}

	bool has_alpha = channels == 4;

	for (int y = 0; y < height; ++y) {
		const guint8 *s = src + y * src_stride;
		uint32_t *d = (uint32_t *)(dst + y * dst_stride);

		for (int x = 0; x < width; ++x) {
			uint8_t r = s[0];
			uint8_t g = s[1];
			uint8_t b = s[2];
			uint8_t a = has_alpha ? s[3] : 0xFF;
			uint8_t pr = (uint16_t)r * a / 255;
			uint8_t pg = (uint16_t)g * a / 255;
			uint8_t pb = (uint16_t)b * a / 255;
			d[x] = ((uint32_t)a << 24) |
				((uint32_t)pr << 16) |
				((uint32_t)pg << 8) |
				(uint32_t)pb;
			s += channels;
		}
	}
	return true;
}

static struct wlr_buffer *icon_pixbuf_to_buffer(GdkPixbuf *pixbuf,
		const char *icon_name) {
	if (!pixbuf) {
		return NULL;
	}
	int width = gdk_pixbuf_get_width(pixbuf);
	int height = gdk_pixbuf_get_height(pixbuf);
	struct icon_buffer *buffer = icon_buffer_create(width, height);
	if (!buffer) {
		return NULL;
	}
	if (!copy_pixbuf_to_argb8888(pixbuf, buffer->data, buffer->stride)) {
		wlr_buffer_drop(&buffer->base);
		return NULL;
	}
	sway_log(SWAY_DEBUG, "Converted pixbuf to buffer for %s (%dx%d)",
		icon_name ? icon_name : "unknown", width, height);
	return &buffer->base;
}

/* -- PNG loading -- */

static struct wlr_buffer *icon_load_png_scaled(const char *path, int size) {
	cairo_surface_t *surface = NULL;
	cairo_surface_t *scaled = NULL;
	cairo_t *cr = NULL;
	struct icon_buffer *buffer = NULL;
	struct wlr_buffer *result = NULL;

	surface = cairo_image_surface_create_from_png(path);
	if (!surface || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
		goto cleanup;
	}
	int src_w = cairo_image_surface_get_width(surface);
	int src_h = cairo_image_surface_get_height(surface);
	if (src_w <= 0 || src_h <= 0) {
		goto cleanup;
	}
	int out_h = size;
	int out_w = (int)((int64_t)src_w * out_h / src_h);
	out_w = out_w > 0 ? out_w : 1;
	scaled = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, out_w, out_h);
	if (!scaled || cairo_surface_status(scaled) != CAIRO_STATUS_SUCCESS) {
		goto cleanup;
	}
	cr = cairo_create(scaled);
	if (!cr || cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
		goto cleanup;
	}
	cairo_scale(cr, (double)out_w / src_w, (double)out_h / src_h);
	cairo_set_source_surface(cr, surface, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
	cairo_paint(cr);
	cairo_surface_flush(scaled);

	unsigned char *src_data = cairo_image_surface_get_data(scaled);
	int src_stride = cairo_image_surface_get_stride(scaled);
	if (!src_data || src_stride <= 0) {
		goto cleanup;
	}
	buffer = icon_buffer_create(out_w, out_h);
	if (!buffer) {
		goto cleanup;
	}
	if ((size_t)src_stride < buffer->stride) {
		goto cleanup;
	}
	for (int y = 0; y < out_h; ++y) {
		memcpy(buffer->data + y * buffer->stride,
			src_data + y * src_stride, buffer->stride);
	}
	result = &buffer->base;
	buffer = NULL;
cleanup:
	if (buffer) {
		wlr_buffer_drop(&buffer->base);
	}
	if (cr) {
		cairo_destroy(cr);
	}
	if (scaled) {
		cairo_surface_destroy(scaled);
	}
	if (surface) {
		cairo_surface_destroy(surface);
	}
	return result;
}

/* -- path lookup -- */

static const int icon_common_sizes[] = {
	16, 22, 24, 32, 48, 64, 96, 128, 256,
};

static const char *icon_extensions[] = {
	"png", "svg", "xpm",
};

static char *try_format_path(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	int needed = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	if (needed < 0) {
		return NULL;
	}
	size_t cap = (size_t)needed + 1;
	char *candidate = calloc(1, cap);
	if (!candidate) {
		return NULL;
	}
	va_start(ap, fmt);
	vsnprintf(candidate, cap, fmt, ap);
	va_end(ap);
	if (access(candidate, R_OK) == 0) {
		return candidate;
	}
	free(candidate);
	return NULL;
}

// Search common fixed sizes, then scalable for a given theme
static char *search_theme_sizes(const char *base, const char *theme,
		const char *icon_name, const char *ext) {
	for (size_t i = 0; i < ARRAY_LEN(icon_common_sizes); i++) {
		char *candidate = try_format_path("%s/%s/%dx%d/apps/%s.%s",
			base, theme,
			icon_common_sizes[i], icon_common_sizes[i],
			icon_name, ext);
		if (candidate) {
			return candidate;
		}
	}
	return try_format_path("%s/%s/scalable/apps/%s.%s",
		base, theme, icon_name, ext);
}

// Exact size, then common sizes + scalable for theme, then hicolor fallback
static char *find_in_dir(const char *base, const char *theme,
		const char *icon_name, int size, const char *ext) {
	char *candidate = try_format_path("%s/%s/%dx%d/apps/%s.%s",
		base, theme, size, size, icon_name, ext);
	if (candidate) {
		return candidate;
	}
	candidate = search_theme_sizes(base, theme, icon_name, ext);
	if (candidate) {
		return candidate;
	}
	return search_theme_sizes(base, "hicolor", icon_name, ext);
}

static char *icon_find_theme_path(const char *icon_name, int size) {
	const char *theme = getenv("XDG_ICON_THEME");
	if (!theme || theme[0] == '\0') {
		theme = "hicolor";
	}

	char base[PATH_MAX];

	const char *xdg_data_home = getenv("XDG_DATA_HOME");
	if (xdg_data_home && xdg_data_home[0] != '\0') {
		int n = snprintf(base, sizeof(base), "%s/icons", xdg_data_home);
		if (n > 0 && (size_t)n < sizeof(base)) {
			for (size_t e = 0; e < ARRAY_LEN(icon_extensions); e++) {
				char *candidate = find_in_dir(base, theme, icon_name, size,
					icon_extensions[e]);
				if (candidate) {
					return candidate;
				}
			}
		}
	} else {
		const char *home = getenv("HOME");
		if (home && home[0] != '\0') {
			int n = snprintf(base, sizeof(base),
				"%s/.local/share/icons", home);
			if (n > 0 && (size_t)n < sizeof(base)) {
				for (size_t e = 0; e < ARRAY_LEN(icon_extensions); e++) {
					char *candidate = find_in_dir(base, theme, icon_name,
						size, icon_extensions[e]);
					if (candidate) {
						return candidate;
					}
				}
			}
		}
	}

	const char *xdg_data_dirs = getenv("XDG_DATA_DIRS");
	if (!xdg_data_dirs || xdg_data_dirs[0] == '\0') {
		xdg_data_dirs = "/usr/local/share:/usr/share";
	}

	char dirs[PATH_MAX];
	snprintf(dirs, sizeof(dirs), "%s", xdg_data_dirs);

	char *saveptr = NULL;
	for (char *dir = strtok_r(dirs, ":", &saveptr); dir;
			dir = strtok_r(NULL, ":", &saveptr)) {
		int n = snprintf(base, sizeof(base), "%s/icons", dir);
		if (n < 0 || (size_t)n >= sizeof(base)) {
			continue;
		}
		for (size_t e = 0; e < ARRAY_LEN(icon_extensions); e++) {
			char *candidate = find_in_dir(base, theme, icon_name, size,
				icon_extensions[e]);
			if (candidate) {
				return candidate;
			}
		}
	}

	return NULL;
}

/* -- loading helpers -- */

// Load icon via gdk-pixbuf as fallback (supports SVG, PNG, XPM, etc)
static struct wlr_buffer *icon_load_with_gdk_pixbuf(
		const char *icon_path, const char *icon_name, int size) {
	GError *err = NULL;
	GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(
		icon_path, size, size, true, &err);
	if (pixbuf) {
		struct wlr_buffer *buffer = icon_pixbuf_to_buffer(pixbuf, icon_name);
		g_object_unref(pixbuf);
		return buffer;
	}
	sway_log(SWAY_DEBUG, "Icon fallback: gdk-pixbuf load failed for %s: %s",
		icon_name, err ? err->message : "unknown error");
	if (err) {
		g_error_free(err);
	}
	return NULL;
}

static bool icon_path_is_png(const char *icon_path) {
	const char *ext = strrchr(icon_path, '.');
	return ext && strcmp(ext, ".png") == 0;
}

// Try loading PNG via cairo first (faster, better scaling)
static struct wlr_buffer *icon_try_load_png(
		const char *icon_path, const char *icon_name, int size) {
	if (!icon_path_is_png(icon_path)) {
		return NULL;
	}
	struct wlr_buffer *buffer = icon_load_png_scaled(icon_path, size);
	if (buffer) {
		sway_log(SWAY_DEBUG, "Icon fallback: loaded PNG for %s", icon_name);
	}
	return buffer;
}
#endif

/* -- public API -- */

struct wlr_buffer *icon_fallback_load_buffer(const char *icon_name, int size) {
#if !defined(HAVE_GDK_PIXBUF) || !HAVE_GDK_PIXBUF
	(void)icon_name;
	(void)size;
	return NULL;
#else
	if (!icon_name || icon_name[0] == '\0' || size <= 0) {
		return NULL;
	}

	char *icon_path = icon_find_theme_path(icon_name, size);
	if (!icon_path) {
		sway_log(SWAY_DEBUG, "Icon fallback: no path for name=%s size=%d",
			icon_name, size);
		return NULL;
	}

	sway_log(SWAY_DEBUG, "Icon fallback: resolved name=%s path=%s size=%d",
		icon_name, icon_path, size);

	// Try PNG via cairo first, then fall back to gdk-pixbuf for other formats
	struct wlr_buffer *buffer = icon_try_load_png(icon_path, icon_name, size);
	if (!buffer) {
		buffer = icon_load_with_gdk_pixbuf(icon_path, icon_name, size);
	}

	free(icon_path);
	return buffer;
#endif
}
