/* encode.c - ber output encoding routines */
/*
 * Copyright 1998-1999 The OpenLDAP Foundation, All Rights Reserved.
 * COPYING RESTRICTIONS APPLY, see COPYRIGHT file
 */
/* Portions
 * Copyright (c) 1990 Regents of the University of Michigan.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that this notice is preserved and that due credit is given
 * to the University of Michigan at Ann Arbor. The name of the University
 * may not be used to endorse or promote products derived from this
 * software without specific prior written permission. This software
 * is provided ``as is'' without express or implied warranty.
 */

#include "portable.h"

#include <stdio.h>

#include <ac/stdlib.h>

#include <ac/stdarg.h>
#include <ac/socket.h>
#include <ac/string.h>

#undef LDAP_F_PRE
#define LDAP_F_PRE LDAP_F_EXPORT

#include "lber-int.h"

static int ber_put_len LDAP_P((
	BerElement *ber,
	ber_len_t len,
	int nosos ));

static int ber_start_seqorset LDAP_P((
	BerElement *ber,
	ber_tag_t tag ));

static int ber_put_seqorset LDAP_P(( BerElement *ber ));

static int ber_put_int_or_enum LDAP_P((
	BerElement *ber,
	ber_int_t num,
	ber_tag_t tag ));


static ber_len_t
ber_calc_taglen( ber_tag_t tag )
{
	int	i;
	ber_tag_t	mask;

	/* find the first non-all-zero byte in the tag */
	for ( i = sizeof(ber_tag_t) - 1; i > 0; i-- ) {
		mask = ((ber_tag_t)0xffU << (i * 8));
		/* not all zero */
		if ( tag & mask )
			break;
	}

	return( i + 1 );
}

static int
ber_put_tag(
	BerElement	*ber,
	ber_tag_t tag,
	int nosos )
{
	int rc;
	ber_len_t	taglen;
	ber_tag_t	ntag;

	assert( ber != NULL );
	assert( BER_VALID( ber ) );

	taglen = ber_calc_taglen( tag );

	ntag = LBER_TAG_HTON( tag );

	rc = ber_write( ber,
		((char *) &ntag) + sizeof(ber_tag_t) - taglen,
	    taglen, nosos );

	return rc;
}

static ber_len_t
ber_calc_lenlen( ber_len_t len )
{
	/*
	 * short len if it's less than 128 - one byte giving the len,
	 * with bit 8 0.
	 */

	if ( len <= (ber_len_t) 0x7FU )
		return( 1 );

	/*
	 * long len otherwise - one byte with bit 8 set, giving the
	 * length of the length, followed by the length itself.
	 */

	if ( len <= (ber_len_t) 0xffU )
		return( 2 );
	if ( len <= (ber_len_t) 0xffffU )
		return( 3 );
	if ( len <= (ber_len_t) 0xffffffU )
		return( 4 );

	return( 5 );
}

static int
ber_put_len( BerElement *ber, ber_len_t len, int nosos )
{
	int		i;
	char		lenlen;
	ber_len_t	mask;
	ber_len_t	netlen;

	assert( ber != NULL );
	assert( BER_VALID( ber ) );

	/*
	 * short len if it's less than 128 - one byte giving the len,
	 * with bit 8 0.
	 */

	if ( len <= 127 ) {
		netlen = LBER_LEN_HTON( len );
		return( ber_write( ber, (char *) &netlen + sizeof(long) - 1,
		    1, nosos ) );
	}

	/*
	 * long len otherwise - one byte with bit 8 set, giving the
	 * length of the length, followed by the length itself.
	 */

	/* find the first non-all-zero byte */
	for ( i = sizeof(ber_len_t) - 1; i > 0; i-- ) {
		mask = (0xffU << (i * 8));
		/* not all zero */
		if ( len & mask )
			break;
	}
	lenlen = (unsigned char) ++i;
	if ( lenlen > 4 )
		return( -1 );
	lenlen |= 0x80UL;

	/* write the length of the length */
	if ( ber_write( ber, &lenlen, 1, nosos ) != 1 )
		return( -1 );

	/* write the length itself */
	netlen = LBER_LEN_HTON( len );
	if ( ber_write( ber, (char *) &netlen + (sizeof(ber_len_t) - i), i, nosos )
	    != i )
		return( -1 );

	return( i + 1 );
}

static int
ber_put_int_or_enum(
	BerElement *ber,
	ber_int_t num,
	ber_tag_t tag )
{
	int	i, sign;
	ber_len_t	len, lenlen, taglen;
	ber_uint_t	unum, netnum, mask;

	assert( ber != NULL );
	assert( BER_VALID( ber ) );

	sign = (num < 0);
	unum = num;     /* Bit fiddling should be done with unsigned values */

	/*
	 * high bit is set - look for first non-all-one byte
	 * high bit is clear - look for first non-all-zero byte
	 */
	for ( i = sizeof(ber_int_t) - 1; i > 0; i-- ) {
		mask = (0xffU << (i * 8));

		if ( sign ) {
			/* not all ones */
			if ( (unum & mask) != mask )
				break;
		} else {
			/* not all zero */
			if ( unum & mask )
				break;
		}
	}

	/*
	 * we now have the "leading byte".  if the high bit on this
	 * byte matches the sign bit, we need to "back up" a byte.
	 */
	mask = (unum & (0x80U << (i * 8)));
	if ( (mask && !sign) || (sign && !mask) )
		i++;

	len = i + 1;

	if ( (taglen = ber_put_tag( ber, tag, 0 )) == -1 )
		return( -1 );

	if ( (lenlen = ber_put_len( ber, len, 0 )) == -1 )
		return( -1 );
	i++;
	netnum = LBER_INT_HTON( unum );
	if ( ber_write( ber, (char *) &netnum + (sizeof(ber_int_t) - i), i, 0 )
	   != i )
		return( -1 );

	/* length of tag + length + contents */
	return( taglen + lenlen + i );
}

int
ber_put_enum(
	BerElement *ber,
	ber_int_t num,
	ber_tag_t tag )
{
	assert( ber != NULL );
	assert( BER_VALID( ber ) );

	if ( tag == LBER_DEFAULT )
		tag = LBER_ENUMERATED;

	return( ber_put_int_or_enum( ber, num, tag ) );
}

int
ber_put_int(
	BerElement *ber,
	ber_int_t num,
	ber_tag_t tag )
{
	assert( ber != NULL );
	assert( BER_VALID( ber ) );

	if ( tag == LBER_DEFAULT )
		tag = LBER_INTEGER;

	return( ber_put_int_or_enum( ber, num, tag ) );
}

int
ber_put_ostring(
	BerElement *ber,
	LDAP_CONST char *str,
	ber_len_t len,
	ber_tag_t tag )
{
	ber_len_t	taglen, lenlen;
	int rc;
#ifdef STR_TRANSLATION
	int	free_str;
#endif /* STR_TRANSLATION */

	assert( ber != NULL );
	assert( str != NULL );

	assert( BER_VALID( ber ) );

	if ( tag == LBER_DEFAULT )
		tag = LBER_OCTETSTRING;

	if ( (taglen = ber_put_tag( ber, tag, 0 )) == -1 )
		return( -1 );

#ifdef STR_TRANSLATION
	if ( len > 0 && ( ber->ber_options & LBER_TRANSLATE_STRINGS ) != 0 &&
	    ber->ber_encode_translate_proc ) {
		if ( (*(ber->ber_encode_translate_proc))( &str, &len, 0 )
		    != 0 ) {
			return( -1 );
		}
		free_str = 1;
	} else {
		free_str = 0;
	}
#endif /* STR_TRANSLATION */

	if ( (lenlen = ber_put_len( ber, len, 0 )) == -1 ||
		(unsigned long) ber_write( ber, str, len, 0 ) != len ) {
		rc = -1;
	} else {
		/* return length of tag + length + contents */
		rc = taglen + lenlen + len;
	}

#ifdef STR_TRANSLATION
	if ( free_str ) {
		LBER_FREE( str );
	}
#endif /* STR_TRANSLATION */

	return( rc );
}

int
ber_put_berval(
	BerElement *ber,
	LDAP_CONST struct berval *bv,
	ber_tag_t tag )
{
	assert( ber != NULL );
	assert( BER_VALID( ber ) );

	if( bv == NULL || bv->bv_len == 0 ) {
		return ber_put_ostring( ber, "", (ber_len_t) 0, tag );
	}

	return ber_put_ostring( ber, bv->bv_val, bv->bv_len, tag );
}

int
ber_put_string(
	BerElement *ber,
	LDAP_CONST char *str,
	ber_tag_t tag )
{
	assert( ber != NULL );
	assert( str != NULL );

	assert( BER_VALID( ber ) );

	return( ber_put_ostring( ber, str, strlen( str ), tag ));
}

int
ber_put_bitstring(
	BerElement *ber,
	LDAP_CONST char *str,
	ber_len_t blen /* in bits */,
	ber_tag_t tag )
{
	ber_len_t		taglen, lenlen, len;
	unsigned char	unusedbits;

	assert( ber != NULL );
	assert( str != NULL );

	assert( BER_VALID( ber ) );

	if ( tag == LBER_DEFAULT )
		tag = LBER_BITSTRING;

	if ( (taglen = ber_put_tag( ber, tag, 0 )) == -1 )
		return( -1 );

	len = ( blen + 7 ) / 8;
	unusedbits = (unsigned char) ((len * 8) - blen);
	if ( (lenlen = ber_put_len( ber, len + 1, 0 )) == -1 )
		return( -1 );

	if ( ber_write( ber, (char *)&unusedbits, 1, 0 ) != 1 )
		return( -1 );

	if ( (ber_len_t) ber_write( ber, str, len, 0 ) != len )
		return( -1 );

	/* return length of tag + length + unused bit count + contents */
	return( taglen + 1 + lenlen + len );
}

int
ber_put_null( BerElement *ber, ber_tag_t tag )
{
	ber_len_t	taglen;

	assert( ber != NULL );
	assert( BER_VALID( ber ) );

	if ( tag == LBER_DEFAULT )
		tag = LBER_NULL;

	if ( (taglen = ber_put_tag( ber, tag, 0 )) == -1 )
		return( -1 );

	if ( ber_put_len( ber, 0, 0 ) != 1 )
		return( -1 );

	return( taglen + 1 );
}

int
ber_put_boolean(
	BerElement *ber,
	ber_int_t boolval,
	ber_tag_t tag )
{
	ber_len_t		taglen;
	unsigned char	trueval = 0xFFU;
	unsigned char	falseval = 0x00U;

	assert( ber != NULL );
	assert( BER_VALID( ber ) );

	if ( tag == LBER_DEFAULT )
		tag = LBER_BOOLEAN;

	if ( (taglen = ber_put_tag( ber, tag, 0 )) == -1 )
		return( -1 );

	if ( ber_put_len( ber, 1, 0 ) != 1 )
		return( -1 );

	if ( ber_write( ber, (char *)(boolval ? &trueval : &falseval), 1, 0 )
	    != 1 )
		return( -1 );

	return( taglen + 2 );
}

#define FOUR_BYTE_LEN	5

static int
ber_start_seqorset(
	BerElement *ber,
	ber_tag_t tag )
{
	Seqorset	*new;

	assert( ber != NULL );
	assert( BER_VALID( ber ) );

	new = (Seqorset *) LBER_CALLOC( 1, sizeof(Seqorset) );

	if ( new == NULL )
		return( -1 );

	new->sos_ber = ber;
	if ( ber->ber_sos == NULL )
		new->sos_first = ber->ber_ptr;
	else
		new->sos_first = ber->ber_sos->sos_ptr;

	/* Set aside room for a 4 byte length field */
	new->sos_ptr = new->sos_first + ber_calc_taglen( tag ) + FOUR_BYTE_LEN;
	new->sos_tag = tag;

	new->sos_next = ber->ber_sos;
	ber->ber_sos = new;

	return( 0 );
}

int
ber_start_seq( BerElement *ber, ber_tag_t tag )
{
	assert( ber != NULL );
	assert( BER_VALID( ber ) );

	if ( tag == LBER_DEFAULT )
		tag = LBER_SEQUENCE;

	return( ber_start_seqorset( ber, tag ) );
}

int
ber_start_set( BerElement *ber, ber_tag_t tag )
{
	assert( ber != NULL );
	assert( BER_VALID( ber ) );

	if ( tag == LBER_DEFAULT )
		tag = LBER_SET;

	return( ber_start_seqorset( ber, tag ) );
}

static int
ber_put_seqorset( BerElement *ber )
{
	ber_len_t	len, netlen;
	ber_len_t	taglen, lenlen;
	unsigned char	ltag = 0x80U + FOUR_BYTE_LEN - 1;
	Seqorset	*next;
	Seqorset	**sos = &ber->ber_sos;

	assert( ber != NULL );
	assert( BER_VALID( ber ) );

	/*
	 * If this is the toplevel sequence or set, we need to actually
	 * write the stuff out.  Otherwise, it's already been put in
	 * the appropriate buffer and will be written when the toplevel
	 * one is written.  In this case all we need to do is update the
	 * length and tag.
	 */

	len = (*sos)->sos_clen;
	netlen = LBER_LEN_HTON( len );
	if ( sizeof(ber_len_t) > 4 && len > 0xffffffffUL )
		return( -1 );

	if ( ber->ber_options & LBER_USE_DER ) {
		lenlen = ber_calc_lenlen( len );
	} else {
		lenlen = FOUR_BYTE_LEN;
	}

	if ( (next = (*sos)->sos_next) == NULL ) {
		/* write the tag */
		if ( (taglen = ber_put_tag( ber, (*sos)->sos_tag, 1 )) == -1 )
			return( -1 );

		if ( ber->ber_options & LBER_USE_DER ) {
			/* Write the length in the minimum # of octets */
			if ( ber_put_len( ber, len, 1 ) == -1 )
				return( -1 );

			if (lenlen != FOUR_BYTE_LEN) {
				/*
				 * We set aside FOUR_BYTE_LEN bytes for
				 * the length field.  Move the data if
				 * we don't actually need that much
				 */
				SAFEMEMCPY( (*sos)->sos_first + taglen +
				    lenlen, (*sos)->sos_first + taglen +
				    FOUR_BYTE_LEN, len );
			}
		} else {
			/* Fill FOUR_BYTE_LEN bytes for length field */
			/* one byte of length length */
			if ( ber_write( ber, (char *)&ltag, 1, 1 ) != 1 )
				return( -1 );

			/* the length itself */
			if ( ber_write( ber, (char *) &netlen + sizeof(long)
			    - (FOUR_BYTE_LEN - 1), FOUR_BYTE_LEN - 1, 1 )
			    != FOUR_BYTE_LEN - 1 )
				return( -1 );
		}
		/* The ber_ptr is at the set/seq start - move it to the end */
		(*sos)->sos_ber->ber_ptr += len;
	} else {
		ber_tag_t	ntag;

		/* the tag */
		taglen = ber_calc_taglen( (*sos)->sos_tag );
		ntag = LBER_TAG_HTON( (*sos)->sos_tag );
		SAFEMEMCPY( (*sos)->sos_first, (char *) &ntag +
		    sizeof(ber_tag_t) - taglen, taglen );

		if ( ber->ber_options & LBER_USE_DER ) {
			ltag = (lenlen == 1)
				? (unsigned char) len
				: (unsigned char) (0x80U + (lenlen - 1));
		}

		/* one byte of length length */
		SAFEMEMCPY( (*sos)->sos_first + 1, &ltag, 1 );

		if ( ber->ber_options & LBER_USE_DER ) {
			if (lenlen > 1) {
				/* Write the length itself */
				SAFEMEMCPY( (*sos)->sos_first + 2,
				    (char *)&netlen + sizeof(ber_len_t) -
				    (lenlen - 1),
				    lenlen - 1 );
			}
			if (lenlen != FOUR_BYTE_LEN) {
				/*
				 * We set aside FOUR_BYTE_LEN bytes for
				 * the length field.  Move the data if
				 * we don't actually need that much
				 */
				SAFEMEMCPY( (*sos)->sos_first + taglen +
				    lenlen, (*sos)->sos_first + taglen +
				    FOUR_BYTE_LEN, len );
			}
		} else {
			/* the length itself */
			SAFEMEMCPY( (*sos)->sos_first + taglen + 1,
			    (char *) &netlen + sizeof(ber_len_t) -
			    (FOUR_BYTE_LEN - 1), FOUR_BYTE_LEN - 1 );
		}

		next->sos_clen += (taglen + lenlen + len);
		next->sos_ptr += (taglen + lenlen + len);
	}

	/* we're done with this seqorset, so free it up */
	LBER_FREE( (char *) (*sos) );
	*sos = next;

	return( taglen + lenlen + len );
}

int
ber_put_seq( BerElement *ber )
{
	assert( ber != NULL );
	assert( BER_VALID( ber ) );

	return( ber_put_seqorset( ber ) );
}

int
ber_put_set( BerElement *ber )
{
	assert( ber != NULL );
	assert( BER_VALID( ber ) );

	return( ber_put_seqorset( ber ) );
}

/* VARARGS */
int
ber_printf
#ifdef HAVE_STDARG
	( BerElement *ber,
	LDAP_CONST char *fmt,
	... )
#else
	( va_alist )
va_dcl
#endif
{
	va_list		ap;
#ifndef HAVE_STDARG
	BerElement	*ber;
	char		*fmt;
#endif
	char		*s, **ss;
	struct berval	*bv, **bvp;
	int		rc;
	ber_int_t	i;
	ber_len_t	len;

#ifdef HAVE_STDARG
	va_start( ap, fmt );
#else
	va_start( ap );
	ber = va_arg( ap, BerElement * );
	fmt = va_arg( ap, char * );
#endif

	assert( ber != NULL );
	assert( fmt != NULL );

	assert( BER_VALID( ber ) );

	for ( rc = 0; *fmt && rc != -1; fmt++ ) {
		switch ( *fmt ) {
		case '!': { /* hook */
				BEREncodeCallback *f;
				void *p;

				f = va_arg( ap, BEREncodeCallback * );
				p = va_arg( ap, void * );

				rc = (*f)( ber, p );
			} break;

		case 'b':	/* boolean */
			i = va_arg( ap, ber_int_t );
			rc = ber_put_boolean( ber, i, ber->ber_tag );
			break;

		case 'i':	/* int */
			i = va_arg( ap, ber_int_t );
			rc = ber_put_int( ber, i, ber->ber_tag );
			break;

		case 'e':	/* enumeration */
			i = va_arg( ap, ber_int_t );
			rc = ber_put_enum( ber, i, ber->ber_tag );
			break;

		case 'n':	/* null */
			rc = ber_put_null( ber, ber->ber_tag );
			break;

		case 'o':	/* octet string (non-null terminated) */
			s = va_arg( ap, char * );
			len = va_arg( ap, ber_len_t );
			rc = ber_put_ostring( ber, s, len, ber->ber_tag );
			break;

		case 'O':	/* berval octet string */
			bv = va_arg( ap, struct berval * );
			if( bv == NULL ) break;
			rc = ber_put_berval( ber, bv, ber->ber_tag );
			break;

		case 's':	/* string */
			s = va_arg( ap, char * );
			rc = ber_put_string( ber, s, ber->ber_tag );
			break;

		case 'B':	/* bit string */
			s = va_arg( ap, char * );
			len = va_arg( ap, int );	/* in bits */
			rc = ber_put_bitstring( ber, s, len, ber->ber_tag );
			break;

		case 't':	/* tag for the next element */
			ber->ber_tag = va_arg( ap, ber_tag_t );
			ber->ber_usertag = 1;
			break;

		case 'v':	/* vector of strings */
			if ( (ss = va_arg( ap, char ** )) == NULL )
				break;
			for ( i = 0; ss[i] != NULL; i++ ) {
				if ( (rc = ber_put_string( ber, ss[i],
				    ber->ber_tag )) == -1 )
					break;
			}
			break;

		case 'V':	/* sequences of strings + lengths */
			if ( (bvp = va_arg( ap, struct berval ** )) == NULL )
				break;
			for ( i = 0; bvp[i] != NULL; i++ ) {
				if ( (rc = ber_put_berval( ber, bvp[i],
				    ber->ber_tag )) == -1 )
					break;
			}
			break;

		case '{':	/* begin sequence */
			rc = ber_start_seq( ber, ber->ber_tag );
			break;

		case '}':	/* end sequence */
			rc = ber_put_seqorset( ber );
			break;

		case '[':	/* begin set */
			rc = ber_start_set( ber, ber->ber_tag );
			break;

		case ']':	/* end set */
			rc = ber_put_seqorset( ber );
			break;

		default:
			if( ber->ber_debug ) {
				ber_log_printf( LDAP_DEBUG_ANY, ber->ber_debug,
					"ber_printf: unknown fmt %c\n", *fmt );
			}
			rc = -1;
			break;
		}

		if ( ber->ber_usertag == 0 )
			ber->ber_tag = LBER_DEFAULT;
		else
			ber->ber_usertag = 0;
	}

	va_end( ap );

	return( rc );
}
