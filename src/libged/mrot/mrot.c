/*                         M R O T . C
 * BRL-CAD
 *
 * Copyright (c) 2008-2022 United States Government as represented by
 * the U.S. Army Research Laboratory.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this file; see the file named COPYING for more
 * information.
 */
/** @file libged/mrot.c
 *
 * The mrot command.
 *
 */

#include "common.h"

#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "../ged_private.h"


int
ged_mrot_core(struct ged *gedp, int argc, const char *argv[])
{
    int i;
    int ac;
    char *av[6];
    static const char *usage = "x y z";

    GED_CHECK_DATABASE_OPEN(gedp, BRLCAD_ERROR);
    GED_CHECK_VIEW(gedp, BRLCAD_ERROR);
    GED_CHECK_ARGC_GT_0(gedp, argc, BRLCAD_ERROR);

    /* initialize result */
    bu_vls_trunc(gedp->ged_result_str, 0);

    /* must be wanting help */
    if (argc == 1) {
	bu_vls_printf(gedp->ged_result_str, "Usage: %s %s", argv[0], usage);
	return BRLCAD_HELP;
    }

    if (argc != 2 && argc != 4) {
	bu_vls_printf(gedp->ged_result_str, "Usage: %s %s", argv[0], usage);
	return BRLCAD_ERROR;
    }

    av[0] = (char *)argv[0];
    av[1] = "-m";
    ac = argc+1;
    for (i = 1; i < argc; ++i)
	av[i+1] = (char *)argv[i];
    av[i+1] = (char *)0;

    return ged_exec(gedp, ac, (const char **)av);
}


#ifdef GED_PLUGIN
#include "../include/plugin.h"
struct ged_cmd_impl mrot_cmd_impl = {
    "mrot",
    ged_mrot_core,
    GED_CMD_DEFAULT
};

const struct ged_cmd mrot_cmd = { &mrot_cmd_impl };
const struct ged_cmd *mrot_cmds[] = { &mrot_cmd, NULL };

static const struct ged_plugin pinfo = { GED_API,  mrot_cmds, 1 };

COMPILER_DLLEXPORT const struct ged_plugin *ged_plugin_info()
{
    return &pinfo;
}
#endif /* GED_PLUGIN */

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
