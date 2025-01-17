/*
 * Copyright (C) 2007 Voice Sistem SRL
 *
 * This file is part of opensips, a free SIP server.
 *
 * opensips is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * opensips is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 *
 * History:
 * ---------
 *  2007-06-25  first version (ancuta)
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

#include "../../resolve.h"
#include "mi_datagram.h"
#include "datagram_fnc.h"
#include "mi_datagram_parser.h"
#include "mi_datagram_writer.h"
#include "../../mi/mi_trace.h"

/* solaris doesn't have SUN_LEN  */
#ifndef SUN_LEN
#define SUN_LEN(sa)	 ( strlen((sa)->sun_path) + \
					 (size_t)(((struct sockaddr_un*)0)->sun_path) )
#endif
/* AF_LOCAL is not defined on solaris */
#if !defined(AF_LOCAL)
#define AF_LOCAL AF_UNIX
#endif

int flags;
static char *mi_buf = 0;

typedef union {
	struct sockaddr_un un;
	struct sockaddr_in in;
} reply_addr_t;


typedef struct{
	reply_addr_t address;
	int address_len;
	int tx_sock;
}my_socket_address;

static reply_addr_t reply_addr;
static unsigned int reply_addr_len;

/* Timeout for sending replies in milliseconds */
extern int mi_socket_timeout;
static unsigned int mi_socket_domain;

extern sockaddr_dtgram mi_dtgram_addr;
extern trace_dest t_dst;
extern int mi_trace_mod_id;

static int mi_sock_check(int fd, char* fname);

static str CMD_FAILED_STR   = str_init(MI_COMMAND_FAILED_REASON);
static str CMD_NOT_AVL_STR  = str_init(MI_COMMAND_NOT_AVAILABLE_REASON);
static str PARSE_ERR_STR    = str_init(MI_PARSE_ERROR_REASON);
static str INTERNAL_ERR_STR = str_init(MI_INTERNAL_ERROR_REASON);

static str backend = str_init("datagram");
static union sockaddr_union *sv_socket=0;

int  mi_init_datagram_server(sockaddr_dtgram *addr, unsigned int socket_domain,
						rx_tx_sockets * socks, int mode, int uid, int gid )
{
	char * socket_name;

	/* create sockets rx and tx ... */
	/***********************************/
	mi_socket_domain = socket_domain;
	/**********************************/

	socks->rx_sock = socket(socket_domain, SOCK_DGRAM, 0);
	if (socks->rx_sock == -1) {
		LM_ERR("cannot create RX socket: %s\n", strerror(errno));
		return -1;
	}

	switch(socket_domain) {
		case AF_LOCAL:
			LM_DBG("we have a unix socket: %s\n", addr->unix_addr.sun_path);
			socket_name = addr->unix_addr.sun_path;
			if(bind(socks->rx_sock,(struct sockaddr*)&addr->unix_addr,
					SUN_LEN(&addr->unix_addr))< 0) {
				LM_ERR("bind: %s\n", strerror(errno));
				goto err_rx;
			}
			if(mi_sock_check(socks->rx_sock, socket_name)!=0)
				goto err_rx;
			/* change permissions */
			if (mode){
				if (chmod(socket_name, mode)<0){
					LM_ERR("failed to change the permissions for %s to %04o:"
						"%s[%d]\n",socket_name, mode, strerror(errno), errno);
					goto err_rx;
				}
			}
			/* change ownership */
			if ((uid!=-1) || (gid!=-1)){
				if (chown(socket_name, uid, gid)<0){
					LM_ERR("failed to change the owner/group for %s to %d.%d;"
					"%s[%d]\n",socket_name, uid, gid, strerror(errno), errno);
					goto err_rx;
				}
			}
			/* create TX socket */
			socks->tx_sock = socket( socket_domain, SOCK_DGRAM, 0);
			if (socks->tx_sock == -1) {
				LM_ERR("cannot create socket: %s\n", strerror(errno));
				goto err_rx;
			};
			/* Turn non-blocking mode on for tx*/
			flags = fcntl(socks->tx_sock, F_GETFL);
			if (flags == -1) {
				LM_ERR("fcntl failed: %s\n", strerror(errno));
				goto err_both;
			}
			if (fcntl(socks->tx_sock, F_SETFL, flags | O_NONBLOCK) == -1) {
				LM_ERR("fcntl: set non-blocking failed: %s\n",strerror(errno));
				goto err_both;
			}
			break;

		case AF_INET:
			if (bind(socks->rx_sock, &addr->udp_addr.s,
			sockaddru_len(addr->udp_addr))< 0) {
				LM_ERR("bind: %s\n", strerror(errno));
				goto err_rx;
			}
			socks->tx_sock = socks->rx_sock;
			break;
		case AF_INET6:
			if(bind(socks->rx_sock, (struct sockaddr*)&addr->udp_addr.sin6,
					sizeof(addr->udp_addr)) < 0) {
				LM_ERR("bind: %s\n", strerror(errno));
				goto err_rx;
			}
			socks->tx_sock = socks->rx_sock;
			break;
		default:
			LM_ERR("domain not supported\n");
			goto err_rx;

	}

	return 0;
err_both:
	close(socks->tx_sock);
err_rx:
	close(socks->rx_sock);
	return -1;
}



int mi_init_datagram_buffer(void){

	mi_buf = pkg_malloc(DATAGRAM_SOCK_BUF_SIZE + 1);
	if ( mi_buf==NULL) {
		LM_ERR("no more pkg memory\n");
		return -1;
	}
	return 0;
}

/* reply socket security checks:
 * checks if fd is a socket, is not hardlinked and it's not a softlink
 * opened file descriptor + file name (for soft link check)
 * returns 0 if ok, <0 if not */
int mi_sock_check(int fd, char* fname)
{
	struct stat fst;
	struct stat lst;

	if (fstat(fd, &fst)<0){
		LM_ERR("fstat failed: %s\n",
		strerror(errno));
		return -1;
	}
	/* check if socket */
	if (!S_ISSOCK(fst.st_mode)){
		LM_ERR("%s is not a sock\n", fname);
		return -1;
	}
	/* check if hard-linked */
	if (fst.st_nlink>1){
		LM_ERR("security: sock_check: %s is hard-linked %d times\n",
				fname, (unsigned)fst.st_nlink);
		return -1;
	}

	/* lstat to check for soft links */
	if (lstat(fname, &lst)<0){
		LM_ERR("lstat failed: %s\n", strerror(errno));
		return -1;
	}
	if (S_ISLNK(lst.st_mode)){
		LM_ERR("security: sock_check: %s is a soft link\n", fname);
		return -1;
	}
	/* if this is not a symbolic link, check to see if the inode didn't
	 * change to avoid possible sym.link, rm sym.link & replace w/ sock race
	 */
	/*LM_DBG("for %s lst.st_dev %fl fst.st_dev %i lst.st_ino %i fst.st_ino"
		"%i\n", fname, lst.st_dev, fst.st_dev, lst.st_ino, fst.st_ino);*/
	/*if ((lst.st_dev!=fst.st_dev)||(lst.st_ino!=fst.st_ino)){
		LM_ERR("security: sock_check: "
			"socket	%s inode/dev number differ: %d %d \n", fname,
			(int)fst.st_ino, (int)lst.st_ino);
	}*/
	/* success */
	return 0;
}



/* this function sends the reply over the reply socket */
static int mi_send_dgram(int fd, char* buf, unsigned int len,
				const struct sockaddr* to, int tolen, int timeout)
{
	int n;
	size_t total_len;
	total_len = strlen(buf);

	/*LM_DBG("response is %s \n tolen is %i "
			"and len is %i\n",buf,	tolen,len);*/

	if(total_len == 0 || tolen ==0)
		return -1;

	if (total_len>DATAGRAM_SOCK_BUF_SIZE)
	{
		LM_DBG("datagram too big, "
			"truncking, datagram_size is %i\n",DATAGRAM_SOCK_BUF_SIZE);
		len = DATAGRAM_SOCK_BUF_SIZE;
	}
	/*LM_DBG("destination address length is %i\n", tolen);*/
	n=sendto(fd, buf, len, 0, to, tolen);
	return n;
}



/*function that verifyes that the function from the datagram's first
 * line is correct and exists*/
static int identify_command(datagram_stream * dtgram, struct mi_cmd * *f)
{
	char *command,*p;

	/* default offset for the command: 0 */
	p= dtgram->start;
	if (!p){
		LM_ERR("null pointer\n");
		return -1;
	}

	/*if no command*/
	if ( dtgram->len ==0 ){
		LM_DBG("command empty case1\n");
		goto error;
	}
	if (*p != MI_CMD_SEPARATOR){
		LM_ERR("command must begin with: %c\n", MI_CMD_SEPARATOR);
		goto error;
	}
	command = p+1;

	LM_DBG("the command starts here: %s\n", command);
	p=strchr(command, MI_CMD_SEPARATOR );
	if (!p ){
		LM_ERR("empty command \n");
		goto error;
	}
	if(*(p+1)!='\n'){
		LM_ERR("the request's first line is invalid :no newline after "
				"the second %c\n",MI_CMD_SEPARATOR);
		goto error;
	}

	/* make command zero-terminated */
	*p=0;
	LM_DBG("the command is %s\n",command);
	/*search for the appropiate command*/
	*f=lookup_mi_cmd( command, p-command);
	if(!*f)
		goto error;
	/*the current offset has changed*/
	LM_DBG("dtgram->len is %i\n", dtgram->len);
	dtgram->current = p+2 ;
	dtgram->len -=p+2 - dtgram->start;
	LM_DBG("dtgram->len is %i\n",dtgram->len);

	return 0;
error:
	return -1;
}


/*************************** async functions ******************************/
static inline void free_async_handler( struct mi_handler *hdl )
{
	if (hdl)
		shm_free(hdl);
}


static void datagram_close_async(struct mi_root *mi_rpl,struct mi_handler *hdl,
																	int done)
{
	datagram_stream dtgram;
	int ret;
	my_socket_address *p;

	p = (my_socket_address *)hdl->param;

	if ( mi_rpl!=0 || done )
	{
		if (mi_rpl!=0) {
			/*allocate the response datagram*/
			dtgram.start = pkg_malloc(DATAGRAM_SOCK_BUF_SIZE);
			if(!dtgram.start){
				LM_ERR("no more pkg memory\n");
				goto err;
			}
			/*build the response*/
			if(mi_datagram_write_tree(&dtgram , mi_rpl) != 0){
				LM_ERR("failed to build the response\n");
				goto err1;
			}
			LM_DBG("the response is %s", dtgram.start);

			/*send the response*/
			ret = mi_send_dgram(p->tx_sock, dtgram.start,
							dtgram.current - dtgram.start,
							 (struct sockaddr *)&p->address,
							 p->address_len, mi_socket_timeout);
			if (ret>0){
				LM_DBG("the response: %s has been sent in %i octets\n",
					dtgram.start, ret);
			}else{
				LM_ERR("failed to send the response, ret is %i | errno=%d\n",
						ret, errno);
			}
			free_mi_tree( mi_rpl );
			pkg_free(dtgram.start);
		} else {
			if (mi_send_dgram(p->tx_sock, MI_COMMAND_FAILED, MI_COMMAND_FAILED_LEN,
							(struct sockaddr*)&reply_addr, reply_addr_len,
							mi_socket_timeout) < 0)
				LM_ERR("failed to send reply %s | errno=%d\n",
						MI_COMMAND_FAILED, errno);
		}
	}

	if (done)
		free_async_handler( hdl );

	return;

err1:
	pkg_free(dtgram.start);
err:
	return;
}



static inline struct mi_handler* build_async_handler(unsigned int sock_domain,
								reply_addr_t *reply_addr,
								unsigned int reply_addr_len, int tx_sock)
{
	struct mi_handler *hdl;
	void * p;
	my_socket_address * repl_address;


	hdl = (struct mi_handler*)shm_malloc( sizeof(struct mi_handler) +
			sizeof(my_socket_address));
	if (hdl==0) {
		LM_ERR("no more shm mem\n");
		return 0;
	}

	p = (void *)((hdl) + 1);
	repl_address = p;

	memcpy( &repl_address->address, reply_addr, sizeof(reply_addr_t));

	repl_address->address_len  = reply_addr_len;
	repl_address->tx_sock = tx_sock;

	hdl->handler_f = datagram_close_async;
	hdl->param = (void*)repl_address;

	return hdl;
}

static inline void trace_datagram( struct mi_cmd* f, char* cmd, int len,
		struct mi_root* mi_req, str* error, int code, str* message)
{

	char* command;
	union sockaddr_union cl_socket;

	if ( f && !is_mi_cmd_traced( mi_trace_mod_id, f) )
		return;

	memcpy( &cl_socket.sin, &reply_addr.in, sizeof(reply_addr.in));


	if ( !sv_socket ) {
		sv_socket = &mi_dtgram_addr.udp_addr;
	}

	if ( cmd )
		command = cmd ;
	else
		command = "";

	mi_trace_request( &cl_socket, sv_socket, command,
								len, mi_req, &backend, t_dst);

	mi_trace_reply( sv_socket, &cl_socket, code, error, message, t_dst);
}

static inline void trace_datagram_request( struct mi_cmd* f, char* cmd, int len, struct mi_root* mi_req)
{
	char* command;
	union sockaddr_union cl_socket;

	/* command not traced */
	if ( f && !is_mi_cmd_traced( mi_trace_mod_id, f) )
		return;

	memcpy( &cl_socket.sin, &reply_addr.in, sizeof(reply_addr.in));


	if ( !sv_socket ) {
		sv_socket = &mi_dtgram_addr.udp_addr;
	}

	if ( cmd )
		command = cmd ;
	else
		command = "";

	mi_trace_request( &cl_socket, sv_socket, command,
								len, mi_req, &backend, t_dst);
}

static inline void trace_datagram_reply( struct mi_cmd* f, str* error, int code, str* message)
{
	union sockaddr_union cl_socket;

	if ( f && !is_mi_cmd_traced( mi_trace_mod_id, f) )
		return;

	memcpy( &cl_socket.sin, &reply_addr.in, sizeof(reply_addr.in));

	mi_trace_reply( sv_socket, &cl_socket, code, error, message, t_dst);
}

void mi_datagram_server(int rx_sock, int tx_sock)
{
	struct mi_root *mi_cmd;
	struct mi_root *mi_rpl;
	struct mi_handler *hdl;
	struct mi_cmd * f;
	datagram_stream dtgram;

	/* buffer for the command */
	static char cmd_buf[128];
	str resp_message;

	int ret, len, cmd_len;

	ret = 0;
	f = 0;

	while(1){/*read the datagram*/
		reply_addr_len = sizeof(reply_addr);

		/* get the client's address */
		ret = recvfrom(rx_sock, mi_buf, DATAGRAM_SOCK_BUF_SIZE, 0,
					(struct sockaddr*)&reply_addr, &reply_addr_len);

		if (ret < 0) {
			LM_ERR("recvfrom %d: (%d) %s\n", ret, errno, strerror(errno));
			if ((errno == EINTR) ||
				(errno == EAGAIN) ||
				(errno == EWOULDBLOCK) ||
				(errno == ECONNREFUSED)) {
				LM_DBG("got %d (%s), going on\n", errno, strerror(errno));
				continue;
			}
			LM_DBG("error in recvfrom\n");
			continue;
		}

		if(ret == 0)
			continue;

		mi_buf[ret] = '\0';
		LM_DBG("received %d |%.*s|\n", ret, ret, mi_buf);

		if(ret> DATAGRAM_SOCK_BUF_SIZE){
				LM_ERR("buffer overflow\n");
				continue;
		}

		LM_DBG("mi_buf is %.*s and we have received %i bytes\n", ret, mi_buf, ret); /*mi_buff is not null terminated */
		dtgram.start 	= mi_buf;
		dtgram.len 		= ret;
		dtgram.current 	= dtgram.start;

		ret = identify_command(&dtgram, &f);
		/*analyze the command--from the first line*/
		if(ret != 0)
		{
			LM_ERR("command not available\n");
			if (mi_send_dgram(tx_sock, MI_COMMAND_NOT_AVAILABLE,
						  MI_COMMAND_AVAILABLE_LEN,
						  (struct sockaddr* )&reply_addr, reply_addr_len,
						  mi_socket_timeout) < 0)
				LM_ERR("failed to send reply %s | errno=%d\n",
						MI_COMMAND_NOT_AVAILABLE, errno);

			trace_datagram( 0, dtgram.start, dtgram.len, 0, &CMD_NOT_AVL_STR,
					MI_INTERNAL_ERR_CODE, 0);

			continue;
		}
		LM_DBG("we have a valid command \n");
		cmd_len = strlen(dtgram.start + 1);
		memcpy( cmd_buf, dtgram.start + 1, cmd_len);

		/* if asyncron cmd, build the async handler */
		if (f->flags&MI_ASYNC_RPL_FLAG) {
			hdl = build_async_handler(mi_socket_domain,
					&reply_addr, reply_addr_len, tx_sock);
			if (hdl==0) {
				LM_ERR("failed to build async handler\n");
				if (mi_send_dgram(tx_sock, MI_INTERNAL_ERROR,
						MI_INTERNAL_ERROR_LEN,(struct sockaddr* )&reply_addr,
						reply_addr_len, mi_socket_timeout) < 0)
				LM_ERR("failed to send reply %s | errno=%d\n",
						MI_INTERNAL_ERROR, errno);

				trace_datagram( f, cmd_buf, cmd_len, 0, &INTERNAL_ERR_STR,
						MI_INTERNAL_ERR_CODE, 0);
				continue;
			}
		} else{
			hdl = 0;
		}

		LM_DBG("after identifing the command, the received datagram is %s\n",
				dtgram.current);

		/*if no params required*/
		if (f->flags&MI_NO_INPUT_FLAG) {
			LM_DBG("the command has no params\n");
			mi_cmd = 0;
		} else {
			LM_DBG("parsing the command's params\n");
			mi_cmd = mi_datagram_parse_tree(&dtgram);

			if (mi_cmd==NULL){
				LM_ERR("failed to parse the MI tree\n");
				if (mi_send_dgram(tx_sock, MI_PARSE_ERROR, MI_PARSE_ERROR_LEN,
							  (struct sockaddr* )&reply_addr, reply_addr_len,
							  mi_socket_timeout))
					LM_ERR("failed to send reply %s | errno=%d\n",
							MI_PARSE_ERROR, errno);
				free_async_handler(hdl);

				trace_datagram( f, cmd_buf, cmd_len, 0, &PARSE_ERR_STR,
						MI_PARSE_ERR_CODE, 0);
				continue;
			}
			mi_cmd->async_hdl = hdl;
		}

		LM_DBG("done parsing the mi tree\n");
		if ( (mi_rpl=run_mi_cmd(f, mi_cmd,
		(mi_flush_f *)mi_datagram_flush_tree, &dtgram))==0 ) {
		/*error while running the command*/
			LM_ERR("failed to process the command\n");
			if (mi_send_dgram(tx_sock, MI_COMMAND_FAILED, MI_COMMAND_FAILED_LEN,
							(struct sockaddr* )&reply_addr, reply_addr_len,
							mi_socket_timeout))
				LM_ERR("failed to send reply %s | errno=%d\n",
						MI_COMMAND_FAILED, errno);

			trace_datagram( f, cmd_buf, cmd_len, 0, &CMD_FAILED_STR,
					MI_INTERNAL_ERR_CODE, 0);

			goto failure;
		}

		/*the command exited well*/
		LM_DBG("command process (%s)succeeded\n",f->name.s);

		if (mi_rpl!=MI_ROOT_ASYNC_RPL) {
			trace_datagram_request( f, cmd_buf, cmd_len, mi_cmd);

			if(mi_datagram_write_tree(&dtgram , mi_rpl) != 0){
				LM_ERR("failed to build the response \n");
				goto failure;
			}

			len = dtgram.current - dtgram.start;
			ret = mi_send_dgram(tx_sock, dtgram.start,len,
							(struct sockaddr* )&reply_addr,
							reply_addr_len, mi_socket_timeout);
			if (ret>0){
				LM_DBG("the response: %s has been sent in %i octets\n",
					dtgram.start, ret);
			}else{
				LM_ERR("failed to send the response: %s (%d)\n",
					strerror(errno), errno);
			}

			resp_message.s = dtgram.start;
			resp_message.len = len;

			trace_datagram_reply( f, &mi_rpl->reason, mi_rpl->code, &resp_message);

			free_mi_tree( mi_rpl );
			free_async_handler(hdl);
			if (mi_cmd) free_mi_tree( mi_cmd );
		}else {
			if (mi_cmd) free_mi_tree( mi_cmd );
		}

		continue;

failure:
		free_async_handler(hdl);
		/* destroy request tree */
		if (mi_cmd) free_mi_tree( mi_cmd );
		/* destroy the reply tree */
		if (mi_rpl) free_mi_tree(mi_rpl);
		continue;
	}
}

