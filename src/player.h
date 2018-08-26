#pragma once

#include <mpg123.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>

#ifdef USE_RASP_PI
	#include <alsa/asoundlib.h>
#else
	#include <pulse/simple.h>
	#include <pulse/error.h>
#endif



#include "circular_buffer.h"
#include "play_queue.h"
#include "playlist_manager.h"
#include "httpget.h"

#define PLAYER_ARTIST_LEN 1024
#define PLAYER_TITLE_LEN 1024

#define TRACK_CHANGE_IMMEDIATE 1
#define TRACK_CHANGE_NEXT 2

#define AUDIO_DATA 1
#define ID_DATA_START 2
#define ID_DATA_END 3

#define PLAYER_CONTROL_PLAYING 1
#define PLAYER_CONTROL_SKIP 2
#define PLAYER_CONTROL_EXIT 4

struct Player;
typedef struct Player Player;


typedef void (*MetadataObserver)( bool playing, const PlaylistItem *playlist_item, int playlist_version, void *data );
typedef int (*AudioConsumer)( Player *player, const char *p, size_t n );
typedef void (*LoadItem)( Player *player, PlaylistItem *item );

struct Player
{
#ifdef USE_RASP_PI
	snd_pcm_t *alsa_handle;
#else
	pa_simple *pa_handle;
#endif

	mpg123_handle *mh;

	pthread_t audio_thread;
	pthread_t reader_thread;

	CircularBuffer circular_buffer;

	PlayQueue play_queue;
	pthread_mutex_t the_lock;

	PlaylistManager *playlist_manager;

	int metadata_observers_num;
	int metadata_observers_cap;
	MetadataObserver *metadata_observers;
	void **metadata_observers_data;

	pthread_cond_t done_track_cond;

	PlaylistItem *current_track;

	PlaylistItem *playlist_item_to_buffer;
	PlaylistItem *playlist_item_to_buffer_override;

	const char *library_path;

	// when true play, when false, pause / stop
	//bool playing;
	//bool exit;
	//bool next_track;
	int control; // bitmask of PLAYER_CONTROL_* flags

	// control over changing tracks
	//pthread_mutex_t change_track_lock;
	bool load_in_progress;
	bool load_abort_requested;
	pthread_cond_t load_cond;
	
	size_t max_payload_size;

	size_t decode_buffer_size;
	char *decode_buffer;

	AudioConsumer audio_consumer;
	LoadItem load_item;
};

#ifdef USE_RASP_PI
	void init_alsa( Player *player );
	int consume_alsa( Player *player, const char *p, size_t n );
#else
	void init_pa( Player *player );
	int consume_pa( Player *player, const char *p, size_t n );
#endif

int init_player( Player *player, const char *library_path );
int start_player( Player *player );
int stop_player( Player *player );

void stop_loader( Player *player );
void start_loader( Player *player );

void player_lock( Player *player );
void player_unlock( Player *player );

int player_add_metadata_observer( Player *player, MetadataObserver observer, void *data );

int player_change_track( Player *player, PlaylistItem *playlist_item, int when );
int player_change_next_album( Player *player, int when );
int player_change_next_track( Player *player, int when );
int player_change_next_playlist( Player *player, int when );

int player_notify_item_change( Player *player, PlaylistItem *playlist_item );

void player_set_playing( Player *player, bool playing );
void player_pause( Player *player );

int player_get_control( Player *player );
