/*
 * This file is distributed as part of MaxScale by MariaDB Corporation.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab 2014
 */

/**
 * @file fwfilter.c
 * Firewall Filter
 *
 * A filter that acts as a firewall, blocking queries that do not meet the set requirements.
 */
#include <my_config.h>
#include <stdint.h>
#include <ctype.h>
#include <stdio.h>
#include <fcntl.h>
#include <filter.h>
#include <string.h>
#include <atomic.h>
#include <modutil.h>
#include <log_manager.h>
#include <query_classifier.h>
#include <mysql_client_server_protocol.h>
#include <spinlock.h>
#include <session.h>
#include <plugin.h>
#include <skygw_types.h>


MODULE_INFO 	info = {
	MODULE_API_FILTER,
	MODULE_ALPHA_RELEASE,
	FILTER_VERSION,
	"Firewall Filter"
};

static char *version_str = "V1.0.0";

/*
 * The filter entry points
 */
static	FILTER	*createInstance(char **options, FILTER_PARAMETER **);
static	void	*newSession(FILTER *instance, SESSION *session);
static	void 	closeSession(FILTER *instance, void *session);
static	void 	freeSession(FILTER *instance, void *session);
static	void	setDownstream(FILTER *instance, void *fsession, DOWNSTREAM *downstream);
static	int	routeQuery(FILTER *instance, void *fsession, GWBUF *queue);
static	void	diagnostic(FILTER *instance, void *fsession, DCB *dcb);


static FILTER_OBJECT MyObject = {
	createInstance,
	newSession,
	closeSession,
	freeSession,
	setDownstream,
	NULL, 
	routeQuery,
	NULL,
	diagnostic,
};

#define QUERY_TYPES 5

/**
 * Query types
 */
typedef enum{
	ALL,
	SELECT,
	INSERT,
	UPDATE,
	DELETE
}querytype_t;

/**
 * Rule types
 */
typedef enum {
	RT_UNDEFINED,
    RT_USER,
    RT_COLUMN
}ruletype_t;

/**
 * Generic linked list of string values
 */ 

typedef struct item_t{
	struct item_t* next;
	char* value;
}ITEM;

/**
 * A link in a list of IP adresses and subnet masks
 */
typedef struct iprange_t{
	struct iprange_t* next;
	uint32_t ip;
	uint32_t mask;
}IPRANGE;

/**
 * The Firewall filter instance.
 */
typedef struct {
	HASHTABLE* htable;
	IPRANGE* networks;
	int column_count, column_size, user_count, user_size;
	bool require_where[QUERY_TYPES];
	bool block_wildcard, whitelist_users,whitelist_networks,def_op;
	
} FW_INSTANCE;

/**
 * The session structure for Firewall filter.
 */
typedef struct {
	DOWNSTREAM	down;
	UPSTREAM	up;
	SESSION*	session;
} FW_SESSION;

static int hashkeyfun(void* key);
static int hashcmpfun (void *, void *);

static int hashkeyfun(
		void* key)
{
  if(key == NULL){
    return 0;
  }
  unsigned int hash = 0,c = 0;
  char* ptr = (char*)key;
  while((c = *ptr++)){
    hash = c + (hash << 6) + (hash << 16) - hash;
  }
	return (int)hash > 0 ? hash : -hash; 
}

static int hashcmpfun(
		  void* v1,
		  void* v2)
{
  char* i1 = (char*) v1;
  char* i2 = (char*) v2;

  return strcmp(i1,i2);
}

static void* hstrdup(void* fval)
{
  char* str = (char*)fval;
  return strdup(str);
}


static void* hfree(void* fval)
{
  free (fval);
  return NULL;
}

/**
 * Utility function to check if a string contains a valid IP address.
 * The string handled as a null-terminated string.
 * @param str String to parse
 * @return True if the string contains a valid IP address.
 */
bool valid_ip(char* str)
{
    int octval = 0;
	bool valid = true;
	char cmpbuff[32];
	char *source = str,*dest = cmpbuff,*end = strchr(str,'\0');
    
	while(source < end && (int)(dest - cmpbuff) < 32 && valid){
		switch(*source){

		case '.':
		case '/':
		case ' ':
		case '\0':
			/**End of IP, string or octet*/
			*(dest++) = '\0';
			octval = atoi(cmpbuff);
			dest = cmpbuff;
			valid = octval < 256 && octval > -1 ? true: false;
			if(*source == '/' || *source == '\0' || *source == ' '){
				return valid;
			}else{
				source++;
			}
			break;

		default:
			/**In the IP octet, copy to buffer*/
			if(isdigit(*source)){
				*(dest++) = *(source++);
			}else{
				return false;
			}
			break;
		}
	}	
	
	return valid;
}
/**
 * Replace all non-essential characters with whitespace from a null-terminated string.
 * This function modifies the passed string.
 * @param str String to purify
 */
char* strip_tags(char* str)
{
	char *ptr = str, *lead = str, *tail = NULL;
	int len = 0;
	while(*ptr != '\0'){
		if(isalnum(*ptr) || *ptr == '.' || *ptr == '/'){
			ptr++;
			continue;
		}
		*ptr++ = ' ';
	}

	/**Strip leading and trailing whitespace*/

	while(*lead != '\0'){
		if(isspace(*lead)){
			lead++;
		}else{
			tail = strchr(str,'\0') - 1;
			while(tail > lead && isspace(*tail)){
				tail--;
			}
			len = (int)(tail - lead) + 1;
			memmove(str,lead,len);
			memset(str+len, 0, 1);
			break;
		}
	}
	return str;
}

/**
 * Get one octet of IP
 */
int get_octet(char* str)
{
    int octval = 0,retval = -1;
	bool valid = false;
	char cmpbuff[32];
	char *source = str,*dest = cmpbuff,*end = strchr(str,'\0') + 1;
    
	if(end == NULL){
		return retval;
	}

	while(source < end && (int)(dest - cmpbuff) < 32 && !valid){
		switch(*source){

			/**End of IP or string or the octet is done*/
		case '.':
		case '/':
		case ' ':
		case '\0':
			
			*(dest++) = '\0';
			source++;
			octval = atoi(cmpbuff);
			dest = cmpbuff;
			valid = octval < 256 && octval > -1 ? true: false;
			if(valid)
				{
					retval = octval;
				}
			
			break;

		default:
			/**In the IP octet, copy to buffer*/
			if(isdigit(*source)){
				*(dest++) = *(source++);
			}else{
				return -1;
			}
			break;
		}
	}	
	
	return retval;

}

/**
 *Convert string with IP address to an unsigned 32-bit integer
 * @param str String to convert
 * @return Value of the IP converted to an unsigned 32-bit integer or zero in case of an error.
 */
uint32_t strtoip(char* str)
{
	uint32_t ip = 0,octet = 0;
	char* tok = str;
	if(!valid_ip(str)){
		return 0;
	}
	octet = get_octet(tok) << 24;
	ip |= octet;
	tok = strchr(tok,'.') + 1;
	octet = get_octet(tok) << 16;
	ip |= octet;
	tok = strchr(tok,'.') + 1;
	octet = get_octet(tok) << 8;
	ip |= octet;
	tok = strchr(tok,'.') + 1;
	octet = get_octet(tok);
	ip |= octet;
	
	return ip;
}

/**
 *Convert string with a subnet mask to an unsigned 32-bit integer
 */
uint32_t strtosubmask(char* str)
{
	uint32_t mask = 0;
	char *ptr;
	
	if(!valid_ip(str) || 
	   (ptr = strchr(str,'/')) == NULL ||
	   !valid_ip(++ptr))
		{
			return mask;
		}
	
	mask = strtoip(ptr);
	return ~mask;
}



/**
 * Implementation of the mandatory version entry point
 *
 * @return version string of the module
 */
char *
version()
{
	return version_str;
}

/**
 * The module initialisation routine, called when the module
 * is first loaded.
 */
void
ModuleInit()
{
}

/**
* The module entry point routine. It is this routine that
* must populate the structure that is referred to as the
* "module object", this is a structure with the set of
* external entry points for this module.
*
* @return The module object
*/
FILTER_OBJECT *
GetModuleObject()
{
	return &MyObject;
}

void parse_rule(char* rule, FW_INSTANCE* instance)
{
	char* ptr = rule;
	bool allow,block,mode;

	/**IP range rules*/
	if((allow = (strstr(rule,"allow") != NULL)) || 
	   (block = (strstr(rule,"block") != NULL))){
		
		mode = allow ? true:false;

		if((ptr = strchr(rule,' ')) == NULL){
			return;
		}
		ptr++;

		if(valid_ip(ptr)){ /**Add IP address range*/			

			instance->whitelist_networks = mode;
			IPRANGE* rng = calloc(1,sizeof(IPRANGE));
			if(rng){
				rng->ip = strtoip(ptr);
				rng->mask = strtosubmask(ptr);
				rng->next = instance->networks;
				instance->networks = rng;
			}

		}else{ /**Add rules on usernames or columns*/
	    
			char *tok = strtok(ptr," ,\0");
			bool is_user = false, is_column = false, is_time = false;
			
			if(strcmp(tok,"wildcard") == 0){
				instance->block_wildcard = block ? true : false;
				return;
			}

			if(strcmp(tok,"users") == 0){/**Adding users*/
				instance->whitelist_users = mode;
				is_user = true;
			}else if(strcmp(tok,"columns") == 0){/**Adding Columns*/
				is_column = true;
			}

			tok = strtok(NULL," ,\0");

			if(is_user || is_column){
				while(tok){

					/**Add value to hashtable*/

					ruletype_t rtype = is_user ? RT_USER : is_column ? RT_COLUMN: RT_UNDEFINED;
					hashtable_add(instance->htable,
								  (void *)tok,
								  (void *)rtype);

					tok = strtok(NULL," ,\0");

				}
			}
		}

	}else if((ptr = strstr(rule,"require")) != NULL){
		
		if((ptr = strstr(ptr,"where")) != NULL &&
		   (ptr = strchr(ptr,' ')) != NULL){
			char* tok;

			ptr++;
			tok = strtok(ptr," ,\0");
			while(tok){
				if(strcmp(tok, "all") == 0){
					instance->require_where[ALL] = true;
					break;
				}else if(strcmp(tok, "select") == 0){
					instance->require_where[SELECT] = true;
				}else if(strcmp(tok, "insert") == 0){
					instance->require_where[INSERT] = true;
				}else if(strcmp(tok, "update") == 0){
					instance->require_where[UPDATE] = true;
				}else if(strcmp(tok, "delete") == 0){
					instance->require_where[DELETE] = true;
				}
				tok = strtok(NULL," ,\0");
			}
			
		}

	}

}

/**
 * Create an instance of the filter for a particular service
 * within MaxScale.
 * 
 * @param options	The options for this filter
 *
 * @return The instance data for this new instance
 */
static	FILTER	*
createInstance(char **options, FILTER_PARAMETER **params)
{
	FW_INSTANCE	*my_instance;
  
	if ((my_instance = calloc(1, sizeof(FW_INSTANCE))) == NULL){
		return NULL;
	}
	int i;
	HASHTABLE* ht;

	if((ht = hashtable_alloc(7, hashkeyfun, hashcmpfun)) == NULL){
		skygw_log_write(LOGFILE_ERROR, "Unable to allocate hashtable.");
		return NULL;
	}

	hashtable_memory_fns(ht,hstrdup,NULL,hfree,NULL);
	
	my_instance->htable = ht;
	my_instance->def_op = true;

	for(i = 0;params[i];i++){
		if(strstr(params[i]->name,"rule")){
			parse_rule(strip_tags(params[i]->value),my_instance);
		}
	}
	return (FILTER *)my_instance;
}




/**
 * Associate a new session with this instance of the filter and opens
 * a connection to the server and prepares the exchange and the queue for use.
 *
 *
 * @param instance	The filter instance data
 * @param session	The session itself
 * @return Session specific data for this session
 */
static	void	*
newSession(FILTER *instance, SESSION *session)
{
	FW_SESSION	*my_session;

	if ((my_session = calloc(1, sizeof(FW_SESSION))) == NULL){
		return NULL;
	}
	my_session->session = session;
	return my_session;
}



/**
 * Close a session with the filter, this is the mechanism
 * by which a filter may cleanup data structure etc.
 *
 * @param instance	The filter instance data
 * @param session	The session being closed
 */
static	void 	
closeSession(FILTER *instance, void *session)
{
	//FW_SESSION	*my_session = (FW_SESSION *)session;
}

/**
 * Free the memory associated with the session
 *
 * @param instance	The filter instance
 * @param session	The filter session
 */
static void
freeSession(FILTER *instance, void *session)
{
	FW_SESSION	*my_session = (FW_SESSION *)session;
	free(my_session);
}

/**
 * Set the downstream filter or router to which queries will be
 * passed from this filter.
 *
 * @param instance	The filter instance data
 * @param session	The filter session 
 * @param downstream	The downstream filter or router.
 */
static void
setDownstream(FILTER *instance, void *session, DOWNSTREAM *downstream)
{
	FW_SESSION	*my_session = (FW_SESSION *)session;
	my_session->down = *downstream;
}

/**
 * Generates a dummy error packet for the client.
 * @return The dummy packet or NULL if an error occurred
 */
GWBUF* gen_dummy_error(FW_SESSION* session)
{
	GWBUF* buf;
	char errmsg[512];
	DCB* dcb = session->session->client;
	MYSQL_session* mysql_session = (MYSQL_session*)session->session->data;
	unsigned int errlen, pktlen;

	sprintf(errmsg,"Access denied for user '%s'@'%s' to database '%s' ",
			dcb->user,
			dcb->remote,
			mysql_session->db);	
	errlen = strlen(errmsg);
	pktlen = errlen + 9;
	buf = gwbuf_alloc(13 + errlen);

	if(buf){
		strcpy(buf->start + 7,"#HY000");
		memcpy(buf->start + 13,errmsg,errlen);
		*((unsigned char*)buf->start + 0) = pktlen;
		*((unsigned char*)buf->start + 1) = pktlen >> 8;
		*((unsigned char*)buf->start + 2) = pktlen >> 16;
		*((unsigned char*)buf->start + 3) = 0x01;
		*((unsigned char*)buf->start + 4) = 0xff;
		*((unsigned char*)buf->start + 5) = (unsigned char)1141;
		*((unsigned char*)buf->start + 6) = (unsigned char)(1141 >> 8);
	}
	return buf;
}

/**
 * The routeQuery entry point. This is passed the query buffer
 * to which the filter should be applied. Once processed the
 * query is passed to the downstream component
 * (filter or router) in the filter chain.
 *
 * @param instance	The filter instance data
 * @param session	The filter session
 * @param queue		The query data
 */
static	int	
routeQuery(FILTER *instance, void *session, GWBUF *queue)
{
	FW_SESSION	*my_session = (FW_SESSION *)session;
	FW_INSTANCE	*my_instance = (FW_INSTANCE *)instance;
	IPRANGE* ipranges = my_instance->networks;
	bool accept = false, match = false;
	char *where;
	uint32_t ip;
	ruletype_t rtype = RT_UNDEFINED;
	skygw_query_op_t queryop;
    DCB* dcb = my_session->session->client;
	ip = strtoip(dcb->remote);
	
	rtype = (ruletype_t)hashtable_fetch(my_instance->htable, dcb->user);
	if(rtype == RT_USER){
		match = true;
		accept = my_instance->whitelist_users;
		skygw_log_write(LOGFILE_TRACE, "Firewall: %s@%s was %s.",
						dcb->user, dcb->remote,
						(my_instance->whitelist_users ?
						 "allowed":"blocked"));
	}

	if(!match){
		while(ipranges){
			if(ip >= ipranges->ip && ip <= ipranges->ip + ipranges->mask){
				match = true;
				accept = my_instance->whitelist_networks;
				skygw_log_write(LOGFILE_TRACE, "Firewall: %s@%s was %s.",
								dcb->user,dcb->remote,(my_instance->whitelist_networks ? "allowed":"blocked"));
				break;
			}
			ipranges = ipranges->next;
		}
	}

	
	if(modutil_is_SQL(queue)){

		if(!query_is_parsed(queue)){
			parse_query(queue);
		}

		if(skygw_is_real_query(queue)){

			match = false;		
			
			if(!skygw_query_has_clause(queue)){

				queryop =  query_classifier_get_operation(queue);

				if(my_instance->require_where[ALL] ||
				   (my_instance->require_where[SELECT] && queryop == QUERY_OP_SELECT) || 
				   (my_instance->require_where[UPDATE] && queryop == QUERY_OP_UPDATE) || 
				   (my_instance->require_where[INSERT] && queryop == QUERY_OP_INSERT) || 
				   (my_instance->require_where[DELETE] && queryop == QUERY_OP_DELETE)){
					match = true;
					accept = false;
					skygw_log_write(LOGFILE_TRACE, "Firewall: query does not have a where clause or a having clause, blocking it: %.*s",GWBUF_LENGTH(queue),(char*)(queue->start + 5));
				}
			}
			
			if(!match){

				where = skygw_get_affected_fields(queue);

				if(my_instance->block_wildcard && 
				   where && strchr(where,'*') != NULL)
					{
						match = true;
						accept = false;
						skygw_log_write(LOGFILE_TRACE, "Firewall: query contains wildcard, blocking it: %.*s",GWBUF_LENGTH(queue),(char*)(queue->start + 5));
					}
				else if(where)
					{
						char* tok = strtok(where," ");

						while(tok){
							rtype = (ruletype_t)hashtable_fetch(my_instance->htable, tok);
							if(rtype == RT_COLUMN){
								match = true;
								accept = false;
								skygw_log_write(LOGFILE_TRACE, "Firewall: query contains a forbidden column %s, blocking it: %.*s",tok,GWBUF_LENGTH(queue),(char*)(queue->start + 5));
							}
							tok = strtok(NULL," ");
						}

					}
				free(where);
			}

		}
	}

	/**If no rules matched, do the default operation. (allow by default)*/
	if(!match){
		accept = my_instance->def_op;
	}
	
	if(accept){

		return my_session->down.routeQuery(my_session->down.instance,
										   my_session->down.session, queue);
	}else{
	    
		gwbuf_free(queue);
	    GWBUF* forward = gen_dummy_error(my_session);
		dcb->func.write(dcb,forward);
		//gwbuf_free(forward);
		return 0;
	}
}

/**
 * Diagnostics routine
 *
 * Prints the connection details and the names of the exchange,
 * queue and the routing key.
 *
 * @param	instance	The filter instance
 * @param	fsession	Filter session, may be NULL
 * @param	dcb		The DCB for diagnostic output
 */
static	void
diagnostic(FILTER *instance, void *fsession, DCB *dcb)
{
	FW_INSTANCE	*my_instance = (FW_INSTANCE *)instance;

	if (my_instance)
		{
			dcb_printf(dcb, "\t\tFirewall Filter\n");
		}
}
	