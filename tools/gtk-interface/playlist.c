#include "playlist.h"
#include "json_helpers.h"

#include <assert.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <stdlib.h>
#include <string.h>

int add_playlist( const char* name, Playlists* playlists )
{
	int i, j;
	for( i = 0; i < playlists->num_playlists; i++ ) {
		if( strcmp( playlists->playlists[i].name->str, name ) == 0 ) {
			return -1;
		}
	}
	j = playlists->num_playlists;
	playlists->num_playlists++;
	playlists->playlists =
		realloc( playlists->playlists, sizeof( Playlist ) * playlists->num_playlists );
	playlists->playlists[j].name = g_string_new( name );
	playlists->playlists[j].num_items = 0;
	playlists->playlists[j].list_store = NULL;
	playlists->playlists[j].items = NULL;

	return j;
}

void parse_playlists( const char* s, Playlists* playlists )
{
	int item_id;
	GString* path;
	json_object *playlists_obj, *root_obj, *playlist_obj, *items_obj, *playlist_item_obj;
	root_obj = json_tokener_parse( s );
	if( get_json_object( root_obj, "playlists", json_type_array, &playlists_obj ) ) {
		int num_playlists = json_object_array_length( playlists_obj );
		playlists->num_playlists = num_playlists;
		playlists->playlists = (Playlist*)malloc( sizeof( Playlist ) * num_playlists );
		for( int i = 0; i < num_playlists; i++ ) {
			playlist_obj = json_object_array_get_idx( playlists_obj, i );
			GString* playlist_name;
			if( !get_json_object_string( playlist_obj, "name", &playlist_name ) ) {
				continue;
			}
			playlists->playlists[i].name = playlist_name;

			assert( get_json_object( playlist_obj, "items", json_type_array, &items_obj ) );
			int num_items = json_object_array_length( items_obj );
			playlists->playlists[i].items = malloc( sizeof( PlaylistItem ) * num_items );
			playlists->playlists[i].num_items = num_items;
			for( int j = 0; j < num_items; j++ ) {
				playlist_item_obj = json_object_array_get_idx( items_obj, j );
				//printf("got %s\n", json_object_get_string(playlist_item_obj));
				if( get_json_object_int( playlist_item_obj, "id", &item_id ) ) {
					playlists->playlists[i].items[j].item_id = item_id;
				}
				else {
					assert( 0 );
				}

				if( get_json_object_string( playlist_item_obj, "path", &path ) ) {
					playlists->playlists[i].items[j].path = path;
				}
				else if( get_json_object_string( playlist_item_obj, "stream", &path ) ) {
					playlists->playlists[i].items[j].path = path;
				}
				else {
					assert( 0 );
				}
			}
		}
	}
}

int fetch_playlists( const char* endpoint, Playlists* playlists )
{
	int err = 0;
	CURL* curl;
	CURLcode res;

	char url[1024];
	snprintf( url, 1024, "%s/playlists", endpoint );
	printf( "http get %s\n", url );

	curl = curl_easy_init();
	if( curl ) {
		GString* payload = g_string_sized_new( 1024 * 1024 );

		curl_easy_setopt( curl, CURLOPT_URL, url );
		curl_easy_setopt( curl, CURLOPT_FOLLOWLOCATION, 1L );
		curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, write_data );
		curl_easy_setopt( curl, CURLOPT_WRITEDATA, payload );

		res = curl_easy_perform( curl );
		if( res != CURLE_OK ) {
			fprintf( stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror( res ) );
			err = 1;
		}
		else {
			parse_playlists( payload->str, playlists );
		}

		g_string_free( payload, TRUE );
		curl_easy_cleanup( curl );
	}
	return err;
}

typedef struct PostData
{
	const char* ptr;
	size_t size;
} PostData;

size_t read_data( void* ptr, size_t size, size_t nmemb, void* userp )
{
	printf( "in read_data\n" );
	PostData* post_data = (PostData*)userp;

	size_t byte_len = size * nmemb;
	if( post_data->size < byte_len ) {
		byte_len = post_data->size;
	}
	memcpy( ptr, post_data->ptr, byte_len );
	post_data->ptr += byte_len;
	post_data->size -= byte_len;
	return byte_len;
}

int send_playlist( const char* endpoint, const char* playlist_name, const Playlist* playlist )
{
	int err = 0;
	CURL* curl;
	CURLcode res;
	json_object *root, *path_and_id, *playlist_array;

	root = json_object_new_object();
	json_object_object_add( root, "name", json_object_new_string( playlist_name ) );

	playlist_array = json_object_new_array();
	for( int i = 0; i < playlist->num_items; i++ ) {
		path_and_id = json_object_new_array();
		json_object_array_add( path_and_id,
							   json_object_new_string( playlist->items[i].path->str ) );
		json_object_array_add( path_and_id, json_object_new_int( 0 ) ); // TODO

		json_object_array_add( playlist_array, path_and_id );
	}
	json_object_object_add( root, "playlist", playlist_array );

	const char* payload = json_object_to_json_string( root );

	char url[1024];
	snprintf( url, 1024, "%s/playlists", endpoint );

	curl = curl_easy_init();
	if( curl ) {
		PostData post_data;
		post_data.ptr = payload;
		post_data.size = strlen( payload );

		curl_easy_setopt( curl, CURLOPT_URL, url );
		curl_easy_setopt( curl, CURLOPT_FOLLOWLOCATION, 1L );
		curl_easy_setopt( curl, CURLOPT_POST, 1L );
		curl_easy_setopt( curl, CURLOPT_READDATA, &post_data );
		curl_easy_setopt( curl, CURLOPT_READFUNCTION, read_data );
		curl_easy_setopt( curl, CURLOPT_POSTFIELDSIZE, post_data.size );

		res = curl_easy_perform( curl );
		if( res != CURLE_OK ) {
			fprintf( stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror( res ) );
			err = 1;
		}

		curl_easy_cleanup( curl );
	}

	json_object_put( root );

	return err;
}

int play_song( const char* endpoint, const char* playlist_name, int track_index, const char* when )
{
	int err = 0;
	CURL* curl;
	CURLcode res;

	char url[1024];
	snprintf( url,
			  1024,
			  "%s/play?playlist=%s&track=%d&when=%s",
			  endpoint,
			  playlist_name,
			  track_index,
			  when );

	GString* payload = g_string_sized_new( 1024 * 1024 );

	printf( "post %s\n", url );
	curl = curl_easy_init();
	if( curl ) {
		curl_easy_setopt( curl, CURLOPT_URL, url );
		curl_easy_setopt( curl, CURLOPT_FOLLOWLOCATION, 1L );
		curl_easy_setopt( curl, CURLOPT_POST, 1L );
		curl_easy_setopt( curl, CURLOPT_POSTFIELDSIZE, 0 );

		curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, write_data );
		curl_easy_setopt( curl, CURLOPT_WRITEDATA, payload );

		printf( "doing post\n" );
		res = curl_easy_perform( curl );
		if( res != CURLE_OK ) {
			fprintf( stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror( res ) );
			err = 1;
		}
		printf( "done posting song\n" );

		curl_easy_cleanup( curl );
	}

	g_string_free( payload, TRUE );
	return res;
}

int music_pause( const char* endpoint )
{
	int err = 0;
	CURL* curl;
	CURLcode res;

	char url[1024];
	snprintf( url, 1024, "%s/pause", endpoint );

	printf( "post %s\n", url );
	curl = curl_easy_init();
	if( curl ) {
		curl_easy_setopt( curl, CURLOPT_URL, url );
		curl_easy_setopt( curl, CURLOPT_FOLLOWLOCATION, 1L );
		curl_easy_setopt( curl, CURLOPT_POST, 1L );
		curl_easy_setopt( curl, CURLOPT_POSTFIELDSIZE, 0 );

		res = curl_easy_perform( curl );
		if( res != CURLE_OK ) {
			fprintf( stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror( res ) );
			err = 1;
		}

		curl_easy_cleanup( curl );
	}

	return res;
}

const PlaylistItem*
get_playlist_item_by_id( const Playlists* playlists, int* playlist_index, int item_id )
{
	for( int i = 0; i < playlists->num_playlists; i++ ) {
		for( int j = 0; j < playlists->playlists[i].num_items; j++ ) {
			//printf("%d vs %d\n", playlists->playlists[i].items[j].item_id, item_id );
			if( playlists->playlists[i].items[j].item_id == item_id ) {
				*playlist_index = i;
				return &( playlists->playlists[i].items[j] );
			}
		}
	}
	return NULL;
}
