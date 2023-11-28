
#include "tinyos.h"
#include "kernel_streams.h"
//#include "kernel_pipe.h"
#include "kernel_cc.h"
#include <stdbool.h>


/********************** File Ops Used **************************/
static file_ops writer_file_ops = {

	.Open = NULL,
	.Read = reader_blocked,
	.Write = pipe_write,
	.Close = pipe_writer_close

};


static file_ops reader_file_ops = {

	.Open = NULL,
	.Read = pipe_read,
	.Write = writer_blocked,
	.Close = pipe_reader_close

};


void initialize_PIPE_CB (PIPE_CB *pipe_cb, pipe_t* pipe, Fid_t* fid, FCB** fcb) 
{
  	pipe->read = fid[0];
  	pipe->write = fid[1];
  	
	pipe_cb->reader = fcb[0];
  	pipe_cb->writer = fcb[1];

  	pipe_cb->has_space = COND_INIT;
  	pipe_cb->has_data = COND_INIT;

 	pipe_cb->r_position = 0;
  	pipe_cb->w_position = 0;

  	pipe_cb->buff_bytes = 0;

  	/*for(int i = 0; i < PIPE_BUFFER_SIZE; i++)
    	pipe_cb->BUFFER[i] = 0;*/
}

/* Allocate memory for pipe control block */
PIPE_CB* acquire_PIPE_CB()
{
  	PIPE_CB* pipe_cb = (PIPE_CB*)xmalloc(sizeof(PIPE_CB));
  	return pipe_cb;
}


void release_PIPE_CB(PIPE_CB* pipe_cb)
{
  	free(pipe_cb);
}

/* Syscall for pipes, Returns 0 on success and -1 on error */
int sys_Pipe(pipe_t* pipe)
{	
	Fid_t fid[2];
	FCB* fcb[2];

	if(FCB_reserve(2, fid, fcb) != 1) {
		
		return -1;
	}

	PIPE_CB* pipe_cb = acquire_PIPE_CB();
	initialize_PIPE_CB(pipe_cb, pipe, fid, fcb);

	fcb[0]->streamobj = pipe_cb;
	fcb[1]->streamobj = pipe_cb;

	fcb[0]->streamfunc = &reader_file_ops;
	fcb[1]->streamfunc = &writer_file_ops;

	return 0;
}


/************************* Writer Ops *************************/
int pipe_write(void* pipecb_t, const char *buf, unsigned int n) 
{
	PIPE_CB * pipe_cb = (PIPE_CB *) pipecb_t;

	int next;
	int i;

	if (pipe_cb->writer == NULL || pipe_cb->reader == NULL){
			
		return -1;
	}

        while (pipe_cb->buff_bytes == PIPE_BUFFER_SIZE && pipe_cb->reader != NULL){  	// if the head + 1 == tail, circular buffer is full
    		kernel_broadcast(&pipe_cb->has_data);
		kernel_wait(&pipe_cb->has_space, SCHED_PIPE);        	
    	}

    	if (pipe_cb->reader == NULL) {
    		return -1;
    	}

    	int bytes_to_write;

    	if (n < (PIPE_BUFFER_SIZE - pipe_cb->buff_bytes)) {
    		bytes_to_write = n;

    	} else {
    		bytes_to_write = PIPE_BUFFER_SIZE - pipe_cb->buff_bytes;
    	}


    	for (i = 0 ; i < bytes_to_write; i++){
		next = pipe_cb->w_position;
    		pipe_cb->BUFFER[next] = buf[i];  									// Load data and then move
    		pipe_cb->w_position = (next+1)%PIPE_BUFFER_SIZE; 
    		pipe_cb->buff_bytes++;
	}


    	kernel_broadcast(&pipe_cb->has_data);
	
	return i;
}


int reader_blocked(void* pipecb_t, char *buf, unsigned int n)
{
	return -1;
}


int pipe_writer_close(void* _pipecb) 
{	
	PIPE_CB* pipe_cb = (PIPE_CB*)_pipecb;

	if(pipe_cb->writer == NULL)
		return 0;

	pipe_cb->writer = NULL;

	if (pipe_cb->reader != NULL){

		kernel_broadcast(&pipe_cb->has_data);

	} else {

		release_PIPE_CB(pipe_cb);
	}

	return 0;
}


/********************* Reader Ops *********************/
int pipe_read(void* pipecb_t, char *buf, unsigned int n) 
{
	PIPE_CB * pipe_cb = (PIPE_CB *) pipecb_t;
	int next;
	int i;

	if (pipe_cb->reader == NULL){
      
    	return -1;
  	}

    	while ((pipe_cb->buff_bytes == 0) && pipe_cb->writer != NULL ) { // if the head == tail, we don't have any data
		kernel_broadcast(&pipe_cb->has_space);
		kernel_wait(&pipe_cb->has_data, SCHED_PIPE);
    	}

    	if (pipe_cb->buff_bytes == 0) 
		return 0;
	

	//if (pipe_cb->w_position == pipe_cb->r_position || pipe_cb->buff_bytes == 0) //&& pipe_cb->writer == NULL
    	//return -1;

    	int bytes_to_read;

    	if (n < pipe_cb->buff_bytes) {
    		bytes_to_read = n;

    	} else {
      		bytes_to_read = pipe_cb->buff_bytes;
    	}

    	for (i = 0; i < bytes_to_read; i++) {
		next = (pipe_cb->r_position + 1)%PIPE_BUFFER_SIZE; // next is where tail will point to after this read.
    		buf[i] = pipe_cb->BUFFER[pipe_cb->r_position]; 	// Read data and then move
    		pipe_cb->r_position = next;  // tail to next offset.
	
		pipe_cb->buff_bytes--;	
	}

	kernel_broadcast(&pipe_cb->has_space);

    return i;
}


int writer_blocked(void* pipecb_t, const char *buf, unsigned int n)
{
	return -1;
}


int pipe_reader_close(void* _pipecb) 
{
	PIPE_CB* pipe_cb = (PIPE_CB*)_pipecb;

	if(pipe_cb->reader == NULL)
		return 0;

	pipe_cb->reader = NULL;

	if (pipe_cb->writer != NULL){

		kernel_broadcast(&pipe_cb->has_space);

	} else {

		release_PIPE_CB(pipe_cb);
	}

	return 0;
}

