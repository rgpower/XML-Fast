#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
//#include "xmlfast.h"

#define PROCESSING_INSTRUCTION 0x0001
#define TEXT_NODE              0x0002

#define case_wsp   \
		case 0x9  :\
		case 0xa  :\
		case 0xd  :\
		case 0x20

struct entityref {
	char c;
	char *entity;
	unsigned int length;
	unsigned children;
	struct entityref *more;
};

#define mkents(er,N) \
do { \
	er->more = malloc( sizeof(struct entityref) * N ); \
	memset(er->more, 0, sizeof(struct entityref) * N); \
	er->children = N; \
} while (0)

static struct entityref entities;

//Max string lengh for entity name, with trailing '\0'
#define MAX_ENTITY_LENGTH 5
#define MAX_ENTITY_VAULE_LENGTH 1

struct entity {
	char * str;
	char * val;
};

#define ENTITY_COUNT 5

static struct entity entitydef[] = {
	 { "lt",     "<"  }
	,{ "gt",     ">"  }
	,{ "amp",    "&"  }
	,{ "apos",   "'"  }
	,{ "quot",   "\"" }
};

typedef struct {
	char *name;
	char *value;
} xml_attr;

typedef struct {
	char *name;
	unsigned int len;
	char closed;
} xml_node;

static void calculate(char *prefix, unsigned char offset, struct entity *strings, struct entityref *ents);
static void calculate(char *prefix, unsigned char offset, struct entity *strings, struct entityref *ents) {
	unsigned char counts[256];
	unsigned char i,x,len;
	unsigned int total = 0;
	char pref[MAX_ENTITY_LENGTH];
	struct entityref *curent;
	struct entity *keep;
	memset(&counts,0,256);
	//printf("Counting, prefix='%s'\n",prefix);
	for (i = 0; i < ENTITY_COUNT; i++) {
		len = strlen(strings[i].str);
		if ( len > offset && strncmp( strings[i].str, prefix, offset ) == 0) {
			counts[ strings[i].str[offset] ]++;
		} else
		if ( len == offset ) {
			
		}
	}
	for (i = 0; i < 255; i++) {
		if (counts[i]) {
			total++;
			//printf("have %d children for '%c'\n",counts[i],i);
		}
	}
	strncpy(pref,prefix,offset+1);
	if (total == 0) {
		keep = 0;
		for (x = 0; x < ENTITY_COUNT; x++) {
			//printf("compare '%s'<=>'%s' (max %d)\n",strings[x].str, prefix, offset);
			if ( strncmp( strings[x].str, prefix, offset ) == 0) {
				keep = &(strings[x]);
				break;
			}
		}
		if (keep) {
			//printf("endpoint for c='%c': %s -> %s\n", ents->c ,prefix, keep->val);
			ents->entity = keep->val;
			ents->length = strlen(ents->entity);
		} else {
			printf("fuck, not found keep");
		}
		return;
	}
	//printf("have totally %d strings, next prefix='%s'\n",total,pref);
	pref[offset+1] = '\0';
	mkents(ents,total);
	curent = ents->more;
	for (i = 0; i < 255; i++) {
		if (counts[i]) {
			curent->c = i;
			pref[offset] = i;
			calculate(pref,offset+1,strings,curent);
			++curent;
		}
	}
	return;
}

static void init_entities() {
	calculate("",0,entitydef,&entities);
	return;
}

static char * eat_wsp(char *p) {
	while (1) {
		switch (*p) {
			case 0: return p;
			case_wsp :
				break;
			default:
				return p;
		}
		p++;
	}
}

static char * eatback_wsp(char *p) {
	while (1) {
		switch (*p) {
			case 0: return p;
			case_wsp :
				break;
			default:
				return p;
		}
		p--;
	}
}

static struct entityref *parse_entity (char **pp) {
	char *p = *pp;
	struct entityref *cur_ent, *last_ent;
	char *at;
	at = p;
	unsigned int i;
	cur_ent = &entities;
	next_ent:
		if (*p == 0) return 0;
		p++;
		if (*p == 0) return 0;
		if (*p == ';') {
			if (cur_ent && cur_ent->entity) {
				//printf("Entity terminated. result='%s', buffer='%s'\n",cur_ent->entity,buf-1);
				p++;
				goto ret;
			} else {
				//printf("Entity termination while not have cur\n");
				goto no_ent;
			}
		}
		for (i=0; i < cur_ent->children; i++) {
			//printf("\tcheck '%c' against '%c'\n", *p, cur_ent->more[i].c);
			if (cur_ent->more[i].c == *p) {
				cur_ent = &cur_ent->more[i];
				//printf("found ent ref '%c' (%s)\n",cur_ent->c, cur_ent->entity ? (cur_ent->entity) : " ");
				goto next_ent;
			}
		}
		if (cur_ent && cur_ent->entity) {
			//printf("Not found nested entity ref, but have good cur '%s'\n", cur_ent->entity);
			//p--;
			goto ret;
		} else {
			//printf("Not found entity ref\n");
		}
	no_ent:
	p = at;
	*pp = p;
	return 0;
	
	ret:
	*pp = p;
	
	return cur_ent;
}

static void print_chain (xml_node *chain, int depth) {
	int i;
	xml_node * node;
	printf(":>> ");
	for (i=0; i < depth; i++) {
		node = &chain[i];
		printf("%s",node->name);
		if (i < depth-1 )printf(" > ");
	}
	printf("\n");
}

typedef struct {
	void (*comment)(char *, unsigned int);
	void (*cdata)(char *, unsigned int);
	void (*text)(char *, unsigned int);
	void (*tagopen)(char *, unsigned int); //third is openstate. 0 - tag empty, 1 - tag have no attrs, 2 - tag may have attrs
	void (*attrname)(char *, unsigned int);
	void (*attrvalpart)(char *, unsigned int);
	void (*attrval)(char *, unsigned int);
	void (*tagclose)(char *, unsigned int);
} xml_callbacks;

#define BUFFER 4096

#define xml_error(x) do { printf("Error at char %d (%c): %s\n", p-xml, *p, x);goto fault; } while (0)

char *parse_attrs(char *p, xml_callbacks * cb) {
		char state = 0;
		/*
		 * state=0 - default, waiting for attr name or /?>
		 * state=1 - reading attr name
		 * state=2 - reading attr value
		 */
		char wait = 0;
		char loop = 1;
		char *at,*end;
		struct entityref *entity;
		p = eat_wsp(p);
		while(loop) {
			switch(state) {
				case 0: // waiting for attr name
					//printf("Want attr name, char='%c'\n",*p);
					while(state == 0) {
						switch(*p) {
							case 0   : printf("Document aborted\n");return 0;
							case_wsp : p = eat_wsp(p); break;
							case '>' :
							case '?' :
							case '/' : return p;
							default  : state = 1;
						}
					}
					break;
				case 1: //reading attr name
					at = p;
					end = 0;
					//printf("Want = (%c)\n",*p);
					while(state == 1) {
						switch(*p) {
							case 0   : printf("Document aborted\n");return 0;
							case_wsp :
								end = p;
								p = eat_wsp(p);
								if (*p != '=') {
									printf("No = after whitespace while reading attr name\n");
									return 0;
								}
							case '=':
								if (!end) end = p;
								if (cb->attrname) cb->attrname( at, end - at );
								p = eat_wsp(p + 1);
								state = 2;
								break;
							default: p++;
						}
					}
					break;
				case 2:
					wait = 0;
					//printf("Want quote (%c)\n",*p);
					while(state == 2) {
						switch(*p) {
							case 0   : printf("Document aborted\n");return 0;
							case '\'':
							case '"':
								if (!wait) { // got open quote
									//printf("\tgot open quote <%c>\n",*p);
									wait = *p;
									p++;
									at = p;
									break;
								} else
								if (*p == wait) {  // got close quote
									//printf("\tgot close quote <%c>\n",*p);
									state = 0;
									if(cb->attrval) cb->attrval( at, p - at );
									p = eat_wsp(p+1);
									break;
								}
							case '&':
								if (wait) {
									//printf("Got entity begin (%s)\n",buffer);
									end = p;
									if( entity = parse_entity(&p) ) {
										if(cb->attrvalpart) {
											if (end > at) cb->attrvalpart( at, end - at );
											cb->attrvalpart( entity->entity, entity->length );
										}
										at = p;
										break;
									}
								} else {
									printf("Not waiting for & in state 2\n");
									return 0;
								}
							default: p++;
						}
					}
					break;
				default:
					printf("default, state=%d, char='%c'\n",state, *p);
					return 0;
			}
		}
		return p;
}

void parse (char * xml, xml_callbacks * cb) {
	if (!entities.more) { init_entities(); }
	char *p, *at, *end, *search, buffer[BUFFER], *buf, wait, loop, backup;
	memset(&buffer,0,BUFFER);
	unsigned int state, len;
	unsigned int mainstate;
	unsigned char textstate;
	p = xml;
	
	xml_node *chain, *root, *seek;
	int chain_depth = 64, curr_depth = 0;
	root = chain = malloc( sizeof(xml_node) * chain_depth );
	unsigned char node_closed;
	struct entityref *entity;

#define DOCUMENT_START 0
#define LT_OPEN        1
#define COMMENT_OPEN   2
#define CDATA_OPEN     3
#define PI             4
#define CONTENT_WAIT   5
#define TAG_OPEN       6
#define TAG_CLOSE      7
#define TEXT_READ      8
	mainstate = DOCUMENT_START;
	next:
	while (1) {
		switch(*p) {
			case 0: goto eod;
			case '<':
				mainstate = LT_OPEN;
				p++;
				switch (*p) {
					case 0: goto eod;
					case '!':
						p++;
						if(*p == 0) goto eod;
						if ( strncmp( p, "--", 2 ) == 0 ) {
							mainstate = COMMENT_OPEN;
							p+=2;
							search = strstr(p,"-->");
							if (search) {
								if (cb->comment) {
									cb->comment( p, search - p );
								} else {
									printf("No comment callback, ignored\n");
								}
								p = search + 3;
							} else xml_error("Comment node not terminated");
							mainstate = CONTENT_WAIT;
							goto next;
						} else
						if ( strncmp( p, "[CDATA[", 7 ) == 0) {
							mainstate = CDATA_OPEN;
							p+=7;
							search = strstr(p,"]]>");
							if (search) {
								if (cb->cdata) {
									cb->cdata( p, search - p );
								} else {
									printf("No cdata callback, ignored\n");
								}
								p = search + 3;
							} else xml_error("Cdata node not terminated");
							mainstate = CONTENT_WAIT;
							goto next;
						} else
						{
							printf("fuckup after <!: %c\n",*p);
							goto fault;
						}
						break;
					case '?':
						mainstate = PI;
						search = strstr(p,"?>");
						if (search) {
							//printf("found pi node length = %d\n", search - p);
							snprintf( buffer, search - p + 1 - 1, "%s", p+1 );
							printf("PI: '%s'\n",buffer);
							p = search + 2;
							mainstate = CONTENT_WAIT;
							goto next;
						} else {
							printf ("PI node not terminated\n");
							goto fault;
						}
					case '/': // </node>
						mainstate = TAG_CLOSE;
						p++;
						at = p;
						search = index(p,'>');
						if (search) {
							p = search + 1;
							//printf("search = '%c'\n",*search);
							search = eatback_wsp(search-1)+1;
							//printf("search = '%c'\n",*search);
							len = search - at;
							//printf("len = %d\n",len);
							if (strncmp(chain->name, at, len) == 0) {
								if (curr_depth == 0) {
									printf("Need to close upper than root\n");
									goto fault;
								}
								if(cb->tagclose) cb->tagclose(at, len);
								curr_depth--;
								chain--;
								print_chain(root, curr_depth);
							} else {
								if(len+1 > BUFFER) {
									snprintf(buffer,BUFFER,"%s",at);
								} else {
									snprintf(buffer,len+1,"%s",at);
								}
								//printf("NODE CLOSE '%s' (unbalanced)\n",buffer);
								seek = chain;
								while( seek > root ) {
									seek--;
									if (strncmp(seek->name, at, len) == 0) {
										//printf("Found early opened node %s\n",seek->name);
										while(chain >= seek) {
											//printf("Auto close %s\n",chain->name);
											if(cb->tagclose) cb->tagclose(chain->name, chain->len);
											chain--;
											curr_depth--;
											print_chain(root, curr_depth);
										}
										seek = 0;
									}
								}
								if (seek) {
									printf("Found no open node until root for '%s'. open and close\n",buffer);
									print_chain(root, curr_depth);
								}
							}
							mainstate = CONTENT_WAIT;
							goto next;
						} else {
							printf ("close tag not terminated");
							goto fault;
						}
					default: //<node...>
						state = 0;
						mainstate = TAG_OPEN;
						while(state < 3) {
							switch(state) {
								case 0:
									at = p;
									while(state == 0) {
										switch(*p) {
											case 0: goto eod;
											case_wsp :
												if (p > at) {
													state = 1;
													break;
												} else {
													printf("Bad node opening\n");
													goto fault;
												}
											case '/':
											case '>':
												if (p > at) {
													state = 2;
													break;
												} else {
													printf("Bad node opening\n");
													goto fault;
												}
											default: p++;
										}
									}
									if (curr_depth++ != 0) chain++;
									len = chain->len = p - at;
									chain->name = malloc( chain->len + 1 );
									strncpy(chain->name, at, len);
									if (cb->tagopen) cb->tagopen( at, len );
									print_chain(root, curr_depth);
									
									break;
								case 1:
									//printf("reading attrs for %s, next='%c'\n",chain->name,*p);
									if (search = parse_attrs(p,cb)) {
										p = search;
										state = 2;
									} else {
										goto fault;
									}
								case 2:
									while(state == 2) {
										//printf("state=2, char='%c'\n",*p);
										switch(*p) {
											case 0: goto eod;
											case_wsp : p = eat_wsp(p);
											case '/' :
												if (cb->tagclose) cb->tagclose( at, len );
												chain--;
												curr_depth--;
												print_chain(root, curr_depth);
												p = eat_wsp(p+1);
											case '>' : state = 3; p++; break;
											default  :
												printf("bad char '%c' at the end of tag\n",*p);
												goto fault;
										}
									}
									mainstate = CONTENT_WAIT;
									goto next;
							}
						}
				}
				break;
			case_wsp :
				printf("skip \\%03o\n",*p);
				p++;
				break;
			default:
				mainstate = TEXT_READ;
				at = p;
				while (1) {
					switch(*p) {
						case 0  :
							if (p > at && cb->text) {
								cb->text(at, p - at );
								mainstate = CONTENT_WAIT;
							}
							goto eod;
						case '&':
							buf = buffer;
							end = p;
							if( entity = parse_entity(&p) ) {
								if(cb->text) {
									if (end > at) cb->text(at, end - at);
									cb->text(entity->entity, entity->length);
								}
								at = p;
								break;
							}
						case '<':
							if(cb->text) cb->text(at, p - at );
							mainstate = CONTENT_WAIT;
							goto next;
						default: p++;
					}
				}
				break;
		}
	}
	printf("parse done\n");
	return;
	
	eod:
		printf("End of document, mainstate=%d\n",mainstate);
		switch(mainstate) {
			case DOCUMENT_START:
				printf("Empty document\n");
				return;
			case LT_OPEN:
			case COMMENT_OPEN:
			case CDATA_OPEN:
			case PI:
			case TAG_OPEN:
			case TAG_CLOSE:
				printf("Bad document end\n");
				break;
			case TEXT_READ:
				printf("Need to call text cb at the end of document\n");
				break;
			case CONTENT_WAIT:
				if (curr_depth == 0) {
					printf("END ok\n");
				} else {
					printf("Document aborted\n");
					print_chain(chain,curr_depth);
				}
				break;
			default:
				printf("Bad mainstate %d at the end of document\n",mainstate);
		}
		return;
	
	fault:
	
	return;
}

void on_comment(char * data,unsigned int length) {
	char * buffer;
	buffer = malloc(length+1);
	strncpy(buffer, data, length);
	*(buffer + length) = '\0';
	printf("CB: <!-- '%s' -->\n",buffer);
	free(buffer);
}

void on_cdata(char * data,unsigned int length) {
	char * buffer;
	buffer = malloc(length+1);
	strncpy(buffer, data, length);
	*(buffer + length) = '\0';
	printf("CB: CDATA[ '%s' ]\n",buffer);
	free(buffer);
}

void on_tag_open(char * data, unsigned int length) {
	char * buffer;
	buffer = malloc(length+1);
	strncpy(buffer, data, length);
	*(buffer + length) = '\0';
	printf("CB: +<%s>\n",buffer);
	free(buffer);
}

void on_attr_name(char * data,unsigned int length) {
	char * buffer;
	buffer = malloc(length+1);
	strncpy(buffer, data, length);
	*(buffer + length) = '\0';
	printf("CB: ATTR '%s'=",buffer);
	free(buffer);
}

void on_attr_val_part(char * data,unsigned int length) {
	char * buffer;
	buffer = malloc(length+1);
	strncpy(buffer, data, length);
	*(buffer + length) = '\0';
	printf("'%s'",buffer);
	free(buffer);
}

void on_attr_val(char * data,unsigned int length) {
	char * buffer;
	buffer = malloc(length+1);
	strncpy(buffer, data, length);
	*(buffer + length) = '\0';
	printf("'%s'\n",buffer);
	free(buffer);
}

void on_text(char * data,unsigned int length) {
	char * buffer;
	buffer = malloc(length+1);
	strncpy(buffer, data, length);
	*(buffer + length) = '\0';
	printf("CB: TEXT='%s'\n",buffer);
	free(buffer);
}

void on_tag_close(char * data, unsigned int length) {
	char * buffer;
	buffer = malloc(length+1);
	strncpy(buffer, data, length);
	*(buffer + length) = '\0';
	printf("CB: -</%s>\n",buffer);
	free(buffer);
}

int main () {
	//init_entities();
	//return 0;
	printf("ok\n");
	return;
	char *xml;
	xml_callbacks cbs;
	memset(&cbs,0,sizeof(xml_callbacks));
	cbs.comment      = on_comment;
	cbs.cdata        = on_cdata;
	cbs.tagopen      = on_tag_open;
	cbs.tagclose     = on_tag_close;
	cbs.attrname     = on_attr_name;
	cbs.attrvalpart  = on_attr_val_part;
	cbs.attrval      = on_attr_val;
	cbs.text         = on_text;
	xml =	"<?xml version=\"1.0\"?>"
			"<test>ok"
				"<test/> "
				"<test />\n"
				"<test></test>\t"
				"<!-- comment -->"
				"<![CDATA[d]]>"
				"<more abc = \"x>\" c='qwe\"qwe' d=\"qwe'qwe\" abcd=\"1&lt;&amp;&apos;&quot;&gt11&;22\" />"
				"</test>\n";
	//parse(xml,&cbs);
	xml = "<?xml version=\"1.0\"?><test1><test2><test3>ok<i>test<b>test</i>test</b></test3></test2></test1>";
	//parse(xml,&cbs);
	xml = "<?xml version=\"1.0\"?>"
			"<test1 a='1&amp;234-5678-9012-3456-7890'>"
				"<testi x='y' />"
				"<testz x='y' / >"
				"<test2>"
					"<test3>"
						"<!-- comment -->"
						"<![CDATA[cda]]>"
						"ok1&amp;ok2&gttest"
						"<i>test<b>test</i>test</b>"
					"</test3>"
				"</test2>"
			"</test1 > ";
	parse(xml,&cbs);
/*
	xml = "";
	parse(xml,&cbs);
	xml = "<?xml version=\"1.0\"";
	parse(xml,&cbs);
	xml = "<?xml version=\"1.0\"?>";
	parse(xml,&cbs);
	xml = "<?xml version=\"1.0\"?><test";
	parse(xml,&cbs);
	xml = "<?xml version=\"1.0\"?><test attr";
	parse(xml,&cbs);
	xml = "<?xml version=\"1.0\"?><test attr=";
	parse(xml,&cbs);
	xml = "<?xml version=\"1.0\"?><test attr='";
	parse(xml,&cbs);
	xml = "<?xml version=\"1.0\"?><test attr='1'";
	parse(xml,&cbs);
	xml = "<?xml version=\"1.0\"?><test attr='1'>";
	parse(xml,&cbs);
	xml = "<?xml version=\"1.0\"?><test attr='&g";
	parse(xml,&cbs);
	xml = "<test></test>";
	parse(xml,&cbs);
	xml = "<!";
	parse(xml,&cbs);
	xml = "<!--";
	parse(xml,&cbs);
	xml = "<![CDATA[";
	parse(xml,&cbs);
*/
	return 0;
}

