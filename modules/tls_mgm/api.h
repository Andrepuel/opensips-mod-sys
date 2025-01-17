/*
 * Copyright (C) 2015 OpenSIPS Solutions
 *
 * This file is part of opensips, a free SIP server.
 *
 * opensips is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
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
 *
 * History:
 * -------
 *  2015-09-01  first version (cristi)
 */


#ifndef TLS_API_H
#define TLS_API_H

#include "../../resolve.h"

#include "tls_helper.h"

typedef struct tls_domain * (*tls_find_server_domain_f) (struct ip_addr *, unsigned short);
typedef struct tls_domain * (*tls_find_client_domain_f) (struct ip_addr *, unsigned short);
typedef int (*get_send_timeout_f) (void);
typedef int (*get_handshake_timeout_f) (void);
typedef void (*tls_release_domain_f) (struct tls_domain *);
#ifndef NO_SSL_GLOBAL_LOCK
typedef void (*tls_global_lock_get_f) (void);
typedef void (*tls_global_lock_release_f) (void);
#endif

struct tls_mgm_binds {
    get_send_timeout_f get_send_timeout;
    get_handshake_timeout_f get_handshake_timeout;
    tls_find_server_domain_f find_server_domain;
    tls_find_client_domain_f find_client_domain;
    tls_release_domain_f release_domain;
    #ifndef NO_SSL_GLOBAL_LOCK
    tls_global_lock_get_f global_lock_get;
    tls_global_lock_release_f global_lock_release;
    #endif
};


typedef int(*load_tls_mgm_f)(struct tls_mgm_binds *binds);

static int parse_domain_address(char *val, unsigned int len, struct ip_addr **ip,
								unsigned int *port)
{
	char *p = val;
	str s;

	/* get the IP */
	s.s = p;
	if ((p = q_memrchr(p, ':', len)) == NULL) {
		LM_ERR("Domain address has to be in [IP:port] format\n");
		goto parse_err;
	}
	s.len = p - s.s;
	p++;
	if ((*ip = str2ip(&s)) == NULL && (*ip = str2ip6(&s)) == NULL) {
		LM_ERR("[%.*s] is not an ip\n", s.len, s.s);
		goto parse_err;
	}

	/* what is left should be a port */
	s.s = p;
	s.len = val + len - p;
	if (str2int(&s, port) < 0) {
		LM_ERR("[%.*s] is not a port\n", s.len, s.s);
		goto parse_err;
	}

	return 0;

parse_err:
	LM_ERR("invalid TLS domain address [%s]\n", val);
	return -1;
}

static inline int
parse_domain_def(const str *val, str *out_name,
                 struct ip_addr **ip, unsigned int *port)
{
	char *p;
	int len;

	if (ZSTRP(val)) {
		LM_ERR("Empty domain definition\n");
		return -1;
	}

	p = val->s;
	len = val->len;

	/* get the domain name and optionally the address */
	out_name->s = p;
	if ((p = strchr(p, '=')) == NULL) {
		out_name->len = len;
		return 0;
	} else {
		if ((out_name->len = p - out_name->s) == 0) {
			LM_ERR("Empty domain name\n");
			return -1;
		}

		p++;
		len = strlen(p);
		return parse_domain_address(p, len, ip, port);
	}
}

static inline int load_tls_mgm_api(struct tls_mgm_binds *binds) {
    load_tls_mgm_f load_tls;

    /* import the DLG auto-loading function */
    if (!(load_tls = (load_tls_mgm_f) find_export("load_tls_mgm", 0, 0)))
        return -1;

    /* let the auto-loading function load all DLG stuff */
    if (load_tls(binds) == -1)
        return -1;

    return 0;
}

#endif	/* TLS_API_H */
