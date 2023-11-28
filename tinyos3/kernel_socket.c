
#include "tinyos.h"
#include "kernel_streams.h"

SOCKET_CB* PORT_MAP[MAX_PORT] = {NULL};

static file_ops socket_file_ops = {
	
	.Open = NULL,
	.Read = socket_read,
	.Write = socket_write,
	.Close = socket_close
};

void initialize_SOCKET_CB(SOCKET_CB* socket_cb, port_t port) 
{
	socket_cb->refcount=0;
	socket_cb->fcb = NULL;
	socket_cb->type = SOCKET_UNBOUND;
	socket_cb->port = port;
}

SOCKET_CB* acquire_SOCKET_CB()
{
  	SOCKET_CB* socket_cb = (SOCKET_CB*)xmalloc(sizeof(SOCKET_CB));
  	return socket_cb;
}


void release_SOCKET_CB(SOCKET_CB* socket_cb)
{
  	free(socket_cb);
}


int socket_read(void* socketcb_t, char *buf, unsigned int n){

	SOCKET_CB* socket_cb = (SOCKET_CB*) socketcb_t;

	if(socket_cb->type == SOCKET_PEER){
		return pipe_read(socket_cb->peer_s->read_pipe, buf, n);
	}
	return -1;
}


int socket_write(void* socketcb_t, const char *buf, unsigned int n){

	SOCKET_CB* socket_cb = (SOCKET_CB*) socketcb_t;

	if(socket_cb->type == SOCKET_PEER){
		return pipe_write(socket_cb->peer_s->write_pipe, buf, n);
	}
	return -1;
}


int socket_close(void* socketcb){

	return 0;
}


Fid_t sys_Socket(port_t port)
{
	if(port > MAX_PORT || port < 0){
		return NOFILE;
	}

	Fid_t fid;
	FCB* fcb;

	if(FCB_reserve(1, &fid, &fcb) != 1){
		return NOFILE;
	}

	SOCKET_CB* socket_cb = acquire_SOCKET_CB();
	initialize_SOCKET_CB(socket_cb, port);

	socket_cb->unbound_s = (U_SOCKET *)xmalloc(sizeof(U_SOCKET));

	fcb->streamobj = socket_cb;

	fcb->streamfunc = &socket_file_ops;

	return fid;
}

int sys_Listen(Fid_t sock)
{
	//SOCKET_CB* socket_cb;
	Fid_t fid = sock;
	//FCB* fcb;
	
	FCB* fcb = get_fcb(fid);
	SOCKET_CB* socket_cb = fcb->streamobj;

	if(fcb == NULL || socket_cb == NULL){
		return NOFILE;
	}

	if(socket_cb->port == NOPORT){
		return -1;
	}

	if(fid > MAX_FILEID || fid < 0 || socket_cb->type != SOCKET_UNBOUND || PORT_MAP[socket_cb->port] != NULL) {
		return -1;
	}

	socket_cb->type = SOCKET_LISTENER;
	PORT_MAP[socket_cb->port] = socket_cb;
	
	socket_cb->listener_s = (L_SOCKET *)xmalloc(sizeof(L_SOCKET));
	free(socket_cb->unbound_s);

	socket_cb->listener_s->req_available = COND_INIT;
	rlnode_init(&socket_cb->listener_s->queue, NULL);

	socket_cb->refcount++;

	return 0;
}


Fid_t sys_Accept(Fid_t lsock)
{
	return NOFILE;
}


int sys_Connect(Fid_t sock, port_t port, timeout_t timeout)
{
	return -1;
}


int sys_ShutDown(Fid_t sock, shutdown_mode how)
{

	SOCKET_CB* socket_cb;
	Fid_t fid = sock;
	FCB* fcb;

	fcb = get_fcb(fid);
	socket_cb = fcb->streamobj;

	if(fid < 0 || fid > MAX_FILEID){
		return -1;
	}

	if(socket_cb->type != SOCKET_PEER){
		return -1;
	}

	switch(how){
		case SHUTDOWN_READ: 
		return pipe_reader_close(socket_cb->peer_s->read_pipe);
		case SHUTDOWN_WRITE:
			return pipe_writer_close(socket_cb->peer_s->write_pipe);
		case SHUTDOWN_BOTH:
			pipe_reader_close(socket_cb->peer_s->read_pipe);
			pipe_writer_close(socket_cb->peer_s->write_pipe);
			return 0;
	}

	return -1;
}

