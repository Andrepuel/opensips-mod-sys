/*
 * $Id: xmpp2simple.c 1666 2007-03-02 13:40:09Z anca_vamanu $
 *
 * pua_xmpp module - presence SIP - XMPP Gateway
 *
 * Copyright (C) 2007 Voice Sistem S.R.L.
 *
 * This file is part of openser, a free SIP server.
 *
 * openser is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * openser is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program; if not, write to the Free Software 
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * History:
 * --------
 *  2007-03-29  initial version (anca)
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <libxml/parser.h>

#include "../../parser/msg_parser.h"
#include "../../parser/parse_from.h"
#include "../../parser/parse_to.h"
#include "../../parser/parse_content.h"
#include "../../mem/mem.h"
#include "../../ut.h"
#include "../pua/pua.h"
#include "pidf.h"
#include "pua_xmpp.h"

int build_publish(xmlNodePtr pres_node, int expire);
int presence_subscribe(xmlNodePtr pres_node, int expires, int flag);

/*  the function registered as a callback in xmpp,
 *  to be called when a new message with presence type is received 
 *  */

void pres_Xmpp2Sip(char *msg, int type, void *param)
{
	xmlDocPtr doc= NULL;
	xmlNodePtr pres_node= NULL;
	char* pres_type= NULL;

	doc= xmlParseMemory(msg, strlen(msg));
	if(doc == NULL)
	{
		DBG("PUA_XMPP:pres_Xmpp2Sip Error while parsing xml memory\n");
		return;
	}

	pres_node= xmlDocGetNodeByName(doc, "presence", NULL);
	if(pres_node == NULL)
	{
		DBG("PUA_XMPP:pres_Xmpp2Sip Error while getting node\n");
		goto error;
	}
	pres_type= xmlNodeGetAttrContentByName(pres_node, "type" );
	
	if(pres_type== NULL )
	{
		DBG("PUA_XMPP:pres_Xmpp2Sip: type attribut not present\n");
		build_publish(pres_node, -1);
		if(presence_subscribe(pres_node, 3600, XMPP_SUBSCRIBE)< 0)
		{
				LOG(L_ERR, "PUA_XMPP:pres_Xmpp2Sip:ERROR when sending"
						" subscribe for presence");
				goto error;
		}


		/* send subscribe after publish because in xmpp subscribe message
		 * comes only when a new contact is inserted in buddy list */
	}
	else
	if(strcmp(pres_type, "unavailable")== 0)
	{
		build_publish(pres_node, 0);
		if(presence_subscribe(pres_node, 3600, XMPP_SUBSCRIBE)< 0)
				/* else subscribe for one hour*/
		{
				LOG(L_ERR,"PUA_XMPP:pres_Xmpp2Sip:ERROR when unsubscribing"
						" for presence");
				goto error;
		}

	}		
	else
	if((strcmp(pres_type, "subscribe")==0)|| 
		( strcmp(pres_type, "unsubscribe")== 0)||
		 (strcmp(pres_type, "probe")== 0))
	{
		if(strcmp(pres_type, "subscribe")==0 || 
				strcmp(pres_type, "probe")== 0)
		{	
		    DBG("PUA_XMPP:pres_Xmpp2Sip: send Subscribe message"
					" (no time limit)\n");
			if(presence_subscribe(pres_node, -1,
						XMPP_INITIAL_SUBS)< 0)
			{
				LOG(L_ERR, "PUA_XMPP:pres_Xmpp2Sip:ERROR when sending"
						" subscribe for presence");
				goto error;
			}
		}	
		if(strcmp(pres_type, "unsubscribe")== 0)
		{
			if(presence_subscribe(pres_node, 0, 
						XMPP_INITIAL_SUBS)< 0)
			{
				LOG(L_ERR,"PUA_XMPP:pres_Xmpp2Sip:ERROR when unsubscribing"
						" for presence");
				goto error;
			}
		}
	}	
	//	else 
	//		send_reply_message(pres_node);

	xmlFreeDoc(doc);
	return ;

error:

	if(doc)
		xmlFreeDoc(doc);
	return ;

}	

str* build_pidf(xmlNodePtr pres_node, char* uri, char* resource)
{
	str* body= NULL;
	xmlDocPtr doc= NULL;
	xmlNodePtr root_node= NULL, status_node= NULL;
	xmlNodePtr node= NULL, person_node= NULL;
	xmlNodePtr tuple_node= NULL, basic_node= NULL;
	char* show_cont= NULL, *status_cont= NULL;
	char* entity= NULL;
	char* type= NULL;
	char* status= NULL;

	DBG("PUA_XMPP: build_pidf...\n");

	entity=(char*)pkg_malloc(7+ strlen(uri)*sizeof(char));
	if(entity== NULL)
	{	
		LOG(L_ERR, "PUA_XMPP:build_pidf: ERROR no more memory\n");
		goto error;
	}
	strcpy(entity, "pres:");
	memcpy(entity+5, uri+4, strlen(uri)-4);
	entity[1+ strlen(uri)]= '\0';
	DBG("PUA_XMPP:build_pidf: entity: %s\n", entity);

	doc= xmlNewDoc(BAD_CAST "1.0");
	if(doc== NULL)
		goto error;

	root_node = xmlNewNode(NULL, BAD_CAST "presence");
	if(root_node== 0)
		goto error;
    xmlDocSetRootElement(doc, root_node);

    xmlNewProp(root_node, BAD_CAST "xmlns",
			BAD_CAST "urn:ietf:params:xml:ns:pidf");
	xmlNewProp(root_node, BAD_CAST "xmlns:dm",
			BAD_CAST "urn:ietf:params:xml:ns:pidf:data-model");
	xmlNewProp(root_node, BAD_CAST  "xmlns:rpid",
			BAD_CAST "urn:ietf:params:xml:ns:pidf:rpid" );
	xmlNewProp(root_node, BAD_CAST "xmlns:c",
			BAD_CAST "urn:ietf:params:xml:ns:pidf:cipid");
	xmlNewProp(root_node, BAD_CAST "entity", BAD_CAST entity);
	
	tuple_node =xmlNewChild(root_node, NULL, BAD_CAST "tuple", NULL) ;
	if( tuple_node ==NULL)
	{
		LOG(L_ERR, "PUA_XMPP:build_pidf: ERRPR while adding child\n");
		goto error;
	}

	status_node = xmlNewChild(tuple_node, NULL, BAD_CAST "status", NULL) ;
	if( status_node ==NULL)
	{
		LOG(L_ERR, "PUA_XMPP:build_pidf: ERRPR while adding child\n");
		goto error;
	}

	type=  xmlNodeGetAttrContentByName(pres_node, "type");
	if(type== NULL)
	{
		basic_node = xmlNewChild(status_node, NULL, BAD_CAST "basic",
			BAD_CAST "open") ;
		if( basic_node ==NULL)
		{
			LOG(L_ERR, "PUA_XMPP: build_pidf: ERRPR while adding child\n");
			goto error;
		}

	}
	else
	{	
		basic_node = xmlNewChild(status_node, NULL, BAD_CAST "basic",
				BAD_CAST "closed") ;
		if( basic_node ==NULL)
		{
			LOG(L_ERR, "PUA_XMPP: build_pidf: ERRPR while adding child\n");
			goto error;
		}
		goto done;		
	}
	/*if no type present search for suplimentary information */
	status_cont= xmlNodeGetNodeContentByName(pres_node, "status", NULL);
	show_cont= xmlNodeGetNodeContentByName(pres_node, "show", NULL);
	
	if(show_cont)
	{
		if(strcmp(show_cont, "xa")== 0)
			status= "not available";
		else
			if(strcmp(show_cont, "chat")== 0)
				status= "free for chat";
		else
			if(strcmp(show_cont, "dnd")== 0)
				status= "do not disturb";
		else
			status= show_cont;
	}

	if(status_cont)
	{
		/*
		person_node= xmlNewChild(root_node, NULL, BAD_CAST "person", 0);
		if(person_node== NULL)
		{
			LOG(L_ERR, "PUA_XMPP: build_pidf: Error while adding node\n");
			goto error;
		}
		*/
		node = xmlNewChild(root_node, NULL, BAD_CAST "note",
				BAD_CAST status_cont);
		if(node== NULL)
		{
			LOG(L_ERR, "PUA_XMPP: build_pidf: Error while adding node\n");
			goto error;
		}
	}else
		if(show_cont)
		{
			node = xmlNewChild(root_node, NULL, BAD_CAST "note", 
					BAD_CAST status);
			if(node== NULL)
			{
				LOG(L_ERR, "PUA_XMPP: build_pidf: Error while adding node\n");
				goto error;
			}	
		}	

	if(show_cont)
	{
		DBG("PUA_XMPP: build_pidf: show_cont= %s\n", show_cont);		
		if(person_node== NULL)
		{	
			person_node= xmlNewChild(root_node, NULL, BAD_CAST "person",0 );
			if(person_node== NULL)
			{
				LOG(L_ERR, "PUA_XMPP: build_pidf: Error while adding node\n");
				goto error;
			}
		}
		node=  xmlNewChild(person_node, NULL, BAD_CAST "activities", 
				BAD_CAST 0);
		if(node== NULL)
		{
			LOG(L_ERR, "PUA_XMPP: build_pidf: Error while adding node\n");
			goto error;
		}

						
		if( xmlNewChild(person_node, NULL, BAD_CAST "note", 
					BAD_CAST status )== NULL)
		{
			LOG(L_ERR, "PUA_XMPP: build_pidf: Error while adding node\n");
			goto error;
		}


	}
		
	
done:	
	body= (str* )pkg_malloc(sizeof(str));
	if(body== NULL)
	{
		DBG("PUA_XMPP:build_pidf: ERROR no more memory\n");
		goto error;
	}
	xmlDocDumpFormatMemory(doc,(xmlChar**)(void*)&body->s, &body->len, 1);

	if(entity)
		pkg_free(entity);
	if(status_cont)
		xmlFree(status_cont);
	if(show_cont)
		xmlFree(show_cont);
	if(type)
		xmlFree(type);
	xmlFreeDoc(doc);
	
	return body;

error:
	DBG("error found\n");
	if(entity)
		pkg_free(entity);
	if(body)
	{
		if(body->s)
			xmlFree(body->s);
		pkg_free(body);
	}
	if(status_cont)
		xmlFree(status_cont);
	if(show_cont)
		xmlFree(show_cont);
	if(type)
		xmlFree(type);
	if(doc)
		xmlFreeDoc(doc);

	return NULL;
}	


int build_publish(xmlNodePtr pres_node, int expires)
{
	str* body= NULL;
	publ_info_t publ;
	char* uri= NULL, *resource= NULL;
	char* pres_uri= NULL;
	char* slash;
	int uri_len;
	str uri_str;

	DBG("PUA_XMPP: build publish .. \n");
	
	uri= xmlNodeGetAttrContentByName(pres_node, "from");
	if(uri== NULL)
	{
		DBG("PUA_XMPP:build_publish: Error while getting 'from' attribute\n");
		return -1;
	}
	uri_len= strlen(uri);

	slash= memchr(uri, '/', strlen(uri));
	if(slash)
	{
		uri_len= slash- uri;
		resource= (char*)pkg_malloc((strlen(uri)-uri_len)*sizeof(char));
		if(resource== NULL)
		{
			LOG(L_ERR,"PUA_XMPP:build_publish: ERROR no more memory\n");
			return -1;
		}
		strcpy(resource, slash+1);
		slash= '\0';
	}	
	pres_uri= euri_xmpp_sip(uri);
	if(pres_uri== NULL)
	{
		LOG(L_ERR, "PUA_XMPP:build_publish: Error while encoding"
				" xmpp-sip uri\n");
		goto error;	
	}	
	xmlFree(uri);
	uri_str.s= pres_uri;
	uri_str.len= strlen(pres_uri);

	body= build_pidf(pres_node, pres_uri, resource);
	if(body== NULL)
	{
		LOG(L_ERR, "PUA_XMPP:build_publish: Error while constructing"
				" PUBLISH body\n");
		goto error;
	}

	/* construct the publ_info_t structure */

	memset(&publ, 0, sizeof(publ_info_t));
	
	publ.pres_uri= &uri_str;

	DBG("PUA_XMPP:publ->pres_uri: %.*s  -  %d\n", publ.pres_uri->len, 
			publ.pres_uri->s, publ.pres_uri->len );

	publ.body= body;
	
	DBG("PUA_XMPP: publ->notify body: %.*s - %d\n", publ.body->len,
			publ.body->s,  publ.body->len);

	publ.source_flag|= XMPP_PUBLISH;
	publ.expires= expires;
	publ.event= PRESENCE_EVENT;
	publ.extra_headers= NULL;
	publ.content_type.s= "application/pidf+xml";
	publ.content_type.len= 20;

	if( pua_send_publish(&publ)< 0)
	{
		LOG(L_ERR, "build_publish: Error while sending publish\n");
		goto error;
	}

	if(resource)
		pkg_free(resource);
	if(body)
	{
		if(body->s)
			xmlFree(body->s);
		pkg_free(body);
	}

	return 0;

error:

	if(resource)
		pkg_free(resource);

	if(body)
	{
		if(body->s)
			xmlFree(body->s);
		pkg_free(body);
	}

	return -1;

}

int presence_subscribe(xmlNodePtr pres_node, int expires,int  flag)
{
	subs_info_t subs;
	char* to_uri= NULL, *from_uri= NULL;
	char* uri= NULL;
	char* type= NULL;
	str to_uri_str;
	str from_uri_str;

	uri= xmlNodeGetAttrContentByName(pres_node, "to"); 
	if(uri== NULL)
	{
		LOG(L_ERR, "PUA_XMPP: build_subscribe:ERRor while getting attribute"
				" from xml doc\n");
		return -1;
	}
	to_uri= duri_xmpp_sip(uri);
	if(to_uri== NULL)
	{
		LOG(L_ERR, "PUA_XMPP:build_subscribe:ERROR while decoding"
				" xmpp--sip uri\n");
		goto error;
	}
	xmlFree(uri);
	to_uri_str.s= to_uri;
	to_uri_str.len= strlen(to_uri);

	uri= xmlNodeGetAttrContentByName(pres_node, "from"); 
	if(uri== NULL)
	{
		LOG(L_ERR, "PUA_XMPP:build_subscribe:ERROR while getting attribute"
				" from xml doc\n");
		goto error;
	}
	from_uri= euri_xmpp_sip(uri);
	if(from_uri== NULL)
	{
		LOG(L_ERR, "PUA_XMPP:build_subscribe: Error while encoding"
				" xmpp-sip uri\n");
		goto error;	
	}	
	xmlFree(uri);
	from_uri_str.s= from_uri;
	from_uri_str.len= strlen(from_uri);
	
	memset(&subs, 0, sizeof(subs_info_t));

	subs.pres_uri= &to_uri_str;
	subs.watcher_uri= &from_uri_str;
	subs.contact= subs.watcher_uri;
	/*
	type= xmlNodeGetAttrContentByName(pres_node, "type" );
	if(strcmp(type, "subscribe")==0 ||strcmp(type, "probe")== 0)
		subs->flag|= INSERT_TYPE;
	else	
		if(strcmp(type, "unsubscribe")== 0)
			subs->flag|= UPDATE_TYPE;
	xmlFree(type);
	type= NULL;
	*/

	subs.source_flag|= flag;
	subs.event= PRESENCE_EVENT;
	subs.expires= expires;
	
	DBG("PUA_XMPP:build_subscribe: subs:\n");
	DBG("\tpres_uri= %.*s\n", subs.pres_uri->len,  subs.pres_uri->s);
	DBG("\twatcher_uri= %.*s\n", subs.watcher_uri->len,  subs.watcher_uri->s);
	DBG("\texpires= %d\n", subs.expires);

	if(pua_send_subscribe(&subs)< 0)
	{
		LOG(L_ERR, "PUA_XMPP:build_subscribe:Error while sending SUBSCRIBE\n");
		goto error;
	}
	return 0;

error:
	if(type)
		xmlFree(type);

	return -1;
}

#if 0
int send_reply_message(xmlNodePtr pres_node)
{
	DBG("PUA_XMPP: send_reply_message: Reply message received\n");
	return 1;
}
#endif


