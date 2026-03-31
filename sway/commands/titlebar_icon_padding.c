#include <stdlib.h>
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/output.h"
#include "sway/tree/arrange.h"

struct cmd_results *cmd_titlebar_icon_padding(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "titlebar_icon_padding", EXPECTED_EQUAL_TO, 1))) {
		return error;
	}

	char *inv = NULL;
	long value = strtol(argv[0], &inv, 10);
	if (!inv || *inv != '\0' || value < 0 || value > 64) {
		return cmd_results_new(CMD_FAILURE,
			"Expected 'titlebar_icon_padding <0..64>'");
	}

	config->titlebar_icon_padding = (int)value;

	for (int i = 0; i < root->outputs->length; ++i) {
		struct sway_output *output = root->outputs->items[i];
		arrange_workspace(output_get_active_workspace(output));
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}
