#include "EXTERN.h"

#include "perl.h"
#include "XSUB.h"

#include "ppport.h"
#include "xmlfast.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

/*
commit 30866c9f74d890c45e8da27ea855468a314a59cf
xmlbare 1785/s      --    -19%
xmlfast 2209/s     24%      --

*/

typedef struct {
	// config
	unsigned int order;
	unsigned int trim;
	unsigned int bytes;
	unsigned int utf8upgrade;
	unsigned int utf8decode;
	unsigned int arrays;
	SV  * attr;
	SV  * text;
	SV  * join;
	SV  * cdata;
	SV  * comm;
	HV  * array;

	// state
	char *encoding;
	SV   *encode;
	int depth;
	unsigned int chainsize;
	HV ** hchain;
	HV  * hcurrent;
	SV  * ctag;
	SV  * pi;
	
	SV  * attrname;
	SV  * textval;
	
} parsestate;


#define hv_store_a( hv, key, sv ) \
	STMT_START { \
		SV **exists; \
		char *kv = SvPV_nolen(key); \
		int   kl = SvCUR(key); \
		if( exists = hv_fetch(hv, kv, kl, 0) ) { \
			if (SvTYPE( SvRV(*exists) ) == SVt_PVAV) { \
				AV *av = (AV *) SvRV( *exists ); \
				/* printf("push '%s' to array in key '%s'\n", SvPV_nolen(old), kv); */ \
				av_push( av, sv ); \
			} \
			else { \
				AV *av   = newAV(); \
				if (SvROK(*exists)) { \
					SvREFCNT_inc(*exists); \
					av_push( av, *exists ); \
				} else { \
					SV *old  = newSV(0); \
					sv_copypv(old, *exists); \
					av_push( av, old ); \
				} \
				av_push( av, sv ); \
				hv_store( hv, kv, kl, newRV_noinc( (SV *) av ), 0 ); \
			} \
		} else { \
			hv_store(hv, kv, kl, sv, 0); \
		} \
	} STMT_END

#define hv_store_aa( hv, key, sv ) \
	STMT_START { \
		SV **exists; \
		char *kv = SvPV_nolen(key); \
		int   kl = SvCUR(key); \
		if( ( exists = hv_fetch(hv, kv, kl, 0) ) && SvROK(*exists) && (SvTYPE( SvRV(*exists) ) == SVt_PVAV) ) { \
			AV *av = (AV *) SvRV( *exists ); \
			/* printf("push '%s' to array in key '%s'\n", SvPV_nolen(old), kv); */ \
			av_push( av, sv ); \
		} \
		else { \
			AV *av   = newAV(); \
			av_push( av, sv ); \
			hv_store( hv, kv, kl, newRV_noinc( (SV *) av ), 0 ); \
		} \
	} STMT_END

#define hv_store_cat( hv, key, sv ) \
	STMT_START { \
		SV **exists; \
		char *kv = SvPV_nolen(key); \
		int   kl = SvCUR(key); \
		if( exists = hv_fetch(hv, kv, kl, 0) ) { \
			if (SvTYPE( SvRV(*exists) ) == SVt_PVAV) { \
				AV *av = (AV *) SvRV( *exists ); \
				SV **val; \
				I32 avlen = av_len(av); \
				if ( (val = av_fetch(av,avlen,0)) && SvPOK(*val) ) { \
					sv_catsv(*val, sv); \
					SvREFCNT_dec(sv); \
				} else { \
					av_push( av, sv ); \
				} \
			} \
			else { \
				if (SvROK(*exists)) { \
					croak("Can't concat to reference value %s",sv_reftype(SvRV(*exists),TRUE)); \
				} else { \
					sv_catsv(*exists, sv);\
					SvREFCNT_dec(sv); \
				} \
			} \
		} else { \
			hv_store(hv, kv, kl, sv, 0); \
		} \
	} STMT_END

#define xml_sv_decode(ctx, sv) \
	STMT_START { \
		if (ctx->utf8upgrade) { \
			SvUTF8_on(sv); \
		} \
		else if (ctx->utf8decode) { \
			sv_utf8_decode(sv); \
		} \
		else if (ctx->encode) { \
			(void) sv_recode_to_utf8(sv, ctx->encode); \
		} \
	} STMT_END

void on_comment(void * pctx, char * data,unsigned int length) {
	if (!pctx) croak("Context not passed to on_comment");
	parsestate *ctx = pctx;
	SV         *sv  = newSVpvn(data, length);
	hv_store_a(ctx->hcurrent, ctx->comm, sv );
}

void on_uchar(void * pctx, wchar_t chr) {
	if (!pctx) croak("Context not passed to on_text_part");
	parsestate *ctx = pctx;
	// TODO: how to define where to store: either text or attribute
	char *start, *end;
	STRLEN len = 0;
	if (ctx->textval) {
		len = SvCUR(ctx->textval);
	} else {
		ctx->textval = newSVpvn("",0);
	}
	sv_grow(ctx->textval, len + UTF8_MAXBYTES+1 );
	start = end = SvEND(ctx->textval);
	//printf("Got string (%p) start=%p\n",SvPVX(ctx->textval),start);
	end = uvchr_to_utf8(start, chr);
	*end = '\0';
	SvCUR_set(ctx->textval,len + end - start);
	//printf("appended uchar: %s[%d] (%s) [%d+%d]\n",SvPV_nolen(ctx->textval), SvCUR(ctx->textval), start, len, end - start);
}

void on_bytes_part(void * pctx, char * data, unsigned int length) {
	if (!pctx) croak("Context not passed to on_bytes_part");
	parsestate *ctx = pctx;
	if (ctx->textval) {
		if (length > 0) { sv_catpvn(ctx->textval, data, length); }
	} else {
		ctx->textval = newSVpvn(data, length);
	}
	//hv_store_a( ctx->hcurrent, ctx->text, ctx->textval );
	//ctx->textval = 0;
}

void on_bytes(void * pctx, char * data, unsigned int length) {
	if (!pctx) croak("Context not passed to on_bytes");
	parsestate *ctx = pctx;
	if (!ctx->textval && !length) {
		warn("Called on_bytes with empty text and empty body");
	}
	if (ctx->textval) {
		if (length > 0) { sv_catpvn(ctx->textval, data, length); }
	} else {
		ctx->textval = newSVpvn(data, length);
	}
	xml_sv_decode(ctx,ctx->textval);
	if (ctx->attrname) {
		if (ctx->pi) {
			printf("PI %s, attr %s='%s'\n",SvPV_nolen(ctx->pi), SvPV_nolen(ctx->attrname),SvPV_nolen(ctx->textval) );
			sv_2mortal(ctx->textval);
		} else {
			hv_store_a(ctx->hcurrent, ctx->attrname, ctx->textval);
		}
		sv_2mortal(ctx->attrname);
		ctx->attrname = 0;
		ctx->textval = 0;
	}
	else {
		hv_store_a(ctx->hcurrent, ctx->text, ctx->textval);
	}
	ctx->textval = 0;
}


void on_cdata(void * pctx, char * data,unsigned int length) {
	if (!pctx) croak("Context not passed to on_cdata");
	parsestate *ctx = pctx;
	SV *sv   = newSVpvn(data, length);
	xml_sv_decode(ctx,sv);
	hv_store_a(ctx->hcurrent, ctx->cdata, sv );
}

void on_pi_open(void * pctx, char * data, unsigned int length) {
	if (!pctx) croak("Context not passed to on_tag_open");
	parsestate *ctx = pctx;
	ctx->pi = newSVpvn(data,length);
}

void on_pi_close(void * pctx, char * data, unsigned int length) {
	if (!pctx) croak("Context not passed to on_tag_open");
	parsestate *ctx = pctx;
	//SvREFCNT_dec(ctx->pi);
	sv_2mortal(ctx->pi);
	ctx->pi = 0;
}

void on_tag_open(void * pctx, char * data, unsigned int length) {
	if (!pctx) croak("Context not passed to on_tag_open");
	parsestate *ctx = pctx;
	if (ctx->textval) {
		xml_sv_decode(ctx,ctx->textval);
		hv_store_a(ctx->hcurrent, ctx->text, ctx->textval);
		ctx->textval = 0;
	}
	HV * hv = newHV();
	//SV *sv = newRV_noinc( (SV *) hv );
	//hv_store(ctx->hcurrent, data, length, sv, 0);
	ctx->depth++;
	if (ctx->depth >= ctx->chainsize) {
		warn("XML depth too high. Consider increasing `_max_depth' to at more than %d to avoid reallocations",ctx->chainsize);
		HV ** keep = ctx->hchain;
		ctx->hchain = safemalloc( sizeof(ctx->hcurrent) * ctx->chainsize * 2);
		memcpy(ctx->hchain, keep, sizeof(ctx->hcurrent) * ctx->chainsize * 2);
		ctx->chainsize *= 2;
		safefree(keep);
	}
	ctx->hchain[ ctx->depth ] = ctx->hcurrent;
	//node_depth++;
	//node_chain[node_depth] = collect;
	ctx->hcurrent = hv;
}

void on_tag_close(void * pctx, char * data, unsigned int length) {
	if (!pctx) croak("Context not passed to on_tag_close");
	parsestate *ctx = pctx;
	// TODO: check node name
	
	// Text joining
	SV **text;
	I32 keys = HvKEYS(ctx->hcurrent);
	SV  *svtext = 0;
	if (ctx->textval) {
		xml_sv_decode(ctx,ctx->textval);
		hv_store_a(ctx->hcurrent, ctx->text, ctx->textval);
		ctx->textval = 0;
	}
	if (ctx->text) {
		// we may have stored text node
		if ((text = hv_fetch(ctx->hcurrent, SvPV_nolen(ctx->text), SvCUR(ctx->text), 0)) && SvOK(*text)) {
			if (SvTYPE( SvRV(*text) ) == SVt_PVAV) {
				AV *av = (AV *) SvRV( *text );
				SV **val;
				I32 len = 0, avlen = av_len(av) + 1;
				if (ctx->join) {
					svtext = newSVpvn("",0);
					if (SvCUR(ctx->join)) {
						//printf("Join length = %d, avlen=%d\n",SvCUR(*join),avlen);
						for ( len = 0; len < avlen; len++ ) {
							if( ( val = av_fetch(av,len,0) ) && SvOK(*val) ) {
								//printf("Join %s with '%s'\n",SvPV_nolen(*val), SvPV_nolen(ctx->join));
								if(len > 0) { sv_catsv(svtext,ctx->join); }
								//printf("Join %s with '%s'\n",SvPV_nolen(*val), SvPV_nolen(ctx->join));
								sv_catsv(svtext,*val);
							}
						}
					} else {
						//printf("Optimized join loop\n");
						for ( len = 0; len < avlen; len++ ) {
							if( ( val = av_fetch(av,len,0) ) && SvOK(*val) ) {
								//printf("Join %s with ''\n",SvPV_nolen(*val));
								sv_catsv(svtext,*val);
							}
						}
					}
					//printf("Joined: to %s => '%s'\n",SvPV_nolen(ctx->text),SvPV_nolen(svtext));
					SvREFCNT_inc(svtext);
					hv_store(ctx->hcurrent, SvPV_nolen(ctx->text), SvCUR(ctx->text), svtext, 0);
				}
				else
				// currently unreachable, since if we have single element, it is stored as SV value, not AV
				//if ( avlen == 1 ) {
				//	Perl_warn("# AVlen=1\n");
				//	/* works
				//	svtext = newSVpvn("",0);
				//	val = av_fetch(av,0,0);
				//	if (val && SvOK(*val)) {
				//		//svtext = *val;
				//		//SvREFCNT_inc(svtext);
				//		sv_catsv(svtext,*val);
				//	}
				//	*/
				//	val = av_fetch(av,0,0);
				//	if (val) {
				//		svtext = *val;
				//		SvREFCNT_inc(svtext);
				//		hv_store(ctx->hcurrent, SvPV_nolen(ctx->text), SvCUR(ctx->text), svtext, 0);
				//	}
				//}
				//else
				{
					// Remebmer for use if it is single
					warn("# No join\n");
					svtext = newRV( (SV *) av );
				}
			} else {
				svtext = *text;
				SvREFCNT_inc(svtext);
			}
		}
	}
	//printf("svtext=(0x%lx) '%s'\n", svtext, svtext ? SvPV_nolen(svtext) : "");
	// Text joining
	SV *tag = newSVpvn(data,length);
	sv_2mortal(tag);
	if (ctx->depth > -1) {
		HV *hv = ctx->hcurrent;
		ctx->hcurrent = ctx->hchain[ ctx->depth ];
		ctx->hchain[ ctx->depth ];// = (HV *)NULL;
		ctx->depth--;
		if (keys == 0) {
			//printf("Tag %s have no keys\n", SvPV_nolen(tag));
			SvREFCNT_dec(hv);
			SV *sv = newSVpvn("",0);
			if (ctx->arrays) {
				hv_store_aa(ctx->hcurrent, tag, sv);
			}
			else if (ctx->array && (hv_exists(ctx->array, data,length ) )) {
				hv_store_aa(ctx->hcurrent, tag, sv);
			}
			else {
				hv_store_a(ctx->hcurrent, tag, sv);
			}
		}
		else
		if (keys == 1 && svtext) {
			//SV *sx   = newSVpvn(data, length);sv_2mortal(sx);
			//printf("Hash in tag '%s' for destruction have refcnt = %d (%lx | %lx)\n",SvPV_nolen(sx),SvREFCNT(hv), hv, ctx->hcurrent);
			SvREFCNT_inc(svtext);
			SvREFCNT_dec(hv);
			//hv_store(ctx->hcurrent, data, length, svtext, 0);
			if (ctx->arrays) {
				//printf("Cast %s as array (all should be)\n",SvPV_nolen(tag));
				hv_store_aa(ctx->hcurrent, tag, svtext);
			}
			else if (ctx->array && (hv_exists(ctx->array, data,length ) )) {
				//printf("Cast %s as array\n",SvPV_nolen(tag));
				hv_store_aa(ctx->hcurrent, tag, svtext);
			}
			else {
				hv_store_a(ctx->hcurrent, tag, svtext);
			}
		} else {
			SV *sv = newRV_noinc( (SV *) hv );
			SV **ary;
			//printf("Store hash into RV '%lx'\n",sv);
			//hv_store(ctx->hcurrent, data, length, sv, 0);
			//printf("Check %s to be array\n",SvPV_nolen(tag));
			if (ctx->arrays) {
				//printf("Cast %s as array (all should be)\n",SvPV_nolen(tag));
				hv_store_aa(ctx->hcurrent, tag, sv);
			}
			else if (ctx->array && ( hv_exists(ctx->array, data,length ) )) {
				//printf("Cast %s as array\n",SvPV_nolen(tag));
				hv_store_aa(ctx->hcurrent, tag, sv);
			}
			else {
				hv_store_a(ctx->hcurrent, tag, sv);
			}
		}
		if (svtext) SvREFCNT_dec(svtext);
	} else {
		SV *sv   = newSVpvn(data, length);
		croak("Bad depth: %d for tag close %s\n",ctx->depth,SvPV_nolen(sv));
	}
}

void on_attr_name(void * pctx, char * data,unsigned int length) {
	if (!pctx) croak("Context not passed to on_attr_name");
	parsestate *ctx = pctx;
	if (ctx->textval) {
		croak("Have textval=%s, while called attrname\n",SvPV_nolen(ctx->textval));
	}
	if (ctx->attrname) {
		croak("Called attrname, while have attrname=%s\n",SvPV_nolen(ctx->attrname));
	}
	SV **key;
	if (ctx->pi) {
		ctx->attrname = newSVpvn(data,length);
	} else {
		if( ctx->attr ) {
			ctx->attrname = newSV(length + SvCUR(ctx->attr));
			sv_copypv(ctx->attrname, ctx->attr);
			sv_catpvn(ctx->attrname, data, length);
		} else {
			ctx->attrname = newSVpvn(data, length);
		}
	}
}

void on_warn(char * format, ...) {
	//if (!pctx) croak("Context not passed");
	va_list va;
	va_start(va,format);
	SV *text = sv_2mortal(newSVpvn("",0));
	sv_vcatpvf(text, format, &va);
	warn("%s",SvPV_nolen(text));
	va_end(va);
}

void on_die(char * format, ...) {
	//if (!pctx) croak("Context not passed");
	va_list va;
	va_start(va,format);
	SV *text = sv_2mortal(newSVpvn("",0));
	sv_vcatpvf(text, format, &va);
	croak("%s",SvPV_nolen(text));
	va_end(va);
}

SV * find_encoding(char * encoding) {
	dSP;
	int count;
	//require_pv("Encode.pm");
	
	ENTER;
	SAVETMPS;
	//printf("searching encoding '%s'\n",encoding);
	
	PUSHMARK(SP);
	XPUSHs(sv_2mortal(newSVpv(encoding, 0)));
	PUTBACK;
	
	count = call_pv("Encode::find_encoding",G_SCALAR);
	
	SPAGAIN;
	if (SvTRUE(ERRSV)) {
		printf("Shit happens: %s\n", SvPV_nolen(ERRSV));
		POPs;
	}
	
	if (count != 1)
		croak("Bad number of returned values: %d",count);
	
	SV *encode = POPs;
	//sv_dump(encode);
	SvREFCNT_inc(encode);
	//printf("Got encode=%s for encoding='%s'\n",SvPV_nolen(encode),encoding);
	
	PUTBACK;
	
	FREETMPS;
	LEAVE;
	
	return encode;
}

/*
#define newRVHV() newRV_noinc((SV *)newHV())
#define rv_hv_store(rv,key,len,sv,f) hv_store((HV*)SvRV(rv), key,len,sv,f)
#define rv_hv_fetch(rv,key,len,f) hv_fetch((HV*)SvRV(rv), key,len,f)
*/
/*
void
_test()
	CODE:
		SV *sv1 = newRVHV();
		SV *sv2 = newRVHV();
		sv_2mortal(sv1);
		sv_2mortal(sv2);
		SV *test = newSVpvn("test",4);
		rv_hv_store(sv1, "test",4,test,0);
		SvREFCNT_inc(test);
		rv_hv_store(sv2, "test",4,test,0);
*/

MODULE = XML::Fast		PACKAGE = XML::Fast

BOOT:
	init_entities();

SV*
_xml2hash(xml,conf)
		char *xml;
		HV *conf;
	CODE:
		/*UV unicode = 0x2622;
		U8 chr[UTF8_MAXBYTES];
		U8 *end = uvchr_to_utf8(chr, unicode);
		*end = '\0';
		croak("utf test=%s (len=%d, max=%d)\n", chr, end - chr, UTF8_MAXBYTES);*/
		
		parser_state state;
		memset(&state,0,sizeof(state));
		
		parsestate ctx;
		memset(&ctx,0,sizeof(parsestate));
		state.ctx = &ctx;
		SV **key;
		if ((key = hv_fetch(conf, "order", 5, 0)) && SvTRUE(*key)) {
			ctx.order = 1;
		}
		if ((key = hv_fetch(conf, "trim", 4, 0)) && SvTRUE(*key)) {
			ctx.trim = 1;
		}
		if ((key = hv_fetch(conf, "bytes", 5, 0)) && SvTRUE(*key)) {
			ctx.bytes = 1;
		} else {
			if ((key = hv_fetch(conf, "utf8decode", 10, 0)) && SvTRUE(*key)) {
				ctx.utf8decode = 1;
			} else {
				ctx.utf8upgrade = 1;
			}
		}
		if ((key = hv_fetch(conf, "trim", 4, 0)) && SvTRUE(*key)) {
			ctx.trim = 1;
		}
		
		if ((key = hv_fetch(conf, "attr", 4, 0)) && SvPOK(*key)) {
			ctx.attr = *key;
		}
		if ((key = hv_fetch(conf, "text", 4, 0)) && SvPOK(*key)) {
			ctx.text = *key;
		}
		if ((key = hv_fetch(conf, "join", 4, 0)) && SvPOK(*key)) {
			ctx.join = *key;
		}
		if ((key = hv_fetch(conf, "cdata", 5, 0)) && SvPOK(*key)) {
			ctx.cdata = *key;
		}
		if ((key = hv_fetch(conf, "comm", 4, 0)) && SvPOK(*key)) {
			ctx.comm = *key;
		}
		if ((key = hv_fetch(conf, "array", 5, 0)) && SvOK(*key)) {
			if (SvROK(*key) && SvTYPE( SvRV(*key) ) == SVt_PVAV) {
				AV *av = (AV *) SvRV( *key );
				ctx.array = newHV();
				//SV *array_container = newRV_noinc( (SV *)ctx.array );
				//sv_2mortal(array_container);
				I32 len = 0, avlen = av_len(av) + 1;
				SV **val;
				for ( len = 0; len < avlen; len++ ) {
					if( ( val = av_fetch(av,len,0) ) && SvOK(*val) ) {
						if(SvPOK(*val)) {
							//printf("Remember %s should be array\n",SvPV_nolen(*val));
							hv_store( ctx.array, SvPV_nolen(*val), SvCUR(*val), newSV(0), 0 );
						} else {
							croak("Bad enrty in array antry: %s",SvPV_nolen(*val));
						}
					}
				}
				
				
			}
			else if (!SvROK(*key)) {
				//printf("Remember all should be arrays\n");
				ctx.arrays = SvTRUE(*key) ? 1 : 0;
			}
			else {
				croak("Bad entry in array: %s",SvPV_nolen(*key));
			}
		}
		
		
		if ((key = hv_fetch(conf, "_max_depth", 10, 0)) && SvOK(*key)) {
			ctx.chainsize = SvIV(*key);
			if (ctx.chainsize < 1) {
				croak("_max_depth contains bad value (%d)",ctx.chainsize);
			}
		} else {
			ctx.chainsize = 256;
		}
		
		
		//xml_callbacks cbs;
		//memset(&cbs,0,sizeof(xml_callbacks));
		if (!ctx.bytes) {
			//if (utf8) {
				ctx.encoding = "utf8";
			//} else {
			//	ctx.encoding = "utf-8";
			//	ctx.encode = find_encoding(ctx.encoding);
			//}
		}
		
		if (ctx.order) {
			croak("Ordered mode not implemented yet\n");
		} else{
			ctx.hcurrent = newHV();
			
			ctx.hchain = safemalloc( sizeof(ctx.hcurrent) * ctx.chainsize);
			ctx.depth = -1;
			
			RETVAL  = newRV_noinc( (SV *) ctx.hcurrent );
			state.cb.piopen      = on_pi_open;
			state.cb.piclose     = on_pi_close;
			state.cb.tagopen      = on_tag_open;
			state.cb.tagclose     = on_tag_close;
			
			state.cb.attrname     = on_attr_name;
			if ((key = hv_fetch(conf, "nowarn", 6, 0)) && SvTRUE(*key)) {
				//
			} else {
				state.cb.warn         = on_warn;
			}
			state.cb.die         = on_die;
			
			if(ctx.comm)
				state.cb.comment      = on_comment;
			
			if(ctx.cdata)
				state.cb.cdata        = on_cdata;
			else if(ctx.text)
				state.cb.cdata        = on_bytes;
			
			if(ctx.text) {
				state.cb.bytes        = on_bytes;
				state.cb.bytespart    = on_bytes_part;
				state.cb.uchar        = on_uchar;
			}
			
			if (!ctx.trim)
				state.save_wsp     = 1;
		}
		parse(xml,&state);
		if(ctx.encode) SvREFCNT_dec(ctx.encode);
		safefree(ctx.hchain);
	OUTPUT:
		RETVAL

