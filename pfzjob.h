/* vim: set tabstop=4:softtabstop=4:shiftwidth=4:noexpandtab */

/*
 * Copyright 2015 Lanet Network
 * Programmed by Oleksandr Natalenko <o.natalenko@lanet.ua>
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#ifndef _LIBZJOB_H_
#define _LIBZJOB_H_

#include <pthread.h>
#include <zmq.h>

typedef void (libzjob_run_t)(zmq_msg_t*, zmq_msg_t*);

typedef struct libzjobworker_context
{
	pthread_t id;
	void* zresponder;
	void* zcontrollee;
	libzjob_run_t* handler;
} libzjobworker_context_t;

typedef struct libzjobpool
{
	unsigned int index;
	unsigned int workers_count;
	pthread_t id;
	char* name;
	void* zcontext;
	void* zrouter;
	char* zrouter_uri;
	void* zdealer;
	char* zdealer_uri;
	void* zcontroller;
	char* zcontroller_uri;
	void* zcontrollee;
	libzjobworker_context_t* workers;
} libzjobpool_t;

libzjobpool_t* libzjob_init(
	const char* _pool_name,
	const unsigned int _orchestrator_threads,
	const unsigned int _worker_threads,
	const char* _orchestrator_address,
	const unsigned int _orchestrator_port,
	libzjob_run_t* _handler);
void libzjob_done(libzjobpool_t* _pool);

#endif /* _LIBZJOB_H_ */

