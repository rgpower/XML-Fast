#include "xmlfast.h"
#include "uni.h"

struct entityref entities; // tree of entity candidates. generated by calculate

#define mkents(er,N) \
do { \
	er->more = safemalloc( sizeof(struct entityref) * N ); \
	memset(er->more, 0, sizeof(struct entityref) * N); \
	er->children = N; \
} while (0)

 // case 0xa  : line_number++;

#define case_wsp   \
		case 0xa  : context->line_number++; \
		case 0x9  :\
		case 0xd  :\
		case 0x20

#define XML_DEBUG 0

#if XML_DEBUG
#define WHERESTR    " at %s line %d.\n"
#define WHEREARG    __FILE__, __LINE__
#define debug(...)   do{ fprintf(stderr, __VA_ARGS__); fprintf(stderr, WHERESTR, WHEREARG); } while(0)
#else
#define debug(...)
#endif

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
#define DOCUMENT_ABORTED 12

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

static char * eat_wsp(parser_state * context, char *p) {
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

static char * eatback_wsp(parser_state * context, char *p) {
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

static char utf8(wchar_t chr,char *xx) {
				if (chr < 0x0080 ) {
					xx[0] = (char)chr;
					xx[1] = 0;
					return 1;
				}
				else if (chr < 0x0800) {
					xx[0] = (char) ( 0xc0 | (chr >> 6) );
					xx[1] = (char) ( 0x80 | (chr & 0x3f) );
					xx[2] = 0;
					return 2;
				}
				else {
					xx[0] = (char) ( 0xe0 | ( chr >> 12 ) );
					xx[1] = (char) ( 0x80 | (( chr >> 6 ) & 0x3f ) );
					xx[2] = (char) ( 0x80 | (chr & 0x3f ) );
					xx[3] = 0;
					return 3;
				}
}

char *parse_entity (parser_state * context, char *p, void (*cb)(void *,char *, unsigned int)) {
	//return p+1;
	struct entityref *cur_ent, *last_ent;
	char *at;
	at = p;
	unsigned int i;
	if (*(p+1) == '#') {
		p+=2;
			wchar_t chr = 0;
			if (*p == 'x') {
				p++;
				while(1) {
					if (*p >= '0' && *p <= '9') {
						chr *= 16;
						chr += (*p++ - '0');
					}
					else
					if (*p >= 'a' && *p <= 'f') {
						chr *= 16;
						chr += (*p++ - 'a' + 10);
					}
					else
					if (*p >= 'A' && *p <= 'F') {
						chr *= 16;
						chr += (*p++ - 'A' + 10);
					}
					else
						break;
				}
				//p+= 
				//return sym;
			}
			else {
				while(*p >= '0' && *p <= '9') {
					chr *= 10;
					chr += (*p++ - '0');
				}
			}
			if (chr > 0) {
				if ( *p == ';' ) p++;
				char sym[4];
				int len;
				len = utf8(chr,sym);
				//printf("Got result = %d (%s) (%s)\n",chr,utf[chr],sym);
				if (cb) cb(context->ctx, sym, len);
			} else {
				//printf("Bad entity\n");
				if (cb) cb(context->ctx, at, p - at);
			}
			return p;
	}
	cur_ent = &entities;
	next_ent:
		if (*p == 0) return 0;
		p++;
		//printf("parse entity char='%c'\n",*p);
		if (*p == '#') {
			p++;
		}
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
	if (p == at) p++;
	if (cb && p > at)
		cb(context->ctx, at, p-at);
	//p = at;
	return p;
	
	ret:
	if (cb)
		cb(context->ctx, cur_ent->entity, cur_ent->length);
	
	return p;
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

char *parse_attrs(char *p, parser_state * context) {
	void * ctx = context->ctx;
	xml_callbacks * cb = &context->cb;
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
		p = eat_wsp(context, p);
		while(loop) {
			switch(state) {
				case 0: // waiting for attr name
					//printf("Want attr name, char='%c'\n",*p);
					while(state == 0) {
						switch(*p) {
							case 0   : printf("Document aborted\n");return 0;
							case_wsp : p = eat_wsp(context, p); break;
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
								p = eat_wsp(context, p);
								if (*p != '=') {
									printf("No = after whitespace while reading attr name\n");
									return 0;
								}
							case '=':
								if (!end) end = p;
								if (cb->attrname) cb->attrname( ctx, at, end - at );
								p = eat_wsp(context, p + 1);
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
									p = eat_wsp(context, p+1);
									break;
								}
							case '&':
								if (wait) {
									//printf("Got entity begin (%s)\n",buffer);
									if (p > at && cb->attrvalpart) cb->attrvalpart( ctx, at, p - at );
									if( p = parse_entity(context, p, cb->attrvalpart) ) {
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

/* parser callbacks sample

void some_callback(char **pp) {
	printf("Callback called\n");
}

	void (*callbacks[256])(char **);
	callbacks['/'] = some_callback;
	callbacks['/'](&xml);

*/

char * parse_exclaim (parser_state * context, char * p) {
	
}

void parse (char * xml, parser_state * context) {
	void * ctx = context->ctx;
	xml_callbacks * cb = &context->cb;
	context->line_number = 1;
	if (!entities.more) { init_entities(); }
	char *p, *at, *end, *search, buffer[BUFFER], *buf, wait, loop, backup;
	memset(&buffer,0,BUFFER);
	unsigned int state, len;
	unsigned char textstate;
	p = xml;
	
	xml_node *chain, *root, *seek, *reverse;
	int chain_depth = 64, curr_depth = 0;
	root = chain = safemalloc( sizeof(xml_node) * chain_depth );
	unsigned char node_closed;
	struct entityref *entity;
	unsigned int line;


	context->state = DOCUMENT_START;
	next:
	while (1) {
		switch(*p) {
			case 0: goto eod;
			case '<':
				context->state = LT_OPEN;
				p++;
				switch (*p) {
					case 0: goto eod;
					case '!':
						p++;
						if(*p == 0) goto eod;
						if ( strncmp( p, "--", 2 ) == 0 ) {
							context->state = COMMENT_OPEN;
							p+=2;
							search = strstr(p,"-->");
							if (search) {
								if (cb->comment) {
									cb->comment( ctx, p, search - p );
								}
								p = search + 3;
							} else xml_error("Comment node not terminated");
							context->state = CONTENT_WAIT;
							goto next;
						} else
						if ( strncmp( p, "[CDATA[", 7 ) == 0) {
							context->state = CDATA_OPEN;
							p+=7;
							search = strstr(p,"]]>");
							if (search) {
								if (cb->cdata) {
									cb->cdata( ctx, p, search - p, 0 );
								}
								p = search + 3;
							} else xml_error("Cdata node not terminated");
							context->state = CONTENT_WAIT;
							goto next;
						} else
						{
							printf("fuckup after <!: %c\n",*p);
							goto fault;
						}
						break;
					case '?':
						context->state = PI;
						state = 0;
						p++;
						at = p;
						while(state == 0) {
							switch(*p) {
								case 0   : context->state = DOCUMENT_ABORTED; goto eod;
								case_wsp :
									if (p > at) {
										debug("PI: want attrs");
										end = p;
										state = 1;
										break;
									} else {
										printf("CB> Bad pi opening\n");
										goto fault;
									}
								case '?':
									end = p;
									p++;
									if (*p == '>') {
										p++;
										state = 2;
									} else {
										printf("CB> PI not closed: %c\n",*p);
										goto fault;
									}
									break;
								default: p++;
							}
						}
						if (cb->piopen) cb->piopen( context->ctx, at, end - at );
						if (state == 1) {
							if (p = parse_attrs(p,context)) {
								//printf("Got attrs\n");
							} else {
								goto fault;
							}
							state = 2;
						}
						debug("CB> Got pi name state=%d next='%c'\n",state,*p);
						if (*p == '?' && *(p+1) == '>') {
							debug("PI correctly closed\n");
							p+=2;
							if (cb->piclose) cb->piclose( context->ctx, at, end - at );
							goto next;;
						} else {
							printf("CB> PI not closed\n");
							goto fault;
						}
					case '/': // </node>
						context->state = TAG_CLOSE;
						p++;
						at = p;
						search = index(p,'>');
						if (search) {
							p = search + 1;
							//printf("search = '%c'\n",*search);
							search = eatback_wsp(context, search-1)+1;
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
										cb->warn("Found no open node until root for '%s' at line %d, char %d. Ignored",buffer, context->line_number, p - xml);
									//print_chain(root, curr_depth);
								} else {
									// TODO
								}
							}
							context->state = CONTENT_WAIT;
							goto next;
						} else {
							printf ("close tag not terminated");
							goto fault;
						}
					default: //<node...>
						state = 0;
						context->state = TAG_OPEN;
						if (XML_DEBUG) printf("Tag open begin\n");
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
									chain->name[len] = '\0';
									if (cb->tagopen) cb->tagopen( ctx, at, len );
									if (XML_DEBUG) print_chain(root, curr_depth);
									break;
								case 1:
									if (XML_DEBUG) printf("reading attrs for %s, next='%c'\n",chain->name,*p);
									if (search = parse_attrs(p,context)) {
										p = search;
										state = 2;
									} else {
										goto fault;
									}
								case 2:
									while(state == 2) {
										if (XML_DEBUG) printf("state=2, char='%c'\n",*p);
										switch(*p) {
											case 0: goto eod;
											case_wsp : p = eat_wsp(context, p);
											case '/' :
												if (cb->tagclose) cb->tagclose( ctx, at, len );
												safefree(chain->name);
												chain--;
												curr_depth--;
												//print_chain(root, curr_depth);
												p = eat_wsp(context, p+1);
											case '>' : state = 3; p++; break;
											default  :
												printf("bad char '%c' at the end of tag\n",*p);
												goto fault;
										}
									}
									if (XML_DEBUG) printf("End of state 2\n");
									context->state = CONTENT_WAIT;
									goto next;
							}
						}
				}
				break;
			default:
				context->state = TEXT_READ;
				at = p;
				char *lastwsp = 0;

/*				
				char *begin;
				char *textdata;
				char *tailwsp;
				
				while (1) {
					switch(*p) {
						case 0  :
						case '<':
							if (textstate == TEXT_WSP) {
								//trailing whitespace
							}
							goto eod;
						case_wsp :
							if (XML_DEBUG) printf("TEXT_READ -> got wsp, next = '%c'\n",*p);
							if (textstate == TEXT_DATA) { lastwsp = p; }
							textstate = TEXT_WSP;
							p++;
						default:
							textstate = TEXT_DATA;
							if (*p == '&') {
								if (XML_DEBUG) printf("TEXT_READ -> parse_entity, concat = %d\n",concat);
								end = p;
								if( entity = parse_entity(&p) ) {
									if (XML_DEBUG) printf("TEXT_READ -> got entity %s, concat = %d\n",entity->entity,concat);
									if(cb->text) {
										if (end > at) cb->text(ctx, at, end - at, concat++);
										cb->text(ctx, entity->entity, entity->length, concat++);
									}
									at = p;
									if (XML_DEBUG) printf("TEXT_READ -> entity callback done, concat = %d, next='%c'\n",concat,*p);
									break;
								}
							}
							p++;
					}
				}
*/
				unsigned int concat = 0;
				if (XML_DEBUG) printf("Enter TEXT_READ, concat = %d\n",concat);
				if (!context->save_wsp) {
					// Try to eat initial whitespace
					p = eat_wsp(context, p);
					if (p > at) {
						//printf("!! Skipped initial whitespace length=%d\n", p - at);
						at = p;
					}
				}
				textstate = TEXT_DATA;
				while (1) {
					switch(*p) {
						case 0  :
						case '<':
							if (p > at) {
								if (XML_DEBUG) printf("TEXT_READ -> ready for leave, have concat=%d at=%d, p=%d, lastwsp=%d\n", concat, at, p, lastwsp);
								if (!context->save_wsp && textstate == TEXT_WSP) {
									//if (XML_DEBUG)
									//printf("Skip trailing whitespace chardata=%d wspdata=%d\n", lastwsp - at, p - lastwsp);
									
								} else {
									lastwsp = p;
								}
								if(cb->text) {
									if (lastwsp  > at) {
										cb->text(ctx, at, lastwsp - at, concat++ );
									} else {
										cb->text(ctx, "", 0, concat++ ); // we need a terminator
									}
								}
							} else {
								if (XML_DEBUG) printf("!! Got no text data\n");
							}
							context->state = CONTENT_WAIT;
							if (XML_DEBUG) printf("Leave TEXT_READ\n");
							if (*p == 0) goto eod;
							goto next;
						case_wsp :
							if (XML_DEBUG) printf("TEXT_READ -> got wsp, next = '%c'\n",*p);
							if (textstate == TEXT_DATA) { lastwsp = p; }
							textstate = TEXT_WSP;
							p++;
							break;
						default:
							textstate = TEXT_DATA;
							if (*p == '&') {
								if (XML_DEBUG) printf("TEXT_READ -> parse_entity, concat = %d\n",concat);
								if (p > at && cb->textpart) cb->textpart(ctx, at, p - at);
								if( p = parse_entity(context,p,cb->textpart) ) {
									at = p;
									if (XML_DEBUG) printf("TEXT_READ -> entity callback done, concat = %d, next='%c'\n",concat,*p);
									break;
								} else {
									printf("TEXT_READ -> read entity failed\n");
									goto fault;
								}
							}
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
		//printf("End of document, context->state=%d\n",context->state);
		switch(context->state) {
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
				printf("Bad context->state %d at the end of document\n",context->state);
		}
	
	fault:
	safefree(root);
	return;
}
