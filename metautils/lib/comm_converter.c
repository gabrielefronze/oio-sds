/*
OpenIO SDS metautils
Copyright (C) 2014 Worldine, original work as part of Redcurrant
Copyright (C) 2015-2016 OpenIO, modified as part of OpenIO SDS

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

#include "metautils.h"
#include "codec.h"

/* -------------------------------------------------------------------------- */

typedef gboolean(*abstract_converter_f) (const void *in, void *out);

typedef void (*abstract_asn_cleaner_f) (void *asn, gboolean only_content);

typedef void (*abstract_api_cleaner_f) (void *api);

struct abstract_sequence_handler_s
{
	size_t asn1_size;                     /**< sizeof() on the ASN structure */
	size_t api_size;                      /**< sizeof() on the API structure */
	void *asn1_descriptor;                /**< pointer to the descriptor generated by asn1c */
	abstract_converter_f map_ASN1_to_API; /**< conversion from API to ASN */
	abstract_converter_f map_API_to_ASN1; /**< conversion from ASN to API */
	abstract_asn_cleaner_f clean_ASN1;    /**< structure cleaner */
	abstract_api_cleaner_f clean_API;     /**< structure cleaner */
	const gchar * const type_name;        /**< Type name used in error messages */
};

#define DEFINE_SEQUENCE_MARSHALLER_GBA(Descr,Name) \
GByteArray* Name (GSList *list, GError **err) {\
GByteArray *gba = abstract_sequence_marshall(Descr, list, err);\
	if (!gba) { GSETERROR(err,"Serialization error"); return 0; }\
	return gba;\
}

#define DEFINE_SEQUENCE_UNMARSHALLER(Descr,Name) \
gint Name (GSList **list, const void *buf, gsize len, GError **err) {\
	if (!list || !buf || !len) {\
		GSETERROR(err,"Invalid parameter (list=%p src=%p/%"G_GSIZE_FORMAT")", list, buf, len);\
		return -1;\
	}\
	return 0 < abstract_sequence_unmarshall(Descr, list, buf, len, err);\
}

struct anonymous_sequence_s
{
	asn_anonymous_set_ list;
	asn_struct_ctx_t _asn_ctx;
};

static void api_gclean(gpointer p1, gpointer p2)
{
	abstract_api_cleaner_f cleanAPI;
	if (!p1) return;
	cleanAPI = p2;
	cleanAPI(p1);
}

static gssize
abstract_sequence_unmarshall(const struct abstract_sequence_handler_s *h,
		GSList ** list, const void *asn1_encoded, gsize asn1_encoded_size,
		GError ** err)
{
	gssize consumed;
	void *result = NULL;
	gint i = 0, max = 0;
	asn_dec_rval_t decRet;
	struct anonymous_sequence_s *abstract_sequence;
	GSList *api_result = NULL;

	void func_free(void *d)
	{
		if (!d)
			return;
		h->clean_ASN1(d, FALSE);
	}

	if (!asn1_encoded || !list) {
		GSETERROR(err, "Invalid parameter");
		return -1;
	}

	asn_codec_ctx_t codecCtx = {0};
	codecCtx.max_stack_size = ASN1C_MAX_STACK;
	decRet = ber_decode(&codecCtx, h->asn1_descriptor, &(result), asn1_encoded, asn1_encoded_size);

	switch (decRet.code) {
	case RC_OK:
		abstract_sequence = (struct anonymous_sequence_s *) result;

		/*fill the list with the content of the array */
		for (i = 0, max = abstract_sequence->list.count; i < max; i++) {
			void *api_structure;

			api_structure = g_malloc0(h->api_size);
			if (!h->map_ASN1_to_API(abstract_sequence->list.array[i], api_structure)) {
				GSETERROR(err,"Element of type [%s] ASN-to-API conversion error", h->type_name);

				if (api_structure)
					h->clean_API(api_structure);

				abstract_sequence->list.free = &func_free;
				asn_set_empty(abstract_sequence);
				ASN1C_FREE(abstract_sequence);
				abstract_sequence = NULL;

				if (api_result) {
					g_slist_foreach(api_result, api_gclean, h->clean_API);
					g_slist_free(api_result);
				}
				return -1;
			}
			api_result = g_slist_prepend(api_result, api_structure);
		}

		abstract_sequence->list.free = &func_free;
		asn_set_empty(abstract_sequence);
		ASN1C_FREE(abstract_sequence);
		abstract_sequence = NULL;

		*list = metautils_gslist_precat(*list, api_result);
		consumed = decRet.consumed;
		return consumed;

	case RC_FAIL:
		GSETERROR(err, "sequence unmarshalling error (%"G_GSIZE_FORMAT" consumed)", decRet.consumed);
		return -1;

	case RC_WMORE:
		GSETERROR(err, "sequence unmarshalling error (uncomplete)");
		return 0;
	default:
		GSETERROR(err, "Serialisation produced an unknow return code : %d", decRet.code);
		return -1;
	}

	return -1;
}

static GByteArray *
abstract_sequence_marshall(const struct abstract_sequence_handler_s * h, GSList * api_sequence, GError ** err)
{
	gboolean error_occured = FALSE;
	gsize probable_size;
	asn_enc_rval_t encRet;
	GByteArray *gba;

	int func_write(const void *b, gsize bSize, void *key)
	{
		(void) key;
		return g_byte_array_append(gba, (guint8 *) b, bSize) ? 0 : -1;
	}

	void func_free(void *d)
	{
		if (!d)
			return;
		h->clean_ASN1(d, FALSE);
	}

	void func_fill(gpointer d, gpointer u)
	{
		asn_anonymous_set_ *p_set;
		void *asn1_form;

		if (error_occured || !d)
			return;
		asn1_form = ASN1C_CALLOC(1, h->asn1_size);
		if (!h->map_API_to_ASN1(d, asn1_form)) {
			ASN1C_FREE(asn1_form);
			GSETERROR(err, "Element of type [%s] serialization failed!", h->type_name);
			error_occured = TRUE;
		} else {
			p_set = &(((struct anonymous_sequence_s *) u)->list);
			asn_set_add(_A_SET_FROM_VOID(p_set), asn1_form);
		}
	}

	probable_size = g_slist_length(api_sequence) * (h->asn1_size + 6) + 64;
	probable_size = MIN(probable_size, 4096);

	gba = g_byte_array_sized_new(probable_size);
	if (!gba) {
		GSETERROR(err, "Memory allocation failure");
		return NULL;
	}

	/*fills the ASN.1 structure */
	struct anonymous_sequence_s asnSeq = {{0}};
	g_slist_foreach(api_sequence, &func_fill, &asnSeq);
	if (error_occured) {
		g_byte_array_free(gba, TRUE);
		GSETERROR(err, "list serialisation error");
		return NULL;
	}

	/*serializes the structure */
	encRet = der_encode(h->asn1_descriptor, &asnSeq, func_write, NULL);
	if (encRet.encoded == -1) {
		GSETERROR(err, "Cannot encode the ASN.1 sequence (error on %s)", encRet.failed_type->name);
		g_byte_array_free(gba, TRUE);
		asnSeq.list.free = &func_free;
		asn_set_empty(&(asnSeq.list));
		return NULL;
	}

	/*free the ASN.1 structure and the working buffer */
	asnSeq.list.free = &func_free;
	asn_set_empty(&asnSeq);
	return gba;
}

/* -------------------------------------------------------------------------- */

static gboolean
key_value_pair_API2ASN(const key_value_pair_t * api, Parameter_t * asn)
{
	EXTRA_ASSERT (asn != NULL);
	EXTRA_ASSERT (api != NULL);
	memset(asn, 0x00, sizeof(Parameter_t));
	OCTET_STRING_fromBuf(&(asn->name), api->key, strlen(api->key));
	OCTET_STRING_fromBuf(&(asn->value), (const char*)api->value->data, api->value->len);
	return TRUE;
}

static void
key_value_pair_cleanASN(Parameter_t * asn, gboolean only_content)
{
	if (!asn)
		return;
	if (only_content)
		ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_Parameter, asn);
	else
		ASN_STRUCT_FREE(asn_DEF_Parameter, asn);
}

static void
free_Parameter(Parameter_t * asn_param)
{
	key_value_pair_cleanASN(asn_param, FALSE);
}

/* -------------------------------------------------------------------------- */

static gboolean
addr_info_ASN2API(const AddrInfo_t * asn, addr_info_t * api)
{
	EXTRA_ASSERT (api != NULL);
	EXTRA_ASSERT (asn != NULL);

	guint16 port16 = 0;
	asn_INTEGER_to_uint16(&asn->port, &port16);
	api->port = g_ntohs(port16);

	switch (asn->ip.present) {
		case AddrInfo__ip_PR_ipv4:
			api->type = TADDR_V4;
			if (asn->ip.choice.ipv4.buf)
				memcpy(&(api->addr), asn->ip.choice.ipv4.buf, MIN(4, asn->ip.choice.ipv4.size));
			break;
		case AddrInfo__ip_PR_ipv6:
			api->type = TADDR_V6;
			if (asn->ip.choice.ipv6.buf)
				memcpy(&(api->addr), asn->ip.choice.ipv6.buf, MIN(16, asn->ip.choice.ipv6.size));
			break;
		case AddrInfo__ip_PR_NOTHING:
			return FALSE;
	}

	return TRUE;
}

static gboolean
addr_info_API2ASN(const addr_info_t * api, AddrInfo_t * asn)
{
	EXTRA_ASSERT (api != NULL);
	EXTRA_ASSERT (asn != NULL);

	asn_uint16_to_INTEGER(&asn->port, g_htons(api->port));
	asn->ip.present = AddrInfo__ip_PR_NOTHING;

	switch (api->type) {
	case TADDR_V4:
		OCTET_STRING_fromBuf(&(asn->ip.choice.ipv4), (char *) &(api->addr), 4);
		asn->ip.present = AddrInfo__ip_PR_ipv4;
		break;
	case TADDR_V6:
		OCTET_STRING_fromBuf(&(asn->ip.choice.ipv6), (char *) &(api->addr), 16);
		asn->ip.present = AddrInfo__ip_PR_ipv6;
		break;
	default:
		g_assert_not_reached();
		return FALSE;
	}

	return TRUE;
}

/* -------------------------------------------------------------------------- */

static gboolean
score_ASN2API(const Score_t * asn, score_t * api)
{
	EXTRA_ASSERT (api != NULL);
	EXTRA_ASSERT (asn != NULL);
	asn_INTEGER_to_int32(&(asn->value), &(api->value));
	asn_INTEGER_to_int32(&(asn->timestamp), &(api->timestamp));
	return TRUE;
}

static gboolean
score_API2ASN(const score_t * api, Score_t * asn)
{
	EXTRA_ASSERT (api != NULL);
	EXTRA_ASSERT (asn != NULL);
	asn_int32_to_INTEGER(&(asn->value), api->value);
	asn_int32_to_INTEGER(&(asn->timestamp), api->timestamp);
	return TRUE;
}

static gboolean
service_tag_ASN2API(ServiceTag_t * asn, service_tag_t * api)
{
	if (!api || !asn)
		return FALSE;

	memset(api, 0x00, sizeof(service_tag_t));

	/*name */
	memcpy(api->name, asn->name.buf, MIN(asn->name.size, (int)sizeof(api->name)));

	/*value */
	switch (asn->value.present) {
		case ServiceTag__value_PR_b:
			api->type = STVT_BOOL;
			api->value.b = asn->value.choice.b;
			return TRUE;
		case ServiceTag__value_PR_i:
			api->type = STVT_I64;
			asn_INTEGER_to_int64(&(asn->value.choice.i), &(api->value.i));
			return TRUE;
		case ServiceTag__value_PR_r:
			api->type = STVT_REAL;
			asn_REAL2double(&(asn->value.choice.r), &(api->value.r));
			return TRUE;
		case ServiceTag__value_PR_s:
			api->type = STVT_STR;
			api->value.s = g_strndup((const gchar*)asn->value.choice.s.buf, asn->value.choice.s.size);
			return TRUE;
		case ServiceTag__value_PR_NOTHING:
			return FALSE;
	}
	return FALSE;
}

static gboolean
service_info_ASN2API(ServiceInfo_t * asn, service_info_t * api)
{
	if (!api || !asn)
		return FALSE;

	memset(api, 0x00, sizeof(service_info_t));

	/*header */
	memcpy(api->ns_name, asn->nsName.buf, MIN(asn->nsName.size, (int)sizeof(api->ns_name)));
	memcpy(api->type, asn->type.buf, MIN(asn->type.size, (int)sizeof(api->type)));
	addr_info_ASN2API(&(asn->addr), &(api->addr));
	score_ASN2API(&asn->score, &api->score);

	/*tags */
	if (!asn->tags)
		api->tags = g_ptr_array_new();
	else {
		api->tags = g_ptr_array_sized_new(asn->tags->list.count);
		for (int i = 0, max = asn->tags->list.count; i < max; i++) {
			service_tag_t *api_tag = g_malloc0(sizeof(service_tag_t));
			ServiceTag_t *asn_tag = asn->tags->list.array[i];
			service_tag_ASN2API(asn_tag, api_tag);
			g_ptr_array_add(api->tags, api_tag);
		}
	}

	return TRUE;
}

static gboolean
service_tag_API2ASN(service_tag_t * api, ServiceTag_t * asn)
{
	gsize name_len;

	if (!api || !asn) {
		return FALSE;
	}

	memset(asn, 0x00, sizeof(ServiceTag_t));

	/*name */
	name_len = strnlen(api->name, sizeof(api->name));
	OCTET_STRING_fromBuf(&(asn->name), api->name, name_len);

	/*value */
	switch (api->type) {
	case STVT_STR:
		asn->value.present = ServiceTag__value_PR_s;
		OCTET_STRING_fromBuf(&(asn->value.choice.s), api->value.s, strlen(api->value.s));
		break;
	case STVT_BUF:
		asn->value.present = ServiceTag__value_PR_s;
		OCTET_STRING_fromBuf(&(asn->value.choice.s), api->value.buf, strnlen(api->value.buf,
			sizeof(api->value.buf)));
		break;
	case STVT_REAL:
		asn->value.present = ServiceTag__value_PR_r;
		asn_double2REAL(&(asn->value.choice.r), api->value.r);
		break;
	case STVT_I64:
		asn->value.present = ServiceTag__value_PR_i;
		asn_int64_to_INTEGER(&(asn->value.choice.i), api->value.i);
		break;
	case STVT_BOOL:
		asn->value.present = ServiceTag__value_PR_b;
		asn->value.choice.b = api->value.b;
		break;
	}
	return TRUE;
}

static gboolean
service_info_API2ASN(service_info_t * api, ServiceInfo_t * asn)
{
	if (!api || !asn)
		return FALSE;

	memset(asn, 0x00, sizeof(ServiceInfo_t));

	/*header */
	OCTET_STRING_fromBuf(&(asn->type), api->type, strnlen(api->type, sizeof(api->type)));
	OCTET_STRING_fromBuf(&(asn->nsName), api->ns_name, strnlen(api->ns_name, sizeof(api->ns_name)));
	addr_info_API2ASN(&(api->addr), &(asn->addr));
	score_API2ASN(&api->score, &asn->score);

	/*tags */
	if (api->tags) {
		service_tag_t *api_tag;
		ServiceTag_t *asn_tag;
		int i, max;

		/*init the array */
		asn->tags = ASN1C_CALLOC(1, sizeof(struct ServiceInfo__tags));

		/*fill the array */
		for (max = api->tags->len, i = 0; i < max; i++) {
			api_tag = (service_tag_t *) g_ptr_array_index(api->tags, i);
			if (!api_tag)
				continue;
			asn_tag = ASN1C_CALLOC(1, sizeof(ServiceTag_t));
			if (!asn_tag)
				continue;
			service_tag_API2ASN(api_tag, asn_tag);
			asn_set_add(&(asn->tags->list), asn_tag);
		}
	}

	return TRUE;
}

static void
free_service_tag_ASN(ServiceTag_t * tag)
{
	if (!tag)
		return;
	asn_DEF_ServiceTag.free_struct(&asn_DEF_ServiceTag, tag, 0);
}

static void
service_info_cleanASN(ServiceInfo_t * asn, gboolean only_content)
{
	if (!asn)
		return;

	if (asn->tags)
		asn->tags->list.free = free_service_tag_ASN;

	if (only_content)
		ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_ServiceInfo, asn);
	else
		ASN_STRUCT_FREE(asn_DEF_ServiceInfo, asn);
}

static const struct abstract_sequence_handler_s descr_ServiceInfo =
{
	sizeof(ServiceInfo_t),
	sizeof(service_info_t),
	&asn_DEF_ServiceInfoSequence,
	(abstract_converter_f) service_info_ASN2API,
	(abstract_converter_f) service_info_API2ASN,
	(abstract_asn_cleaner_f) service_info_cleanASN,
	(abstract_api_cleaner_f) service_info_clean,
	"service_info"
};

GByteArray*
service_info_marshall_1(service_info_t *si, GError **err)
{
	ServiceInfo_t asn;
	asn_enc_rval_t encRet;
	GByteArray *gba;

	if (!si) {
		GSETERROR(err, "invalid parameter");
		return NULL;
	}

	if (!service_info_API2ASN(si, &asn))
		GRID_ERROR("Conversion error");

	gba = g_byte_array_sized_new(64);
	encRet = der_encode(&asn_DEF_ServiceInfo, &asn, metautils_asn1c_write_gba, gba);
	service_info_cleanASN(&asn, TRUE);

	if (encRet.encoded == -1) {
		GSETERROR(err, "Serialization error on '%s'",
				encRet.failed_type->name);
		g_byte_array_free(gba, TRUE);
		return NULL;
	}

	return gba;
}

DEFINE_SEQUENCE_UNMARSHALLER(&descr_ServiceInfo, service_info_unmarshall);
DEFINE_SEQUENCE_MARSHALLER_GBA(&descr_ServiceInfo, service_info_marshall_gba);

/* -------------------------------------------------------------------------- */

static gboolean
meta0_info_ASN2API(const Meta0Info_t * asn, meta0_info_t * api)
{
	EXTRA_ASSERT (api != NULL);
	EXTRA_ASSERT (asn != NULL);
	memset(api, 0x00, sizeof(meta0_info_t));
	api->prefixes_size = asn->prefix.size;
	api->prefixes = g_malloc(api->prefixes_size);
	memcpy(api->prefixes, asn->prefix.buf, asn->prefix.size);
	addr_info_ASN2API(&(asn->addr), &(api->addr));
	return TRUE;
}

static gboolean
meta0_info_API2ASN(const meta0_info_t * api, Meta0Info_t * asn)
{
	EXTRA_ASSERT (api != NULL);
	EXTRA_ASSERT (asn != NULL);
	memset(asn, 0x00, sizeof(Meta0Info_t));
	OCTET_STRING_fromBuf(&(asn->prefix), (char *) api->prefixes, api->prefixes_size);
	addr_info_API2ASN(&(api->addr), &(asn->addr));
	return TRUE;
}

static void
meta0_info_cleanASN(Meta0Info_t * asn, gboolean only_content)
{
	if (!asn)
		return;
	if (only_content)
		ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_Meta0Info, asn);
	else
		ASN_STRUCT_FREE(asn_DEF_Meta0Info, asn);
}

static const struct abstract_sequence_handler_s descr_Meta0Info =
{
	sizeof(Meta0Info_t),
	sizeof(meta0_info_t),
	&asn_DEF_Meta0InfoSequence,
	(abstract_converter_f) meta0_info_ASN2API,
	(abstract_converter_f) meta0_info_API2ASN,
	(abstract_asn_cleaner_f) meta0_info_cleanASN,
	(abstract_api_cleaner_f) meta0_info_clean,
	"meta0_info"
};

DEFINE_SEQUENCE_MARSHALLER_GBA(&descr_Meta0Info, meta0_info_marshall_gba)
DEFINE_SEQUENCE_UNMARSHALLER(&descr_Meta0Info, meta0_info_unmarshall)

/* NSINFO ------------------------------------------------------------------- */

static GHashTable*
list_conversion (const struct ParameterSequence *vl)
{
	EXTRA_ASSERT (vl != NULL);

	GHashTable *ht = g_hash_table_new_full(g_str_hash, g_str_equal,
			g_free, metautils_gba_clean);

	if (vl->list.count > 0) {
		for (int i = 0; i < vl->list.count; i++) {
			Parameter_t* p;
			if (!(p = vl->list.array[i]))
				continue;
			const gsize len = p->value.size;
			GByteArray *v = g_byte_array_sized_new(len);
			v = g_byte_array_append(v, p->value.buf, len);
			g_hash_table_insert (ht, g_strndup((gchar*)p->name.buf, p->name.size), v);
		}
	}

	return ht;
}

static gboolean
namespace_info_ASN2API(const NamespaceInfo_t *asn, namespace_info_t *api)
{
	EXTRA_ASSERT (api != NULL);
	EXTRA_ASSERT (asn != NULL);

	memset(api, 0, sizeof(*api));
	memcpy(api->name, asn->name.buf, MIN((int)sizeof(api->name), asn->name.size));

	asn_INTEGER_to_int64(&(asn->chunkSize), &(api->chunk_size));

	api->options = list_conversion(&(asn->options));
	api->storage_policy = list_conversion(&(asn->storagePolicy));
	api->data_security = list_conversion(&(asn->dataSecurity));
	api->service_pools = list_conversion(&(asn->servicePools));
	return TRUE;
}

static gboolean
hashtable_conversion(GHashTable *ht,
		struct ParameterSequence *nsinfo_vlist,
		GSList* (*conv_func)(GHashTable *, gboolean, GError **))
{
	EXTRA_ASSERT (ht != NULL);
	EXTRA_ASSERT (nsinfo_vlist != NULL);
	EXTRA_ASSERT (conv_func != NULL);

	GError *error = NULL;
	GSList* result = conv_func(ht, TRUE, &error);
	if (result == NULL && error != NULL) {
		ERROR("Failed to convert map to key_value_pairs in namespace_info API to ASN conversion : %s",
				gerror_get_message(error));
		g_clear_error(&error);
		return FALSE;
	}

	if (result != NULL) {
		/* fill the array */
		for (GSList *p = result; p != NULL; p = p->next) {
			key_value_pair_t* api_prop;
			if (!(api_prop = (key_value_pair_t*)p->data))
				continue;
			Parameter_t* asn_prop = ASN1C_CALLOC(1, sizeof(Parameter_t));
			key_value_pair_API2ASN(api_prop, asn_prop);
			asn_set_add(&(nsinfo_vlist->list), asn_prop);
		}

		/* free the temp list */
		g_slist_foreach(result, key_value_pair_gclean, NULL);
		g_slist_free(result);
	}

	return TRUE;
}

static gboolean
namespace_info_API2ASN(const namespace_info_t * api, NamespaceInfo_t * asn)
{
	EXTRA_ASSERT (api != NULL);
	EXTRA_ASSERT (asn != NULL);

	OCTET_STRING_fromBuf(&(asn->name), api->name, strlen(api->name));
	asn_int64_to_INTEGER(&(asn->chunkSize), api->chunk_size);

	if (!hashtable_conversion(api->options, &(asn->options),
							  key_value_pairs_convert_from_map))
		return FALSE;

	if (!hashtable_conversion(api->storage_policy, &(asn->storagePolicy),
							  key_value_pairs_convert_from_map))
		return FALSE;

	if (!hashtable_conversion(api->data_security, &(asn->dataSecurity),
							  key_value_pairs_convert_from_map))
		return FALSE;

	if (!hashtable_conversion(api->service_pools, &(asn->servicePools),
							  key_value_pairs_convert_from_map))
		return FALSE;

	return TRUE;
}

static void
namespace_info_cleanASN(NamespaceInfo_t * asn, gboolean only_content)
{
	if (!asn)
		return;
	asn->options.list.free = free_Parameter;
	asn->storagePolicy.list.free = free_Parameter;
	asn->dataSecurity.list.free = free_Parameter;
	if (only_content)
		ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NamespaceInfo, asn);
	else
		ASN_STRUCT_FREE(asn_DEF_NamespaceInfo, asn);
}

GByteArray *
namespace_info_marshall(namespace_info_t * namespace_info, GError ** err)
{
	asn_enc_rval_t encRet;
	GByteArray *result = NULL;
	NamespaceInfo_t asn1_namespace_info = {{0}};

	/*sanity checks */
	if (!namespace_info) {
		GSETERROR(err, "Invalid parameter");
		goto error_params;
	}

	/*fills an ASN.1 structure */
	if (!namespace_info_API2ASN(namespace_info, &asn1_namespace_info)) {
		GSETERROR(err, "API to ASN.1 mapping error");
		goto error_mapping;
	}

	/*serialize the ASN.1 structure */
	if (!(result = g_byte_array_sized_new(4096))) {
		GSETERROR(err, "memory allocation failure");
		goto error_alloc_gba;
	}
	encRet = der_encode(&asn_DEF_NamespaceInfo, &asn1_namespace_info,
			metautils_asn1c_write_gba, result);
	if (encRet.encoded == -1) {
		GSETERROR(err, "ASN.1 encoding error");
		goto error_encode;
	}

	/*free the ASN.1 structure */
	namespace_info_cleanASN(&asn1_namespace_info, TRUE);

	return result;

error_encode:
	g_byte_array_free(result, TRUE);
error_alloc_gba:
error_mapping:
	namespace_info_cleanASN(&asn1_namespace_info, TRUE);
error_params:

	return NULL;
}

namespace_info_t *
namespace_info_unmarshall(const guint8 * buf, gsize buf_len, GError ** err)
{
	asn_dec_rval_t decRet;
	asn_codec_ctx_t codecCtx;
	namespace_info_t *result = NULL;
	NamespaceInfo_t *asn1_namespace_info = NULL;

	/*sanity checks */
	if (!buf) {
		GSETCODE(err, ERRCODE_PARAM, "Invalid paremeter");
		return NULL;
	}

	/*deserialize the encoded form */
	codecCtx.max_stack_size = ASN1C_MAX_STACK;
	decRet = ber_decode(&codecCtx, &asn_DEF_NamespaceInfo, (void *) &asn1_namespace_info, buf, buf_len);
	if (decRet.code != RC_OK) {
		GSETCODE(err, CODE_INTERNAL_ERROR, "%s", (decRet.code == RC_WMORE) ? "uncomplete data" : "invalid data");
		namespace_info_cleanASN(asn1_namespace_info, FALSE);
		return NULL;
	}

	/*prepare the working structures */
	result = g_malloc0(sizeof(namespace_info_t));

	/*map the ASN.1 in a common structure */
	int rc = namespace_info_ASN2API(asn1_namespace_info, result);
	namespace_info_cleanASN(asn1_namespace_info, FALSE);
	asn1_namespace_info = NULL;
	if (rc)
		return result;

	namespace_info_free(result);

	GSETCODE(err, CODE_INTERNAL_ERROR, "ASN.1 to API mapping failure");
	return NULL;
}

