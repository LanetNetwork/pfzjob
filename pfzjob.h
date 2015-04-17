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

#ifndef __PFZJOB_H__
#define __PFZJOB_H__

#include <pthread.h>
#include <zmq.h>

typedef void (pfzjob_run_t)(zmq_msg_t*, zmq_msg_t*);

typedef struct pfzjobworker_context
{
	pthread_t id;
	void* zresponder;
	void* zcontrollee;
	pfzjob_run_t* handler;
} pfzjobworker_context_t;

typedef struct pfzjob_pool
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
	pfzjobworker_context_t* workers;
} pfzjob_pool_t;

pfzjob_pool_t* pfzjob_init(
	const char* _pool_name,
	const unsigned int _orchestrator_threads,
	const unsigned int _worker_threads,
	const char* _orchestrator_address,
	const unsigned int _orchestrator_port,
	pfzjob_run_t* _handler);
void pfzjob_done(pfzjob_pool_t* _pool);

#endif /* __PFZJOB_H__ */

