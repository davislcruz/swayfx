#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_xdg_toplevel_icon_v1.h>
#include "log.h"
#include "sway/desktop/transaction.h"
#include "sway/tree/view.h"

void handle_xdg_toplevel_icon_v1_set_icon(struct wl_listener *listener,
		void *data) {
	const struct wlr_xdg_toplevel_icon_manager_v1_set_icon_event *event = data;
	struct wlr_xdg_toplevel *toplevel = event->toplevel;

	struct sway_view *view = toplevel->base->data;
	if (!view) {
		return;
	}

	if (view->xdg_toplevel_icon) {
		wlr_xdg_toplevel_icon_v1_unref(view->xdg_toplevel_icon);
		view->xdg_toplevel_icon = NULL;
	}
	free(view->xdg_toplevel_icon_name);
	view->xdg_toplevel_icon_name = NULL;

	if (event->icon) {
		view->xdg_toplevel_icon =
			wlr_xdg_toplevel_icon_v1_ref(event->icon);
		if (event->icon->name) {
			view->xdg_toplevel_icon_name = strdup(event->icon->name);
		}
		sway_log(SWAY_DEBUG, "Icon set for view: name=%s",
			view->xdg_toplevel_icon_name ? view->xdg_toplevel_icon_name : "(null)");
	}

	if (view->container) {
		container_update_title_bar(view->container);
		transaction_commit_dirty();
	}
}
