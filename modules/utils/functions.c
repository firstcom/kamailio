/*
 * script functions of utils module
 *
 * Copyright (C) 2008 Juha Heinanen
 * Copyright (C) 2013 Carsten Bock, ng-voice GmbH
 *
 * This file is part of Kamailio, a free SIP server.
 *
 * Kamailio is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * Kamailio is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program; if not, write to the Free Software 
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

/*!
 * \file
 * \brief Kamailio utils :: 
 * \ingroup utils
 * Module: \ref utils
 */


#include <curl/curl.h>

#include "../../mod_fix.h"
#include "../../pvar.h"
#include "../../route_struct.h"
#include "../../ut.h"
#include "../../mem/mem.h"
#include "../../parser/msg_parser.h"
#include "../../lvalue.h"

#include "utils.h"


/* 
 * curl write function that saves received data as zero terminated
 * to stream. Returns the amount of data taken care of.
 *
 * This function may be called multiple times for larger responses, 
 * so it reallocs + concatenates the buffer as needed.
 */
size_t write_function( void *ptr, size_t size, size_t nmemb, void *stream_ptr)
{
	http_res_stream_t *stream = (http_res_stream_t *) stream_ptr;

	stream->buf = (char *) pkg_realloc(stream->buf, stream->curr_size + 
			(size * nmemb) + 1);

	if (stream->buf == NULL) {
		LM_ERR("cannot allocate memory for stream\n");
		return CURLE_WRITE_ERROR;
	}

	memcpy(&stream->buf[stream->pos], (char *) ptr, (size * nmemb));

	stream->curr_size += ((size * nmemb) + 1);
	stream->pos += (size * nmemb);

	stream->buf[stream->pos + 1] = '\0';

	return size * nmemb;
}


/* 
 * Performs http_query and saves possible result (first body line of reply)
 * to pvar.
 */
int http_query(struct sip_msg* _m, char* _url, char* _dst, char* _post, char* _hdr)
{
	CURL *curl;
	CURLcode res;  
	str value, post_value, hdr_value;
	char *url, *at, *post = NULL, *hdr = NULL;
	http_res_stream_t stream;
	long stat;
	pv_spec_t *dst;
	pv_value_t val;
	double download_size;

	memset(&stream, 0, sizeof(http_res_stream_t));

	if (fixup_get_svalue(_m, (gparam_p)_url, &value) != 0) {
		LM_ERR("cannot get page value\n");
		return -1;
	}

	curl = curl_easy_init();
	if (curl == NULL) {
		LM_ERR("failed to initialize curl\n");
		return -1;
	}

	url = pkg_malloc(value.len + 1);
	if (url == NULL) {
		curl_easy_cleanup(curl);
		LM_ERR("cannot allocate pkg memory for url\n");
		return -1;
	}
	memcpy(url, value.s, value.len);
	*(url + value.len) = (char)0;
	curl_easy_setopt(curl, CURLOPT_URL, url);

	if (_hdr) {
		if (fixup_get_svalue(_m, (gparam_p)_hdr, &hdr_value) != 0) {
			LM_ERR("cannot get Header value\n");
			curl_easy_cleanup(curl);
			pkg_free(url);
			return -1;
		}
		if (hdr_value.len > 0) {
			hdr = pkg_malloc(hdr_value.len + 1);
			if (hdr == NULL) {
				curl_easy_cleanup(curl);
				pkg_free(url);
				LM_ERR("cannot allocate pkg memory for header\n");
				return -1;
			}
			memcpy(hdr, hdr_value.s, hdr_value.len);
			*(hdr + hdr_value.len) = (char)0;

			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr);
		}
	}

	if (_post) {
		/* Now specify we want to POST data */ 
		curl_easy_setopt(curl, CURLOPT_POST, 1L);

		if (fixup_get_svalue(_m, (gparam_p)_post, &post_value) != 0) {
			LM_ERR("cannot get post value\n");
			curl_easy_cleanup(curl);
			pkg_free(url);
			if (hdr) pkg_free(hdr);
			return -1;
		}
		if (post_value.len > 0) {
			post = pkg_malloc(post_value.len + 1);
			if (post == NULL) {
				curl_easy_cleanup(curl);
				pkg_free(url);
				if (hdr) pkg_free(hdr);
				LM_ERR("cannot allocate pkg memory for post\n");
				return -1;
			}
			memcpy(post, post_value.s, post_value.len);
			*(post + post_value.len) = (char)0;
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post);
		}
	}


	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, (long)1);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)http_query_timeout);

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_function);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &stream);

	res = curl_easy_perform(curl);  
	pkg_free(url);
	if (_post) {
		pkg_free(post);
	}
	if (_hdr) {
		pkg_free(hdr);
	}

	if (res != CURLE_OK) {
		/* http://curl.haxx.se/libcurl/c/libcurl-errors.html */
		if (res == CURLE_COULDNT_CONNECT) {
			LM_WARN("failed to connect() to host\n");
		} else if ( res == CURLE_COULDNT_RESOLVE_HOST ) {
			LM_WARN("couldn't resolve host\n");
		} else {
			LM_ERR("failed to perform curl (%d)\n", res);
		}

		curl_easy_cleanup(curl);
		if(stream.buf)
			pkg_free(stream.buf);
		return -1;
	}

	curl_easy_getinfo(curl, CURLINFO_HTTP_CODE, &stat);
	if ((stat >= 200) && (stat < 500)) {
		curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD, &download_size);
		LM_DBG("http_query download size: %u\n", (unsigned int)download_size);

		/* search for line feed */
		at = memchr(stream.buf, (char)10, download_size);
		if (at == NULL) {
			/* not found: use whole stream */
			at = stream.buf + (unsigned int)download_size;
		}
		val.rs.s = stream.buf;
		val.rs.len = at - stream.buf;
		LM_DBG("http_query result: %.*s\n", val.rs.len, val.rs.s);
		val.flags = PV_VAL_STR;
		dst = (pv_spec_t *)_dst;
		dst->setf(_m, &dst->pvp, (int)EQ_T, &val);
	}

	curl_easy_cleanup(curl);
	pkg_free(stream.buf);
	return stat;
}
