/**
 * Implementation of local interface via libmicrohttpd
 *
 * @package vzlogger
 * @copyright Copyright (c) 2011, The volkszaehler.org project
 * @license http://www.gnu.org/licenses/gpl.txt GNU Public License
 * @author Steffen Vogel <info@steffenvogel.de>
 */
/*
 * This file is part of volkzaehler.org
 *
 * volkzaehler.org is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * volkzaehler.org is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with volkszaehler.org. If not, see <http://www.gnu.org/licenses/>.
 */

#include <json/json.h>
#include<cassert>
#include<cstring>
#include<cstdio>
#include<cstdlib>
#include<ctime>
#include<memory>

#include "vzlogger.h"
#include "Channel.hpp"
#include "local.hpp"
#include <MeterMap.hpp>
#include <VZException.hpp>

extern Config_Options options;

/**
 * Pointer types.
 */
typedef std::unique_ptr< struct json_object, int (*)( struct json_object * ) > JsonPtr;
typedef std::unique_ptr< char, void (*)( void * ) > HeapString;

int handle_request(
	void *cls
	, struct MHD_Connection *connection
	, const char *url
	, const char *method
	, const char *version
	, const char *upload_data
	, size_t *upload_data_size
	, void **con_cls
	) {

	int status;
	int response_code = MHD_HTTP_NOT_FOUND;

	/* mapping between meters and channels */
//std::list<Map> *mappings = static_cast<std::list<Map>*>(cls);
	MapContainer *mappings = static_cast<MapContainer*>(cls);

	struct MHD_Response *response = nullptr;

	try {
		const char *mode = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "mode");
		print(log_info, "Local request received: method=%s url=%s mode=%s", 
					"http", method, url, mode ? mode : "unspecified" );

		if (strcmp(method, "GET") == 0) {
			JsonPtr json_obj( json_object_new_object(), json_object_put );
			if ( json_obj.get() == nullptr ) {
				throw vz::VZException( "Unable to allocate JSON result object" );
			}

			JsonPtr json_data( json_object_new_array(), json_object_put );
			if ( json_data.get() == nullptr ) {
				throw vz::VZException( "Unable to allocate JSON data array" );
			}

			JsonPtr json_exception( nullptr, json_object_put );

			const char *uuid = url + 1; /* strip leading slash */
			bool show_all = false;

			if (strcmp(url, "/") == 0) {
				if (options.channel_index()) {
					show_all = true;
				}
				else {
					json_exception = JsonPtr( json_object_new_object(), json_object_put );
					if ( json_exception.get() == nullptr ) {
						throw vz::VZException( "Unable to allocate JSON exception object" );
					}

					JsonPtr str( json_object_new_string( "channel index is disabled" ), json_object_put );
					if ( str.get() == nullptr ) {
						throw vz::VZException( "Unable to allocate JSON string" );
					}

					int rc = json_object_object_add(json_exception.get(), "message", str.get());
					if ( rc != 0 ) {
						throw vz::VZException( "Unable to add exception message" );
					}
					str.release();

					JsonPtr integer( json_object_new_int( 0 ), json_object_put );
					if ( integer.get() == nullptr ) {
						throw vz::VZException( "Unable to allocate JSON integer" );
					}

					rc = json_object_object_add(json_exception.get(), "code", integer.get() );
					if ( rc != 0 ) {
						throw vz::VZException( "Unable to add exception code" );
					}
					integer.release();
				}
			}

			for(MapContainer::iterator mapping = mappings->begin(); mapping!=mappings->end(); mapping++) {
				for(MeterMap::iterator ch = mapping->begin(); ch!=mapping->end(); ch++) {
					if (strcmp((*ch)->uuid(), uuid) == 0 || show_all) {
						response_code = MHD_HTTP_OK;

/* blocking until new data arrives (comet-like blocking of HTTP response) */
						if (mode && strcmp(mode, "comet") == 0) {
							(*ch)->wait();
						}

						JsonPtr json_ch( json_object_new_object(), json_object_put );
						if ( json_ch.get() == nullptr ) {
							throw vz::VZException( "Unable to allocate JSON channel object" );
						}

						JsonPtr str( json_object_new_string((*ch)->uuid()), json_object_put );
						if ( str.get() == nullptr ) {
							throw vz::VZException( "Unable to allocate JSON channel uuid" );
						}

						int rc = json_object_object_add(json_ch.get(), "uuid", str.get());
						if ( rc != 0 ) {
							throw vz::VZException( "Unable to add JSON uuid to channel" );
						}
						str.release();
//json_object_object_add(json_ch, "middleware", json_object_new_string(ch->middleware()));
//json_object_object_add(json_ch, "last", json_object_new_double(ch->last.value));

						JsonPtr number( json_object_new_double((*ch)->tvtod()), json_object_put );
						if ( number.get() == nullptr ) {
							throw vz::VZException( "Unable to allocate JSON number" );
						}

						rc = json_object_object_add(json_ch.get(), "last", number.get());
						if ( rc != 0 ) {
							throw vz::VZException( "Unable to add JSON last timestamp to channel" );
						}
						number.release();

						JsonPtr integer( json_object_new_int(mapping->meter()->interval()), json_object_put );
						if ( integer.get() == nullptr ) {
							throw vz::VZException( "Unable to allocate JSON integer" );
						}

						rc = json_object_object_add(json_ch.get(), "interval", integer.get());
						if ( rc != 0 ) {
							throw vz::VZException( "Unable to add JSON meter time interval to channel" );
						}
						integer.release();

						str = JsonPtr( json_object_new_string(meter_get_details(mapping->meter()->protocolId())->name), json_object_put );
						if ( str.get() == nullptr ) {
							throw vz::VZException( "Unable to allocate JSON protocol name" );
						}

						rc = json_object_object_add(json_ch.get(), "protocol", str.get());
						if ( rc != 0 ) {
							throw vz::VZException( "Unable to add JSON protocol name to channel" );
						}
						str.release();

//struct json_object *json_tuples = api_json_tuples(&ch->buffer, ch->buffer.head, ch->buffer.tail);
//json_object_object_add(json_ch, "tuples", json_tuples);

						rc = json_object_array_add(json_data.get(), json_ch.get());
						if ( rc != 0 ) {
							throw vz::VZException( "Unable to add JSON channel to response" );
						}
						json_ch.release();
					}
				}
			}

			JsonPtr version( json_object_new_string(VERSION), json_object_put );
			if ( version.get() == nullptr ) {
				throw vz::VZException( "Unable to allocate JSON version string" );
			}

			int rc = json_object_object_add(json_obj.get(), "version", version.get());
			if ( rc != 0 ) {
				throw vz::VZException( "Unable to add version to JSON response" );
			}
			version.release();

			JsonPtr package( json_object_new_string(PACKAGE), json_object_put );
			if ( package.get() == nullptr ) {
				throw vz::VZException( "Unable to allocate JSON package string" );
			}

			rc = json_object_object_add(json_obj.get(), "generator", package.get());
			if ( rc != 0 ) {
				throw vz::VZException( "Unable to add package to JSON response" );
			}
			package.release();

			rc = json_object_object_add(json_obj.get(), "data", json_data.get());
			if ( rc != 0 ) {
				throw vz::VZException( "Unable to add data to JSON response" );
			}
			json_data.release();

			if (json_exception.get()) {
				rc = json_object_object_add(json_obj.get(), "exception", json_exception.get());
				if ( rc != 0 ) {
					throw vz::VZException( "Unable to add exception info to JSON response" );
				}
				json_exception.release();
			}

			const char * json_str = json_object_to_json_string(json_obj.get());
			if ( json_str == nullptr ) {
				throw vz::VZException( "Unable to convert JSON object to string" );
			}

			response = MHD_create_response_from_data(strlen(json_str), const_cast< char * >( json_str ), FALSE, TRUE);
			if ( response == nullptr ) {
				throw vz::VZException( "Unable to create JSON response" );
			}

			rc = MHD_add_response_header(response, "Content-type", "application/json");
			if ( rc == MHD_NO ) {
				throw vz::VZException( "Unable to add Content-type header to JSON response" );
			}
		}
		else {
			HeapString response_str( strdup("not implemented\n"), free );
			if ( response_str.get() == nullptr ) {
				throw vz::VZException( "Unable to allocate response string" );
			}

			response = MHD_create_response_from_data(strlen(response_str.get()), response_str.get(), TRUE, FALSE);
			if ( response == nullptr ) {
				throw vz::VZException( "Unable to create response" );
			}
			response_str.release();

			response_code = MHD_HTTP_METHOD_NOT_ALLOWED;

			int rc = MHD_add_response_header(response, "Content-type", "text/text");
			if ( rc == MHD_NO ) {
				throw vz::VZException( "Unable to add Content-type header to response" );
			}
		}
	} catch ( std::exception &e){
		print( log_error, "Error handling request %s: %s", "http", url, e.what() );
	}

	status = MHD_queue_response(connection, response_code, response);

	if ( response != nullptr ) {
		MHD_destroy_response(response);
	}

	return status;
}
