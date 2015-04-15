/* vim: set tabstop=4:softtabstop=4:shiftwidth=4:noexpandtab */

#include <pfcq.h>
#include <pfzjob.h>
#include <pthread.h>
#include <unistd.h>
#include <zmq.h>

static unsigned int pool_index = 0;
static pthread_mutex_t pool_index_lock = PTHREAD_MUTEX_INITIALIZER;

static unsigned int allocate_pool_index(void)
{
	unsigned int ret = 0;

	if (unlikely(pthread_mutex_lock(&pool_index_lock)))
		panic("pthread_mutex_lock");
	ret = pool_index;
	pool_index++;
	if (unlikely(pthread_mutex_unlock(&pool_index_lock)))
		panic("pthread_mutex_unlock");

	return ret;
}

static void* client_worker(void* _data)
{
	if (unlikely(!_data))
		return NULL;

	libzjobworker_context_t* context = _data;

	zmq_pollitem_t items[2];
	items[0].socket = context->zresponder;
	items[0].events = ZMQ_POLLIN;
	items[1].socket = context->zcontrollee;
	items[1].events = ZMQ_POLLIN;

	for (;;)
	{
		int events = zmq_poll(items, 2, -1);
		if (unlikely(events < 0))
		{
			warning("zmq_pool");
			continue;
		}

		if (items[0].revents & ZMQ_POLLIN)
		{
			zmq_msg_t message;
			zmq_msg_init(&message);
			zmq_msg_recv(&message, context->zresponder, 0);

			zmq_msg_t reply;

			context->handler(&message, &reply);

			zmq_msg_close(&message);

			zmq_msg_send(&reply, context->zresponder, 0);
		}

		if (items[1].revents & ZMQ_POLLIN)
		{
			break;
		}

	}

	return NULL;
}

static void* orchestrator_worker(void* _data)
{
	if (unlikely(!_data))
		return NULL;

	libzjobpool_t* pool = _data;
	zmq_pollitem_t items[3];
	items[0].socket = pool->zrouter;
	items[0].events = ZMQ_POLLIN;
	items[1].socket = pool->zdealer;
	items[1].events = ZMQ_POLLIN;
	items[2].socket = pool->zcontrollee;
	items[2].events = ZMQ_POLLIN;

	for (;;)
	{
		int events = zmq_poll(items, 3, -1);
		if (unlikely(events < 0))
		{
			warning("zmq_pool");
			continue;
		}

		if (items[0].revents & ZMQ_POLLIN)
		{
			for (;;)
			{
				zmq_msg_t message;
				zmq_msg_init(&message);
				zmq_msg_recv(&message, pool->zrouter, 0);
				int more = zmq_msg_more(&message);
				zmq_msg_send(&message, pool->zdealer, more ? ZMQ_SNDMORE: 0);
				zmq_msg_close(&message);
				if (!more)
					break;
			}
		}

		if (items[1].revents & ZMQ_POLLIN)
		{
			for (;;)
			{
				zmq_msg_t message;
				zmq_msg_init(&message);
				zmq_msg_recv(&message, pool->zdealer, 0);
				int more = zmq_msg_more(&message);
				zmq_msg_send(&message, pool->zrouter, more ? ZMQ_SNDMORE: 0);
				zmq_msg_close(&message);
				if (!more)
					break;
			}
		}

		if (items[2].revents & ZMQ_POLLIN)
		{
			break;
		}
	}

	return NULL;
}

libzjobpool_t* libzjob_init(
	const char* _pool_name,
	const unsigned int _orchestrator_threads,
	const unsigned int _worker_threads,
	const char* _orchestrator_address,
	const unsigned int _orchestrator_port,
	libzjob_run_t* _handler)
{
	int orchestrator_threads = _orchestrator_threads;
	if (orchestrator_threads < 0)
		orchestrator_threads = 1;
	else if (orchestrator_threads == 0)
	{
		orchestrator_threads = sysconf(_SC_NPROCESSORS_ONLN);
		if (unlikely(orchestrator_threads == -1))
			orchestrator_threads = 1;
	}

	int worker_threads = _worker_threads;
	if (worker_threads < 0)
		worker_threads = 1;
	else if (worker_threads == 0)
	{
		worker_threads = sysconf(_SC_NPROCESSORS_ONLN);
		if (unlikely(worker_threads == -1))
			worker_threads = 1;
	}

	libzjobpool_t* new_pool = pfcq_alloc(sizeof(libzjobpool_t));
	new_pool->name = pfcq_strdup(_pool_name);
	if (unlikely(!new_pool->name))
		panic("strdup");

	new_pool->zcontext = zmq_ctx_new();
	if (unlikely(!new_pool->zcontext))
		panic("zmq_ctx_new");

	if (unlikely(zmq_ctx_set(new_pool->zcontext, ZMQ_IO_THREADS, orchestrator_threads) == -1))
		panic("zmq_ctx_set");

	new_pool->zrouter = zmq_socket(new_pool->zcontext, ZMQ_ROUTER);
	if (unlikely(!new_pool->zrouter))
		panic("zmq_socket");
	new_pool->zrouter_uri = pfcq_mstring("tcp://%s:%u", _orchestrator_address, _orchestrator_port);
	if (unlikely(!new_pool->zrouter_uri))
		panic("pfcq_mstring");
	if (unlikely(zmq_bind(new_pool->zrouter, new_pool->zrouter_uri) == -1))
		panic("zmq_bind");

	new_pool->index = allocate_pool_index();
	new_pool->zdealer = zmq_socket(new_pool->zcontext, ZMQ_DEALER);
	if (unlikely(!new_pool->zdealer))
		panic("zmq_socket");
	new_pool->zdealer_uri = pfcq_mstring("inproc://dealer_%u", new_pool->index);
	if (unlikely(!new_pool->zdealer_uri))
		panic("pfcq_mstring");
	if (unlikely(zmq_bind(new_pool->zdealer, new_pool->zdealer_uri) == -1))
		panic("zmq_bind");

	new_pool->zcontroller = zmq_socket(new_pool->zcontext, ZMQ_PUB);
	if (unlikely(!new_pool->zcontroller))
		panic("zmq_socket");
	new_pool->zcontroller_uri = pfcq_mstring("inproc://controller_%u", new_pool->index);
	if (unlikely(!new_pool->zcontroller_uri))
		panic("pfcq_mstring");
	if (unlikely(zmq_bind(new_pool->zcontroller, new_pool->zcontroller_uri) == -1))
		panic("zmq_bind");

	new_pool->zcontrollee = zmq_socket(new_pool->zcontext, ZMQ_SUB);
	if (unlikely(!new_pool->zcontrollee))
		panic("zmq_socket");
	if (unlikely(zmq_connect(new_pool->zcontrollee, new_pool->zcontroller_uri) == -1))
		panic("zmq_connect");
	if (unlikely(zmq_setsockopt(new_pool->zcontrollee, ZMQ_SUBSCRIBE, "", 0) == -1))
		panic("zmq_setsockopt");

	new_pool->workers_count = worker_threads;
	new_pool->workers = pfcq_alloc(worker_threads * sizeof(libzjobworker_context_t));
	for (unsigned int i = 0; i < new_pool->workers_count; i++)
	{
		new_pool->workers[i].handler = _handler;
		new_pool->workers[i].zcontrollee = zmq_socket(new_pool->zcontext, ZMQ_SUB);
		if (unlikely(!new_pool->workers[i].zcontrollee))
			panic("zmq_socket");
		if (unlikely(zmq_connect(new_pool->workers[i].zcontrollee, new_pool->zcontroller_uri) == -1))
			panic("zmq_connect");
		if (unlikely(zmq_setsockopt(new_pool->workers[i].zcontrollee, ZMQ_SUBSCRIBE, "", 0) == -1))
			panic("zmq_setsockopt");

		new_pool->workers[i].zresponder = zmq_socket(new_pool->zcontext, ZMQ_REP);
		if (unlikely(!new_pool->workers[i].zresponder))
			panic("zmq_socket");
		if (unlikely(zmq_connect(new_pool->workers[i].zresponder, new_pool->zdealer_uri) == -1))
			panic("zmq_connect");
		if (unlikely(pthread_create(&new_pool->workers[i].id, NULL, client_worker, (void*)&new_pool->workers[i])))
			panic("pthread_create");
	}

	if (unlikely(pthread_create(&new_pool->id, NULL, orchestrator_worker, (void*)new_pool)))
		panic("pthread_create");

	return new_pool;
}

void libzjob_done(libzjobpool_t* _pool)
{
	zmq_msg_t shutdown_message;
	if (unlikely(zmq_msg_init(&shutdown_message) == -1))
		panic("zmq_msg_init");
	if (unlikely(zmq_msg_send(&shutdown_message, _pool->zcontroller, 0) == -1))
		panic("zmq_msg_send");
	if (unlikely(zmq_msg_close(&shutdown_message) == -1))
		panic("zmq_msg_close");

	if (unlikely(pthread_join(_pool->id, NULL)))
		panic("pthread_join");

	for (unsigned int i = 0; i < _pool->workers_count; i++)
	{
		if (unlikely(pthread_join(_pool->workers[i].id, NULL)))
			panic("pthread_join");
		if (unlikely(zmq_close(_pool->workers[i].zcontrollee) == -1))
			panic("zmq_close");
		if (unlikely(zmq_close(_pool->workers[i].zresponder) == -1))
			panic("zmq_close");
	}

	pfcq_free(_pool->workers);

	if (unlikely(zmq_close(_pool->zcontrollee) == -1))
		panic("zmq_close");

	if (unlikely(zmq_close(_pool->zcontroller) == -1))
		panic("zmq_close");
	pfcq_free(_pool->zcontroller_uri);

	if (unlikely(zmq_close(_pool->zdealer) == -1))
		panic("zmq_close");
	pfcq_free(_pool->zdealer_uri);

	if (unlikely(zmq_close(_pool->zrouter) == -1))
		panic("zmq_close");
	pfcq_free(_pool->zrouter_uri);

	if (unlikely(zmq_ctx_destroy(_pool->zcontext) == -1))
		panic("zmq_ctx_destroy");

	pfcq_free(_pool->name);
	pfcq_free(_pool);
}

