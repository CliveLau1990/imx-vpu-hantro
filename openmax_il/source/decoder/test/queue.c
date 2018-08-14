/*------------------------------------------------------------------------------
--       Copyright (c) 2015-2017, VeriSilicon Inc. All rights reserved        --
--                                                                            --
-- This software is confidential and proprietary and may be used only as      --
--   expressly authorized by VeriSilicon in a written licensing agreement.    --
--                                                                            --
--         This entire notice must be reproduced on all copies                --
--                       and may not be removed.                              --
--                                                                            --
--------------------------------------------------------------------------------
-- Redistribution and use in source and binary forms, with or without         --
-- modification, are permitted provided that the following conditions are met:--
--   * Redistributions of source code must retain the above copyright notice, --
--       this list of conditions and the following disclaimer.                --
--   * Redistributions in binary form must reproduce the above copyright      --
--       notice, this list of conditions and the following disclaimer in the  --
--       documentation and/or other materials provided with the distribution. --
--   * Neither the names of Google nor the names of its contributors may be   --
--       used to endorse or promote products derived from this software       --
--       without specific prior written permission.                           --
--------------------------------------------------------------------------------
-- THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"--
-- AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE  --
-- IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE --
-- ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE  --
-- LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR        --
-- CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF       --
-- SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS   --
-- INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN    --
-- CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)    --
-- ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE --
-- POSSIBILITY OF SUCH DAMAGE.                                                --
--------------------------------------------------------------------------------
------------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "queue.h"

void queue_clear(QUEUE_CLASS* queue) {

	pthread_mutex_lock(&(queue->lock));

	queue->left = 0;
	queue->head = 0;
	queue->rd = 0;
	queue->abort = 0;

	int i;
	for(i = 0; i < VDEC_EMPTY_QUEUE_MAX; i++) {
		memset(queue->elem[i], 0, queue->elemsize);
	}

	pthread_mutex_unlock(&(queue->lock));
}

void queue_init(QUEUE_CLASS* queue, int elemsize)
{
	pthread_mutex_init( &(queue->lock), NULL );
	pthread_cond_init ( &(queue->cond), NULL );

	int i;
	for(i=0 ; i<VDEC_EMPTY_QUEUE_MAX ; i++) {
		queue->elem[i] = malloc(elemsize);
	}

	queue->elemsize = elemsize;

	queue_clear(queue);
}

void queue_abort(QUEUE_CLASS* queue)
{
	pthread_mutex_lock(&(queue->lock));
	queue->abort = 1;
	pthread_mutex_unlock(&(queue->lock));

	pthread_cond_signal(&(queue->cond));
}

int queue_push(QUEUE_CLASS* queue, void* elem)
{
	int left;

	pthread_mutex_lock(&(queue->lock));

	memcpy(queue->elem[queue->head], elem, queue->elemsize);

	if(queue->left >= VDEC_EMPTY_QUEUE_MAX) {
		pthread_mutex_unlock(&(queue->lock));
		printf("queue overflow.. aborting.\n");
		exit(-1);
	}

	queue->head++;
	if(queue->head >= VDEC_EMPTY_QUEUE_MAX) {
		queue->head = 0;
	}

	queue->left++;

	left = queue->left;

	pthread_mutex_unlock(&(queue->lock));

	pthread_cond_signal(&(queue->cond));

	return left;
}

int queue_pop(QUEUE_CLASS* queue, void* elem)
{
	int left;
	int is_aborted;

	pthread_mutex_lock(&(queue->lock));

	while((queue->left <= 0) && !(queue->abort)) {
		pthread_cond_wait(&(queue->cond), &(queue->lock));
	}

	is_aborted = queue->abort;

	if(queue->left > 0 && !is_aborted) {

		memcpy(elem, queue->elem[queue->rd], queue->elemsize);

		queue->rd++;
		if(queue->rd >= VDEC_EMPTY_QUEUE_MAX) {
			queue->rd = 0;
		}
		queue->left--;
		left = queue->left;

	} else {
		memset(elem, 0, queue->elemsize);
		left = -1;
	}

	pthread_mutex_unlock(&(queue->lock));


	return left;
}

int queue_pop_noblock(QUEUE_CLASS* queue, void* elem, int *found)
{
	int left;

	pthread_mutex_lock(&(queue->lock));

	int is_aborted = queue->abort;

	if(queue->left > 0 && !is_aborted) {

		memcpy(elem, queue->elem[queue->rd], queue->elemsize);

		queue->rd++;
		if(queue->rd >= VDEC_EMPTY_QUEUE_MAX) {
			queue->rd = 0;
		}
		queue->left--;
		left = queue->left;

		*found = 1;

	} else {
		memset(elem, 0, queue->elemsize);

		*found = 0;
		
		if(is_aborted) {
			left = -1;
		} else {
			left = queue->left;
		}
	}

	pthread_mutex_unlock(&(queue->lock));

	return left;
}

int queue_size(QUEUE_CLASS* queue)
{
	int left;

	pthread_mutex_lock(&(queue->lock));

	left = queue->left;

	pthread_mutex_unlock(&(queue->lock));

	return left;
}
