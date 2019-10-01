
#include "id3.h"
#include "log.h"

#include "icy.h"
#include "io_utils.h"
#include "log.h"
#include "my_malloc.h"
#include "player.h"
#include "string_utils.h"

#include <byteswap.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>

#include <mpg123.h>

int is_little_endian()
{
	int i = 1;
	char* p = (char*)&i;
	return p[0] == 1;
}

SGLIB_DEFINE_RBTREE_FUNCTIONS( ID3CacheItem, left, right, color_field, ID3CACHE_CMPARATOR )

int id3_get( ID3Cache* cache, const char* library_path, const char* path, ID3CacheItem* item )
{
	sds full_path = sdscatfmt( NULL, "%s/%s", library_path, path );
	int res;
	LOG_DEBUG( "path=s reading id3 tags", full_path );
	res = mpg123_open( cache->mh, full_path );
	if( res != MPG123_OK ) {
		LOG_ERROR( "err=d errstr=s path=s open error", res, strerror(errno), full_path );
		res = 1;
		assert(0);
		goto error;
	}

	mpg123_scan( cache->mh );

	// works with VBR too
	float tpf = (float)mpg123_tpf( cache->mh );
	long num_frames = mpg123_framelength( cache->mh );
	float length_estimate = num_frames * tpf;

	LOG_DEBUG( "path=s length=f track length", full_path, length_estimate );

	item->path = sdsnew( path );
	item->track = 0; //TODO stored in comment[30] of id3

	item->artist = sdscpy( item->artist, "unknown artist" );
	item->title = sdscpy( item->title, "unknown title" );
	item->album = sdscpy( item->album, "unknown album" );
	item->length = length_estimate;

	mpg123_id3v1* v1;
	mpg123_id3v2* v2;
	res = 1;
	int meta = mpg123_meta_check( cache->mh );
	if( meta & MPG123_NEW_ID3 ) {
		if( mpg123_id3( cache->mh, &v1, &v2 ) == MPG123_OK ) {
			res = 0;
			// first load values from v1
			if( v1 != NULL ) {
				item->artist = sdscpy( item->artist, v1->artist );
				item->title = sdscpy( item->title, v1->title );
				item->album = sdscpy( item->album, v1->album );
				item->year = atoi( v1->year );
				if( v1->comment[28] == 0 && v1->comment[29] != 0 ) {
					item->track = (unsigned char)v1->comment[29];
				}
			}

			// override values with v2
			if( v2 != NULL ) {
				if( v2->artist && v2->artist->p )
					item->artist = sdscpy( item->artist, v2->artist->p );
				if( v2->album && v2->album->p )
					item->album = sdscpy( item->album, v2->album->p );
				if( v2->title && v2->title->p )
					item->title = sdscpy( item->title, v2->title->p );
				if( v2->year && v2->year->p )
					LOG_DEBUG( "year=s year", v2->year->p );

				for( int i = 0; i < v2->texts; i++ ) {
					LOG_DEBUG( "lang=*s id=*s desc=s value=s text",
							   3,
							   null_to_empty( v2->text[i].lang ),
							   4,
							   null_to_empty( v2->text[i].id ),
							   null_to_empty( v2->text[i].description.p ),
							   null_to_empty( v2->text[i].text.p ) );
				}

				for( int i = 0; i < v2->comments; i++ ) {
					LOG_DEBUG( "lang=*s id=*s desc=s value=s comment",
							   3,
							   null_to_empty( v2->comment_list[i].lang ),
							   4,
							   null_to_empty( v2->comment_list[i].id ),
							   null_to_empty( v2->comment_list[i].description.p ),
							   null_to_empty( v2->comment_list[i].text.p ) );
				}

				for( int i = 0; i < v2->extras; i++ ) {
					LOG_DEBUG( "lang=*s id=*s desc=s value=s extra",
							   3,
							   null_to_empty( v2->extra[i].lang ),
							   4,
							   null_to_empty( v2->extra[i].id ),
							   null_to_empty( v2->extra[i].description.p ),
							   null_to_empty( v2->extra[i].text.p ) );
				}
			}
		}
	}
	else {
		LOG_WARN( "path=s no id3 tag values", path );
	}

error:
	sdsfree( full_path );
	mpg123_close( cache->mh );
	return res;
}

int read_float( FILE* fp, float* x )
{
	assert( sizeof( float ) == 4 );
	int res;
	res = fread( x, sizeof( float ), 1, fp );
	if( res != 1 ) {
		return 1;
	}
	return 0;
}

int read_uint32( FILE* fp, uint32_t* x )
{
	int res;
	res = fread( x, sizeof( uint32_t ), 1, fp );
	if( res != 1 ) {
		return 1;
	}

	if( !is_little_endian() ) {
		bswap_32( *x );
	}

	return 0;
}

int read_str( FILE* fp, sds* s )
{
	int res;
	uint32_t n;
	res = read_uint32( fp, &n );
	if( res != 0 ) {
		return 1;
	}
	*s = sdsnewlen( NULL, n + 1 );
	res = fread( *s, 1, n, fp );
	if( res < 0 || res != n ) {
		LOG_ERROR( "res=d unable to fread", res );
		return 1;
	}
	sdsrange( *s, 0, n - 1 );
	return 0;
}

int id3_cache_load( ID3Cache* cache )
{
	int res;
	LOG_INFO( "path=s loading id3 cache", cache->path );
	FILE* fp = fopen( cache->path, "rb" );
	if( !fp ) {
		LOG_ERROR( "path=s failed to open library for reading", cache->path );
		return 1;
	}
	// Versioning
	sds version = NULL;
	res = read_str( fp, &version );
	if( res ) {
		LOG_ERROR( "unable to read version" );
		return 1;
	}
	sdsfree( version );
	LOG_INFO( "HERE" );

	ID3CacheItem* item;

	for( ;; ) {
		item = (ID3CacheItem*)my_malloc( sizeof( ID3CacheItem ) );
		res = read_str( fp, &item->path );
		if( res ) {
			my_free( item );
			break;
		}

		//LOG_INFO( "path=s loading cached entry", item->path );
		res = read_uint32( fp, &item->mod_time );
		if( res ) {
			LOG_ERROR( "unable to read complete record" );
			break;
		}
		res = read_str( fp, &item->album );
		if( res ) {
			LOG_ERROR( "unable to read complete record" );
			break;
		}
		res = read_str( fp, &item->artist );
		if( res ) {
			LOG_ERROR( "unable to read complete record" );
			break;
		}
		res = read_str( fp, &item->title );
		if( res ) {
			LOG_ERROR( "unable to read complete record" );
			break;
		}
		res = read_uint32( fp, &item->year );
		if( res ) {
			LOG_ERROR( "unable to read complete record" );
			break;
		}
		res = read_uint32( fp, &item->track );
		if( res ) {
			LOG_ERROR( "unable to read complete record" );
			break;
		}
		res = read_float( fp, &item->length );
		if( res ) {
			LOG_ERROR( "unable to read complete record" );
			break;
		}

		sglib_ID3CacheItem_add( &( cache->root ), item );
	}
	LOG_INFO( "path=s done reading cache", cache->path );

	fclose( fp );
	return 0;
}

int id3_cache_new( ID3Cache** cache, const char* cache_path, mpg123_handle* mh )
{
	*cache = (ID3Cache*)my_malloc( sizeof( ID3Cache ) );
	( *cache )->root = NULL;
	( *cache )->mh = mh;
	( *cache )->path = cache_path;
	( *cache )->dirty = false;

	id3_cache_load( *cache );
	return 0;
}

int id3_cache_get( ID3Cache* cache,
				   const char* library_path,
				   const char* path,
				   ID3CacheItem** item )
{
	struct stat st;

	sds full_path = sdscatfmt( NULL, "%s/%s", library_path, path );

	if( stat( full_path, &st ) ) {
		LOG_ERROR( "path=s failed to stat file (caching will be disabled)", full_path );
		st.st_mtim.tv_sec = 0;
	}

	int res;
	ID3CacheItem id;
	id.path = (sds)path;
	*item = sglib_ID3CacheItem_find_member( cache->root, &id );
	if( *item == NULL ) {
		LOG_INFO( "path=s item was not found in cache", path );
		*item = (ID3CacheItem*)my_malloc( sizeof( ID3CacheItem ) );
		memset( *item, 0, sizeof( ID3CacheItem ) );
		( *item )->mod_time = st.st_mtim.tv_sec;
		res = id3_get( cache, library_path, path, *item );
		if( res ) {
			LOG_INFO( "path=s failed to read mp3 id3", full_path );
			my_free( *item );
			return 1;
		}
		sglib_ID3CacheItem_add( &( cache->root ), *item );
		cache->dirty = true;
	}
	if( ( *item )->mod_time != st.st_mtim.tv_sec ) {
		LOG_INFO( "path=s cachemtime=d mtime=d mod time doesnt match cached version, re-reading",
				  cache->path,
				  ( *item )->mod_time,
				  st.st_mtim.tv_sec );
		res = id3_get( cache, library_path, path, *item );
		if( res ) {
			LOG_INFO( "path=s failed to read mp3 id3", full_path );
			my_free( *item );
			return 1;
		}
		( *item )->mod_time = st.st_mtim.tv_sec;
		cache->dirty = true;
	}
	( *item )->seen = true;
	return 0;
}

void write_float( FILE* fp, float x )
{
	fwrite( &x, sizeof( float ), 1, fp );
}

void write_uint32( FILE* fp, uint32_t x )
{
	if( !is_little_endian() ) {
		bswap_32( x );
	}
	fwrite( &x, sizeof( uint32_t ), 1, fp );
}

void write_str( FILE* fp, const char* s )
{
	int n;
	n = strlen( s );
	write_uint32( fp, (uint32_t)n );
	if( n > 0 ) {
		fwrite( s, 1, n, fp );
	}
}

int id3_cache_save( ID3Cache* cache )
{
	LOG_INFO( "path=s saving id3 cache", cache->path );
	FILE* fp = fopen( cache->path, "wb" );
	if( !fp ) {
		LOG_ERROR( "path=s failed to open library for writing", cache->path );
		return 1;
	}
	// Versioning
	write_str( fp, "a" );

	struct sglib_ID3CacheItem_iterator it;
	ID3CacheItem* te;
	for( te = sglib_ID3CacheItem_it_init_inorder( &it, cache->root ); te != NULL;
		 te = sglib_ID3CacheItem_it_next( &it ) ) {
		write_str( fp, te->path );
		write_uint32( fp, te->mod_time );
		write_str( fp, te->album );
		write_str( fp, te->artist );
		write_str( fp, te->title );
		write_uint32( fp, te->year );
		write_uint32( fp, te->track );
		write_float( fp, te->length );
	}
	LOG_INFO( "path=s done saving id3 cache", cache->path );

	fclose( fp );
	return 0;
}
