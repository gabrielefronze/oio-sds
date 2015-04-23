/*
OpenIO SDS metautils
Copyright (C) 2014 Worldine, original work as part of Redcurrant
Copyright (C) 2015 OpenIO, modified as part of OpenIO Software Defined Storage

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 3.0 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library.
*/

#ifndef G_LOG_DOMAIN
# define G_LOG_DOMAIN "metautils.tools"
#endif

#include <ctype.h>
#include <openssl/sha.h>

#include "metautils.h"
#include "metautils_internals.h"

static gchar b2h[][2] =
{
	{'0','0'}, {'0','1'}, {'0','2'}, {'0','3'}, {'0','4'}, {'0','5'}, {'0','6'}, {'0','7'},
	{'0','8'}, {'0','9'}, {'0','A'}, {'0','B'}, {'0','C'}, {'0','D'}, {'0','E'}, {'0','F'},
	{'1','0'}, {'1','1'}, {'1','2'}, {'1','3'}, {'1','4'}, {'1','5'}, {'1','6'}, {'1','7'},
	{'1','8'}, {'1','9'}, {'1','A'}, {'1','B'}, {'1','C'}, {'1','D'}, {'1','E'}, {'1','F'},
	{'2','0'}, {'2','1'}, {'2','2'}, {'2','3'}, {'2','4'}, {'2','5'}, {'2','6'}, {'2','7'},
	{'2','8'}, {'2','9'}, {'2','A'}, {'2','B'}, {'2','C'}, {'2','D'}, {'2','E'}, {'2','F'},
	{'3','0'}, {'3','1'}, {'3','2'}, {'3','3'}, {'3','4'}, {'3','5'}, {'3','6'}, {'3','7'},
	{'3','8'}, {'3','9'}, {'3','A'}, {'3','B'}, {'3','C'}, {'3','D'}, {'3','E'}, {'3','F'},
	{'4','0'}, {'4','1'}, {'4','2'}, {'4','3'}, {'4','4'}, {'4','5'}, {'4','6'}, {'4','7'},
	{'4','8'}, {'4','9'}, {'4','A'}, {'4','B'}, {'4','C'}, {'4','D'}, {'4','E'}, {'4','F'},
	{'5','0'}, {'5','1'}, {'5','2'}, {'5','3'}, {'5','4'}, {'5','5'}, {'5','6'}, {'5','7'},
	{'5','8'}, {'5','9'}, {'5','A'}, {'5','B'}, {'5','C'}, {'5','D'}, {'5','E'}, {'5','F'},
	{'6','0'}, {'6','1'}, {'6','2'}, {'6','3'}, {'6','4'}, {'6','5'}, {'6','6'}, {'6','7'},
	{'6','8'}, {'6','9'}, {'6','A'}, {'6','B'}, {'6','C'}, {'6','D'}, {'6','E'}, {'6','F'},
	{'7','0'}, {'7','1'}, {'7','2'}, {'7','3'}, {'7','4'}, {'7','5'}, {'7','6'}, {'7','7'},
	{'7','8'}, {'7','9'}, {'7','A'}, {'7','B'}, {'7','C'}, {'7','D'}, {'7','E'}, {'7','F'},
	{'8','0'}, {'8','1'}, {'8','2'}, {'8','3'}, {'8','4'}, {'8','5'}, {'8','6'}, {'8','7'},
	{'8','8'}, {'8','9'}, {'8','A'}, {'8','B'}, {'8','C'}, {'8','D'}, {'8','E'}, {'8','F'},
	{'9','0'}, {'9','1'}, {'9','2'}, {'9','3'}, {'9','4'}, {'9','5'}, {'9','6'}, {'9','7'},
	{'9','8'}, {'9','9'}, {'9','A'}, {'9','B'}, {'9','C'}, {'9','D'}, {'9','E'}, {'9','F'},
	{'A','0'}, {'A','1'}, {'A','2'}, {'A','3'}, {'A','4'}, {'A','5'}, {'A','6'}, {'A','7'},
	{'A','8'}, {'A','9'}, {'A','A'}, {'A','B'}, {'A','C'}, {'A','D'}, {'A','E'}, {'A','F'},
	{'B','0'}, {'B','1'}, {'B','2'}, {'B','3'}, {'B','4'}, {'B','5'}, {'B','6'}, {'B','7'},
	{'B','8'}, {'B','9'}, {'B','A'}, {'B','B'}, {'B','C'}, {'B','D'}, {'B','E'}, {'B','F'},
	{'C','0'}, {'C','1'}, {'C','2'}, {'C','3'}, {'C','4'}, {'C','5'}, {'C','6'}, {'C','7'},
	{'C','8'}, {'C','9'}, {'C','A'}, {'C','B'}, {'C','C'}, {'C','D'}, {'C','E'}, {'C','F'},
	{'D','0'}, {'D','1'}, {'D','2'}, {'D','3'}, {'D','4'}, {'D','5'}, {'D','6'}, {'D','7'},
	{'D','8'}, {'D','9'}, {'D','A'}, {'D','B'}, {'D','C'}, {'D','D'}, {'D','E'}, {'D','F'},
	{'E','0'}, {'E','1'}, {'E','2'}, {'E','3'}, {'E','4'}, {'E','5'}, {'E','6'}, {'E','7'},
	{'E','8'}, {'E','9'}, {'E','A'}, {'E','B'}, {'E','C'}, {'E','D'}, {'E','E'}, {'E','F'},
	{'F','0'}, {'F','1'}, {'F','2'}, {'F','3'}, {'F','4'}, {'F','5'}, {'F','6'}, {'F','7'},
	{'F','8'}, {'F','9'}, {'F','A'}, {'F','B'}, {'F','C'}, {'F','D'}, {'F','E'}, {'F','F'}
};

static gchar hexa[] =
{
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, -1, -1, -1, -1, -1, -1,
	-1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

/* ------------------------------------------------------------------------- */

static gsize
_buffer2str(const guint8 *s, size_t sS, char *d, size_t dS)
{
	gsize i, j;

	if (!s || !sS || !d || !dS)
		return 0;

	for (i=j=0; i<sS && j<(dS-1) ;i++) {
		register const gchar *h = b2h[((guint8*)s)[i]];
		d[j++] = h[0];
		d[j++] = h[1];
	}

	d[(j<dS ? j : dS-1)] = 0;

	return j;
}

void
buffer2str(const void *s, size_t sS, char *d, size_t dS)
{
	(void) _buffer2str(s, sS, d, dS);
}

gsize
container_id_to_string(const container_id_t id, gchar * dst, gsize dstsize)
{
	return _buffer2str(id, sizeof(container_id_t), dst, dstsize);
}

void
meta1_name2hash(container_id_t cid, const char *ns, const char *account, const char *user)
{
	EXTRA_ASSERT (ns != NULL);
	EXTRA_ASSERT (*ns != 0);
	EXTRA_ASSERT (user != NULL);
	EXTRA_ASSERT (*user != 0);

	guint8 zero = 0;
	GChecksum *sum = g_checksum_new(G_CHECKSUM_SHA256);

	if (account && 0 != strcmp(account, HCURL_DEFAULT_ACCOUNT)) {
		g_checksum_update(sum, (guint8*)account, strlen(account));
		g_checksum_update(sum, &zero, 1);
	}
	g_checksum_update(sum, (guint8*)user, strlen(user));

	memset(cid, 0, sizeof(container_id_t));
	gsize s = sizeof(container_id_t);
	g_checksum_get_digest(sum, (guint8*)cid, &s);
	g_checksum_free(sum);
}

static gboolean
_hex2bin(const guint8 *s, gsize sS, guint8 *d, register gsize dS, GError** error)
{
	if (!s || !d) {
		GSETERROR(error, "src or dst is null");
		return FALSE;
	}

	if (sS < dS * 2) {
		GSETERROR(error, "hexadecimal form too short");
		return FALSE;
	}

	while ((dS--) > 0) {
		register int i0, i1;

		i0 = hexa[*(s++)];
		i1 = hexa[*(s++)];

		if (i0<0 || i1<0) {
			GSETERROR(error, "Invalid hex");
			return FALSE;
		}

		*(d++) = (i0 & 0x0F) << 4 | (i1 & 0x0F);
	}

	return TRUE;
}

gboolean
hex2bin(const gchar *s, void *d, gsize dS, GError** error)
{
	return _hex2bin((guint8*)s, (s?strlen(s):0), (guint8*)d, dS, error);
}

gboolean
container_id_hex2bin(const gchar *s, gsize sS, container_id_t *d,
		GError ** error)
{
	return _hex2bin((guint8*)s, sS, (guint8*)d, 32, error);
}

guint
container_id_hash(gconstpointer k)
{
	const guint *b;
	guint max, i, h;

	if (!k)
		return 0;
	b = k;
	max = sizeof(container_id_t) / sizeof(guint);
	h = 0;
	for (i = 0; i < max; i++)
		h = h ^ b[i];
	return h;
}

gboolean
container_id_equal(gconstpointer k1, gconstpointer k2)
{
	return k1 && k2 && ((k1 == k2)
	    || (0 == memcmp(k1, k2, sizeof(container_id_t))));
}

void g_free0(gpointer p) { if (p) g_free(p); }
void g_free1(gpointer p1, gpointer p2) { (void) p2; g_free0(p1); }
void g_free2(gpointer p1, gpointer p2) { (void) p1; g_free0(p2); }

/* ----------------------------------------------------------------------------------- */

gboolean
convert_chunk_text_to_raw(const struct chunk_textinfo_s* text_chunk, struct meta2_raw_chunk_s* raw_chunk, GError** error)
{
	if (text_chunk == NULL) {
		GSETERROR(error, "text_chunk is null");
		return FALSE;
	}

	memset(raw_chunk, 0, sizeof(struct meta2_raw_chunk_s));

	if (text_chunk->id != NULL
		&& !hex2bin(text_chunk->id, &(raw_chunk->id.id), sizeof(hash_sha256_t), error)) {
			GSETERROR(error, "Failed to convert chunk id from hex to bin");
			return FALSE;
	}

	if (text_chunk->hash != NULL
		&& !hex2bin(text_chunk->hash, &(raw_chunk->hash), sizeof(chunk_hash_t), error)) {
			GSETERROR(error, "Failed to convert chunk hash from hex to bin");
			return FALSE;
	}

	if (text_chunk->size != NULL)
		raw_chunk->size = g_ascii_strtoll(text_chunk->size, NULL, 10);

	if (text_chunk->position != NULL)
		raw_chunk->position = g_ascii_strtoull(text_chunk->position, NULL, 10);

	if (text_chunk->metadata != NULL)
		raw_chunk->metadata = metautils_gba_from_string(text_chunk->metadata);

	return TRUE;
}

gchar*
key_value_pair_to_string(key_value_pair_t * kv)
{
        gchar *str_value = NULL, *result = NULL;
        gsize str_value_len;

        if (!kv)
                return g_strdup("KeyValue|NULL|NULL");

        if (!kv->value)
                return g_strconcat("KeyValue|",(kv->key?kv->key:"NULL"),"|NULL", NULL);

        str_value_len = 8 + 3 * kv->value->len;
        str_value = g_malloc0(str_value_len);
        metautils_gba_data_to_string(kv->value, str_value, str_value_len);

        result = g_strconcat("KeyValue|",(kv->key?kv->key:"NULL"), "|", str_value, NULL);
        g_free(str_value);

        return result;
}

gsize
metautils_strlcpy_physical_ns(gchar *d, const gchar *s, gsize dlen)
{
    register gsize count = 0;

	if (dlen > 0) {
		-- dlen; // Keep one place for the trailing '\0'
	    for (; count<dlen && *s && *s != '.' ;count++)
			*(d++) = *(s++);
		if (dlen)
			*d = '\0';
	}

    for (; *s && *s != '.' ;count++,s++) { }
    return count;
}

void
metautils_randomize_buffer(guint8 *buf, gsize buflen)
{
	union {
		guint32 r32;
		guint8 r8[4];
	} raw;
	GRand *r = g_rand_new();

	if (NULL == buf || 0 == buflen)
		return;

	// Fill 4 by 4
	gsize mod32 = buflen % 4;
	gsize max32 = buflen / 4;
	for (register gsize i32=0; i32 < max32 ; ++i32) {
		raw.r32 = g_rand_int(r);
		((guint32*)buf)[i32] = raw.r32;
	}

	// Finish with the potentially remaining unset bytes
	raw.r32 = g_rand_int(r);
	switch (mod32) {
		case 3:
			buf[ (max32*4) + 2 ] = raw.r8[2];
		case 2:
			buf[ (max32*4) + 1 ] = raw.r8[1];
		case 1:
			buf[ (max32*4) + 0 ] = raw.r8[0];
	}

	g_rand_free(r);
}

