#include "xmlfast.h"

struct entityref entities; // tree of entity candidates. generated by calculate

#define mkents(er,N) \
do { \
	er->more = safemalloc( sizeof(struct entityref) * N ); \
	memset(er->more, 0, sizeof(struct entityref) * N); \
	er->children = N; \
} while (0)

void calculate(char *prefix, unsigned char offset, entity *strings, struct entityref *ents);
void calculate(char *prefix, unsigned char offset, entity *strings, struct entityref *ents) {
	unsigned char counts[256];
	unsigned char i,x,len;
	unsigned int total = 0;
	char pref[MAX_ENTITY_LENGTH];
	struct entityref *curent;
	entity *keep;
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

char *parse_attrs(char *p, void *ctx, xml_callbacks * cb) {
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
								if (cb->attrname) cb->attrname( ctx, at, end - at );
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
									if(cb->attrval) cb->attrval( ctx, at, p - at );
									p = eat_wsp(p+1);
									break;
								}
							case '&':
								if (wait) {
									//printf("Got entity begin (%s)\n",buffer);
									end = p;
									if( entity = parse_entity(&p) ) {
										if(cb->attrvalpart) {
											if (end > at) cb->attrvalpart( ctx, at, end - at );
											cb->attrvalpart( ctx, entity->entity, entity->length );
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

void parse (char * xml, void * ctx, xml_callbacks * cb) {
	if (!entities.more) { init_entities(); }
	char *p, *at, *end, *search, buffer[BUFFER], *buf, wait, loop, backup;
	memset(&buffer,0,BUFFER);
	unsigned int state, len;
	unsigned int mainstate;
	unsigned char textstate;
	p = xml;
	
	xml_node *chain, *root, *seek, *reverse;
	int chain_depth = 64, curr_depth = 0;
	root = chain = safemalloc( sizeof(xml_node) * chain_depth );
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
#define TEXT_DATA      9
#define TEXT_INITWSP  10
#define TEXT_WSP      11

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
									cb->comment( ctx, p, search - p );
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
									cb->cdata( ctx, p, search - p );
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
							//printf("PI: '%s'\n",buffer);
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
								if(cb->tagclose) cb->tagclose(ctx, at, len);
								curr_depth--;
								chain--;
								//print_chain(root, curr_depth);
							} else {
								if(len+1 > BUFFER) {
									snprintf(buffer,BUFFER,"%s",at);
								} else {
									snprintf(buffer,len+1,"%s",at);
								}
								//printf("NODE CLOSE '%s' (unbalanced)\n",buffer);
								reverse = seek = chain;
								while( seek > root ) {
									seek--;
									if (strncmp(seek->name, at, len) == 0) {
										printf("Found early opened node %s\n",seek->name);
										print_chain(root, curr_depth);
										while(chain >= seek) {
											//printf("Auto close %s\n",chain->name);
											if(cb->tagclose) cb->tagclose(ctx, chain->name, chain->len);
											safefree(chain->name);
											chain--;
											curr_depth--;
											//print_chain(root, curr_depth);
										}
										/*
										//optional feature: auto-opening tags
										for (seek = chain+2; seek <= reverse; seek++) {
											//printf("Auto open %s\n",seek->name);
											chain++;
											curr_depth++;
											*chain = *(chain+1);
											if(cb->tagopen) cb->tagopen(ctx, chain->name, chain->len);
											//print_chain(root, curr_depth);
										}
										*/
										seek = 0;
										break;
									}
								}
								if (seek) {
									if (cb->warn)
										cb->warn("Found no open node until root for '%s'. Ignored",buffer);
									//print_chain(root, curr_depth);
								} else {
									// TODO
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
									chain->name = safemalloc( chain->len + 1 );
									strncpy(chain->name, at, len);
									if (cb->tagopen) cb->tagopen( ctx, at, len );
									//print_chain(root, curr_depth);
									
									break;
								case 1:
									//printf("reading attrs for %s, next='%c'\n",chain->name,*p);
									if (search = parse_attrs(p,ctx,cb)) {
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
												if (cb->tagclose) cb->tagclose( ctx, at, len );
												safefree(chain->name);
												chain--;
												curr_depth--;
												//print_chain(root, curr_depth);
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
			default:
				mainstate = TEXT_READ;
				at = p;
				textstate = TEXT_INITWSP;
				char *lastwsp;
				while (1) {
					switch(*p) {
						case 0  :
						case '<':
							if (p > at) {
								if (textstate == TEXT_WSP) {
									//printf("Got trailing whitespace chardata=%d wspdata=%d\n", lastwsp - at, p - lastwsp);
									if(cb->text) cb->text(ctx, at, lastwsp - at );
									if(cb->wsp) cb->wsp(ctx, lastwsp, p - lastwsp ); // whitespace
								}
								else if (textstate == TEXT_INITWSP) {
									//printf("Got only whitespace\n");
									if(cb->wsp) cb->wsp(ctx, at, p - at ); // whitespace
								}
								else
								{
									if(cb->text) cb->text(ctx, at, p - at );
								}
								mainstate = CONTENT_WAIT;
							}
							if (*p == 0) goto eod;
							goto next;
						case '&':
							end = p;
							if( entity = parse_entity(&p) ) {
								if(cb->text) {
									if (end > at) cb->text(ctx, at, end - at);
									cb->text(ctx, entity->entity, entity->length);
								}
								at = p;
								break;
							}
						case_wsp :
							if (textstate == TEXT_DATA) {
								lastwsp = p;
							}
							if (textstate != TEXT_INITWSP) textstate = TEXT_WSP;
							p++;
							break;
						default:
							if ( textstate == TEXT_INITWSP && p > at ) {
								//printf("Got initial whitespace\n");
								if (cb->wsp) cb->wsp(ctx, at, p - at );
								at = p;
							}
							textstate = TEXT_DATA;
							p++;
					}
				}
				textstate = TEXT_INITWSP;
				break;
		}
	}
	printf("parse done\n");
	safefree(root);
	return;
	
	eod:
		//printf("End of document, mainstate=%d\n",mainstate);
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
					//printf("END ok\n");
				} else {
					printf("Document aborted\n");
					//print_chain(chain,curr_depth);
				}
				break;
			default:
				printf("Bad mainstate %d at the end of document\n",mainstate);
		}
	
	fault:
	safefree(root);
	return;
}
