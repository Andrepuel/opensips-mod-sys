/*
 * Oracle module interface
 *
 * Copyright (C) 2007,2008 TRUNK MOBILE
 *
 * This file is part of opensips, a free SIP server.
 *
 * opensips is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * opensips is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
 */
/*
 * History:
 * --------
 */

#include <sys/time.h>
#include <oci.h>
#include "../../sr_module.h"
#include "../../db/db.h"
#include "dbase.h"
#include "asynch.h"

static int oracle_mod_init(void);
static void destroy(void);
static int db_oracle_bind_api(const str* mod, db_func_t *dbb);




/*
 * Oracle database module interface
 */
static cmd_export_t cmds[] = {
	{"db_bind_api",         (cmd_function)db_oracle_bind_api,    0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0}
};


/*
 * Exported parameters
 */
static param_export_t params[] = {
	{"timeout",	STR_PARAM|USE_FUNC_PARAM, (void*)&set_timeout },
	{"reconnect",	STR_PARAM|USE_FUNC_PARAM, (void*)&set_reconnect },
	{0, 0, 0}
};


struct module_exports exports = {
	"db_oracle",
	MOD_TYPE_SQLDB,  /* class of this module */
	MODULE_VERSION,
	DEFAULT_DLFLAGS, /* dlopen flags */
	0,				 /* load function */
	NULL,            /* OpenSIPS module dependencies */
	cmds,
	0,
	params,          /*  module parameters */
	0,               /* exported statistics */
	0,               /* exported MI functions */
	0,               /* exported pseudo-variables */
	0,               /* exported transformations */
	0,               /* extra processes */
	0,               /* module pre-initialization function */
	oracle_mod_init, /* module initialization function */
	0,               /* response function*/
	destroy,         /* destroy function */
	0                /* per-child init function */
};


static int oracle_mod_init(void)
{
	sword major, minor, update, patch, port;

	OCIClientVersion(&major, &minor, &update, &patch, &port);
	LM_DBG("Oracle client version is %d.%d.%d.%d.%d\n",
		major, minor, update, patch, port);
	return 0;
}


static void destroy(void)
{
	LM_INFO("Oracle terminate\n");
	OCITerminate(OCI_DEFAULT);
}


static int db_oracle_bind_api(const str* mod, db_func_t *dbb)
{
	if(dbb==NULL)
		return -1;

	memset(dbb, 0, sizeof(db_func_t));

	dbb->use_table        = db_oracle_use_table;
	dbb->init             = db_oracle_init;
	dbb->close            = db_oracle_close;
	dbb->query            = db_oracle_query;
	dbb->raw_query        = db_oracle_raw_query;
	dbb->free_result      = db_oracle_free_result;
	dbb->insert           = db_oracle_insert;
	dbb->delete           = db_oracle_delete;
	dbb->update           = db_oracle_update;

	return 0;
}
