/*
 * OpenSIPS LDAP Module
 *
 * Copyright (C) 2007 University of North Carolina
 *
 * Original author: Christian Schlatter, cs@unc.edu
 *
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
 *
 * History:
 * --------
 * 2007-02-18: Initial version
 */


#include <string.h>
#include <stdio.h>

#include <ldap.h>

#include "../../ut.h"
#include "../../str.h"
#include "../../pvar.h"
#include "../../usr_avp.h"
#include "../../mem/mem.h"
#include "../../async.h"
#include "ldap_exp_fn.h"
#include "ldap_connect.h"
#include "ldap_api_fn.h"
#include "ldap_escape.h"


#define STR_BUF_SIZE 1024
#define ESC_BUF_SIZE 65536

static char esc_buf[ESC_BUF_SIZE];

/*
* exported functions
*/
int resume_ldap_search(int fd, struct sip_msg *msg, void *param)
{
	int ld_result_count = 0, rc;
	struct ldap_async_params *as_params;

	as_params = (struct ldap_async_params*) param;

	rc = lds_resume( as_params, &ld_result_count);

	switch (rc) {
	case -1:
		/* error */
		pkg_free(as_params);
		return -1;
	case  0:
		/* put back in reactor */
		async_status = ASYNC_CONTINUE;
		return 1;
	case  1:
		/* successful */
		pkg_free(as_params);
		async_status  = ASYNC_DONE;

		break;
	default:
		LM_BUG("invalid return code\n");
		return -1;
	}

	if (ld_result_count < 1)
	{
		/* no LDAP entry found */
		LM_DBG("no LDAP entry found\n");
		return -1;
	}

	return ld_result_count;
}

int ldap_search_impl_async(
	struct sip_msg* _msg,
	async_ctx *ctx,
	pv_elem_t* _ldap_url)
{
	str ldap_url;
	int msgid;
	int sockfd;
	int rc=-1;
	int ld_result_count;
	struct ldap_async_params *as_params;
	struct ld_session *lds;
	struct ld_conn* conn;

	/*
	* do variable substitution for _ldap_url (pv_printf_s)
	*/
	if (_ldap_url==NULL) {
		LM_ERR("empty ldap_url\n");
		return -2;
	}
	if ( _ldap_url->spec.getf!=NULL) {
		if (pv_printf_s( _msg, _ldap_url, &ldap_url)!=0 || ldap_url.len<=0) {
			LM_ERR("pv_printf_s failed\n");
			return -2;
		}
	} else {
		ldap_url = _ldap_url->text;
	}

	/*
	* perform LDAP search
	*/
	if ((rc=ldap_url_search_async(ldap_url.s, &msgid, &lds, &conn, &ld_result_count)) < 0)
	{
		/* LDAP search error */
		rc = -2;
		goto error;
	}

	/* operation was completed in sync mode */
	if (rc == 1) {
		async_status = ASYNC_NO_IO;
		if (ld_result_count == 0) {
			/* no LDAP entry found */
			LM_DBG("no LDAP entry found\n");
			return -1;
		}
		return ld_result_count;
	}

	if (lds == NULL) {
		LM_ERR("invalid session handle\n");
		goto error;
	}

	if (ldap_get_option(conn->handle, LDAP_OPT_DESC, &sockfd) != LDAP_SUCCESS) {
		LM_ERR("failed to get ldap sockbuf\n");
		goto error;
	}

	as_params = pkg_malloc(sizeof(struct ldap_async_params));
	if (as_params == NULL) {
		LM_ERR("no more pkg mem\n");
		goto error;
	}

	as_params->msgid = msgid;
	as_params->ldap_url = ldap_url;
	as_params->lds	 = lds;
	as_params->conn  = conn;

	ctx->resume_param = as_params;
	ctx->resume_f = resume_ldap_search;/* resume function */
	async_status = sockfd;

	return 1;

error:
	release_ldap_connection(conn);
	return rc;
}


int ldap_search_impl(
	struct sip_msg* _msg,
	pv_elem_t* _ldap_url)
{
	str ldap_url;
	int ld_result_count = 0;

	/*
	* do variable substitution for _ldap_url (pv_printf_s)
	*/
	if (_ldap_url==NULL) {
		LM_ERR("empty ldap_url\n");
		return -2;
	}
	if ( _ldap_url->spec.getf!=NULL) {
		if (pv_printf_s( _msg, _ldap_url, &ldap_url)!=0 || ldap_url.len<=0) {
			LM_ERR("pv_printf_s failed\n");
			return -2;
		}
	} else {
		ldap_url = _ldap_url->text;
	}

	/*
	* perform LDAP search
	*/
	if (ldap_url_search(ldap_url.s, &ld_result_count) != 0)
	{
		/* LDAP search error */
		return -2;
	}
	if (ld_result_count < 1)
	{
		/* no LDAP entry found */
		LM_DBG("no LDAP entry found\n");
		return -1;
	}
	return ld_result_count;
}

int ldap_write_result(
	struct sip_msg* _msg,
	struct ldap_result_params* _lrp,
	struct subst_expr* _se)
{
	int                        dst_avp_name;
	int_str dst_avp_val;
	unsigned short             dst_avp_type;
	int                        nmatches, rc, i, added_avp_count = 0;
	struct berval              **attr_vals;
	str                        avp_val_str, *subst_result = NULL;
	int                        avp_val_int;

	/*
	* get dst AVP name (dst_avp_name)
	*/

	if (pv_get_avp_name(	_msg,
				&(_lrp->dst_avp_spec.pvp),
				&dst_avp_name,
				&dst_avp_type)
			!= 0)
	{
		LM_ERR("error getting dst AVP name\n");
		return -2;
	}

	/*
	* get LDAP attr values
	*/
	if ((rc = ldap_get_attr_vals(&_lrp->ldap_attr_name, &attr_vals)) != 0)
	{
		if (rc > 0) {
			return -1;
		} else {
			return -2;
		}
	}

	/*
	* add AVPs
	*/
	for (i = 0; attr_vals[i] != NULL; i++)
	{
		if (_se == NULL)
		{
			avp_val_str.s = attr_vals[i]->bv_val;
			avp_val_str.len = attr_vals[i]->bv_len;
		}
		else
		{
			subst_result = subst_str(attr_vals[i]->bv_val, _msg, _se,
					&nmatches);
			if ((subst_result == NULL) || (nmatches < 1))
			{
				continue;
			}
			avp_val_str = *subst_result;
		}

		if (_lrp->dst_avp_val_type == 1)
		{
			/* try to convert ldap value to integer */
			if (!str2sint(&avp_val_str, &avp_val_int))
			{
				dst_avp_val.n = avp_val_int;
				rc = add_avp(dst_avp_type, dst_avp_name, dst_avp_val);
			} else
			{
				continue;
			}
		} else
		{
			/* save ldap value as string */
			dst_avp_val.s = avp_val_str;
			rc = add_avp(dst_avp_type|AVP_VAL_STR, dst_avp_name, dst_avp_val);
		}

		if (subst_result != NULL) {
			if (subst_result->s != 0) {
				pkg_free(subst_result->s);
			}
			pkg_free(subst_result);
			subst_result = NULL;
		}

		if (rc < 0)
		{
			LM_ERR("failed to create new AVP\n");
			ldap_value_free_len(attr_vals);
			return -2;
		}
		added_avp_count++;
	}
	ldap_value_free_len(attr_vals);

	if (added_avp_count > 0)
	{
		return added_avp_count;
	} else
	{
		return -1;
	}
}

int ldap_result_next(void)
{
	int rc;

	rc = ldap_inc_result_pointer();
	switch (rc)
	{
	case 1:
		return -1;
	case 0:
		return 1;
	case -1:
	default:
		return -2;
	}
}

int ldap_result_check(
	struct sip_msg* _msg,
	struct ldap_result_check_params* _lrp,
	struct subst_expr* _se)
{
	str check_str, *subst_result = NULL;
	int rc, i, nmatches;
	char *attr_val;
	struct berval **attr_vals;

	/*
	* do variable substitution for check_str
	*/

	if (_lrp->check_str_elem_p)
	{
		if (pv_printf_s(_msg, _lrp->check_str_elem_p, &check_str) != 0)
		{
			LM_ERR("pv_printf_s failed\n");
			return -2;
		}
	} else
	{
		LM_ERR("empty check string\n");
		return -2;
	}

	LM_DBG("check_str [%s]\n", check_str.s);

	/*
	* get LDAP attr values
	*/

	if ((rc = ldap_get_attr_vals(&_lrp->ldap_attr_name, &attr_vals)) != 0)
	{
		if (rc > 0) {
			return -1;
		} else {
			return -2;
		}
	}

	/*
	* loop through attribute values
	*/

	for (i = 0; attr_vals[i] != NULL; i++)
	{
		if (_se == NULL)
		{
			attr_val = attr_vals[i]->bv_val;
		} else
		{
			subst_result = subst_str(attr_vals[i]->bv_val, _msg, _se,
					&nmatches);
			if ((subst_result == NULL) || (nmatches < 1))
			{
				continue;
			}
			attr_val = subst_result->s;
		}

		LM_DBG("attr_val [%s]\n", attr_val);
		rc = strncmp(check_str.s, attr_val, check_str.len);
		if (_se != NULL)
		{
			pkg_free(subst_result->s);
		}
		if (rc == 0)
		{
			ldap_value_free_len(attr_vals);
			return 1;
		}
	}

	ldap_value_free_len(attr_vals);
	return -1;
}

int ldap_filter_url_encode(
	struct sip_msg* _msg,
	pv_elem_t* _filter_component,
	pv_spec_t* _dst_avp_spec)
{
	str             filter_component_str, esc_str;
	int         dst_avp_name;
	unsigned short  dst_avp_type;

	/*
	* variable substitution for _filter_component
	*/
	if (_filter_component) {
		if (pv_printf_s(_msg, _filter_component, &filter_component_str) != 0) {
			LM_ERR("pv_printf_s failed\n");
			return -1;
		}
	} else {
		LM_ERR("empty first argument\n");
		return -1;
	}

	/*
	* get dst AVP name (dst_avp_name)
	*/
	if (pv_get_avp_name(_msg, &(_dst_avp_spec->pvp), &dst_avp_name,
				&dst_avp_type) != 0)
	{
		LM_ERR("error getting dst AVP name\n");
		return -1;
	}

	/*
	* apply LDAP filter escaping rules
	*/
	esc_str.s = esc_buf;
	esc_str.len = ESC_BUF_SIZE;
	if (ldap_rfc4515_escape(&filter_component_str, &esc_str, 1) != 0)
	{
		LM_ERR("ldap_rfc4515_escape() failed\n");
		return -1;
	}

	/*
	* add dst AVP
	*/
	if (add_avp(dst_avp_type|AVP_VAL_STR, dst_avp_name, (int_str)esc_str) != 0)
	{
		LM_ERR("failed to add new AVP\n");
		return -1;
	}

	return 1;
}
