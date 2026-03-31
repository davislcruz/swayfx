#include <errno.h>
#include <stdlib.h>
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/output.h"
#include "sway/tree/arrange.h"

struct cmd_results *cmd_titlebar_icon_scale(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "titlebar_icon_scale", EXPECTED_EQUAL_TO, 1))) {
		return error;
	}

	errno = 0;
	char *inv = NULL;
	double scale = strtod(argv[0], &inv);
	if (errno != 0 || !inv || *inv != '\0' || scale <= 0.1 || scale > 3.0) {
		return cmd_results_new(CMD_FAILURE,
			"Expected 'titlebar_icon_scale <0.1..3.0>'");
	}

	config->titlebar_icon_scale = scale;

	for (int i = 0; i < root->outputs->length; ++i) {
		struct sway_output *output = root->outputs->items[i];
		arrange_workspace(output_get_active_workspace(output));
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}
