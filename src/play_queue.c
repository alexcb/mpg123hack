#include "play_queue.h"

int play_queue_init( PlayQueue* pq )
{
	pq->front = pq->rear = -1;
	return 0;
}

int play_queue_add( PlayQueue* pq, PlayQueueItem** item )
{
	if( pq->front == -1 ) {
		pq->front = pq->rear = 0;
		*item = &( pq->queue[pq->front] );
		return 0;
	}

	if( ( pq->rear == ( PLAY_QUEUE_CAP - 1 ) && pq->front == 0 ) ||
		( pq->rear + 1 ) == pq->front ) {
		*item = NULL;
		return 1;
	}

	pq->rear++;
	if( pq->rear == PLAY_QUEUE_CAP ) {
		pq->rear = 0;
	}
	*item = &( pq->queue[pq->rear] );
	return 0;
}

int play_queue_head( PlayQueue* pq, PlayQueueItem** item )
{
	if( pq->front == -1 )
		return 1;
	*item = &( pq->queue[pq->front] );
	return 0;
}

int play_queue_size( PlayQueue* pq )
{
	if( pq->rear == -1 && pq->front == -1 )
		return 0;
	if( pq->rear == pq->front )
		return 1;
	if( pq->rear > pq->front )
		return pq->rear - pq->front + 1;
	return PLAY_QUEUE_CAP - pq->front + pq->rear;
}

void play_queue_clear( PlayQueue* pq )
{
	PlayQueueItem* pqi;
	while( play_queue_head( pq, &pqi ) == 0 ) {
		playlist_item_ref_down( pqi->playlist_item );
		play_queue_pop( pq );
	}
}

int play_queue_pop( PlayQueue* pq )
{
	if( pq->front == -1 )
		return 1;
	if( pq->front == pq->rear ) {
		pq->front = pq->rear = -1;
		return 0;
	}
	pq->front++;
	if( pq->front == PLAY_QUEUE_CAP ) {
		pq->front = 0;
	}
	return 0;
}
