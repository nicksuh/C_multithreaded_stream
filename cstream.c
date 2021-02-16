/**
 * \author Hyoung Min Suh
 */

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <pthread.h>
#include <semaphore.h>
#include "cstream.h"


#ifndef MAXJOB
#define MAXJOB 20
#endif

/**
 * basic node for the buffer, these nodes are linked together to create the buffer
 */
typedef struct cstream_node {
    struct cstream_node *next;              /**< a pointer to the next node*/
    sensor_data_t       data;               /**< a structure containing the data */
    pthread_rwlock_t    nodeLock;
    int                 thread_pass;
    sem_t               garbage_collection_lock;
    int                 jobFlag[MAXJOB];
    pthread_mutex_t     jobFlagLock;
    int                 EOS;                /**<End of Stream Flag          */
} cstream_node_t;

/**
 * a structure to keep track of the buffer
 */
struct cstream {
    pthread_rwlock_t    headTailLock; 
    pthread_t           jobs[MAXJOB];
    pthread_t           gcThread; 
    int                 jobCount;
    int                 threadCount;        //this should be same with jobcount if every job has only one thread assign to it.
    pthread_cond_t      newBroadcastCond;
    pthread_mutex_t     newBroadCastLock;
    cstream_node_t      *head;              /**< a pointer to the first node in the buffer */
    cstream_node_t      *tail;              /**< a pointer to the last node in the buffer */
};

typedef struct args {
    cstream_t           *buffer;
    cstream_node_t      *node;
    generic_func_t      func;
    int                 job_nr;
}args_t;

typedef void (*generic_func_t)(sensor_data_t);

static void garbage_collector_init(cstream_t* buffer );

static void *garbage_wrapper(void * args);

static void garbage_collector_recur(cstream_t* buffer, cstream_node_t *node );

static void *stream_wrapper(void * args);

static void stream_function_recur_rd(cstream_t * buffer, cstream_node_t *node, generic_func_t func, int job_nr);

static int cstream_EOS_insert(cstream_t *buffer);

//TODO: DEGUB MOde

int cstream_init(cstream_t **buffer, int streamJobs,int threadCount) {
    *buffer = malloc(sizeof(cstream_t));
    if (*buffer == NULL) return CSTREAM_FAILURE;
    (*buffer)->head        = NULL;
    (*buffer)->tail        = NULL;
    (*buffer)->threadCount = threadCount;
    (*buffer)->jobCount    = streamJobs;
    pthread_rwlock_init(&((*buffer)->headTailLock),NULL);
    pthread_mutex_init(&((*buffer)->newBroadCastLock), NULL);
    pthread_cond_init(&((*buffer)->newBroadcastCond),NULL);
    garbage_collector_init(*buffer);
    return CSTREAM_SUCCESS;
}

int cstream_free(cstream_t **buffer) {
    cstream_EOS_insert((*buffer));
    void * ret;
    pthread_join((*buffer)->gcThread,&ret);

    cstream_node_t *dummy;
    if ((buffer == NULL) || (*buffer == NULL)) {
        return CSTREAM_FAILURE;
    }
    pthread_mutex_destroy(&((*buffer)->newBroadCastLock));
    pthread_rwlock_destroy(&((*buffer)->headTailLock));
    pthread_cond_destroy(&((*buffer)->newBroadcastCond));
    free(*buffer);
    *buffer = NULL;
    return CSTREAM_SUCCESS;
}


void garbage_collector_init(cstream_t* buffer ){
    pthread_create(&(buffer->gcThread),NULL, garbage_wrapper, (void *) buffer);
}

void *garbage_wrapper(void * args){
    cstream_t * buffer = (cstream_t *) args;
    pthread_rwlock_rdlock(&(buffer->headTailLock));
    if(buffer->head == NULL){
        pthread_rwlock_unlock(&(buffer->headTailLock));
        pthread_mutex_lock(&(buffer->newBroadCastLock));
        while(buffer->head == NULL)
            pthread_cond_wait(&(buffer->newBroadcastCond),&(buffer->newBroadCastLock));
        cstream_node_t * tmp = buffer->head;
        pthread_mutex_unlock(&(buffer->newBroadCastLock));
        garbage_collector_recur(buffer,tmp);

    }else{
        cstream_node_t * tmp = buffer->head;
        pthread_rwlock_unlock(&(buffer->headTailLock));
        garbage_collector_recur(buffer,tmp);
    }
}


void garbage_collector_recur(cstream_t* buffer, cstream_node_t *node ) {
    cstream_node_t *tmpNode;
    // if (buffer == NULL) return cstream_FAILURE;
    // if (buffer->head == NULL) return cstream_NO_DATA;
    sem_wait(&(node->garbage_collection_lock));            //wait at the last thread(tail), it will never free one lasting tail;
    fprintf(stderr,"Garbage Collector deletion started \n");
    // for(int i = 0; i < buffer->jobCount; i ++){
    //     if(node->jobFlag[i] = 1) return cstream_FAILURE;        //this should not happen
    // }

    //Lock priority HeadTailLock >> nodeLock , deadlock advoidance
    pthread_rwlock_wrlock(&(buffer->headTailLock));
    pthread_rwlock_wrlock(&(node->nodeLock));

    if(node->EOS){          //End of Stream routine freeride jobFlag mutex :D 
        fprintf(stderr,"GarbageCollector At EOS, Joining All Job thread and Terminating \n");
        pthread_mutex_unlock(&(node->jobFlagLock));
        pthread_rwlock_unlock(&(node->nodeLock));
        sem_post(&(node->garbage_collection_lock));    //let GC read EOS

        //thread terminate
        void * ret;
        for(int i = 0; i < buffer->threadCount; i ++){
            pthread_join(buffer->jobs[i + 1],&ret);          //should return immediately
        } 

        pthread_mutex_destroy(&(node->jobFlagLock));
        pthread_rwlock_destroy(&(node->nodeLock));
        sem_destroy(&(node->garbage_collection_lock));
        
        free(node);
        pthread_exit(CSTREAM_SUCCESS);
    }


    // while(node->next == NULL){
    //     return cstream_FAILURE;         //node->next should not be null if semaphore is not 1; 
    // }

    buffer->head = buffer->head->next;
    tmpNode = node->next;
    cstream_node_t * tmpnode = node;
    pthread_rwlock_unlock(&(buffer->headTailLock));

    pthread_mutex_destroy(&(node->jobFlagLock));
    pthread_rwlock_destroy(&(node->nodeLock));
    sem_destroy(&(node->garbage_collection_lock));
    free(node);
    garbage_collector_recur(buffer,tmpNode);
}


void stream_function_init(cstream_t * buffer, generic_func_t func, int job_nr){
    args_t *myargs        = (args_t *)malloc(sizeof(args_t));
            myargs->buffer = buffer;
            myargs->func   = func;
            myargs->job_nr = job_nr;
    pthread_create(&(buffer->jobs[job_nr]),NULL, stream_wrapper, (void *)myargs );
}

void *stream_wrapper(void * args){
    args_t * myargs = (args_t *) args;
    cstream_t* buffer =  myargs->buffer;
    generic_func_t func = myargs->func;
    int job_nr = myargs->job_nr;

    free(args);     //free malloced argument
    pthread_rwlock_rdlock(&(buffer->headTailLock));
    if(buffer->head == NULL){
        pthread_rwlock_unlock(&(buffer->headTailLock));
        pthread_mutex_lock(&(buffer->newBroadCastLock));
        while(buffer->head == NULL)
            pthread_cond_wait(&(buffer->newBroadcastCond),&(buffer->newBroadCastLock));
        cstream_node_t * tmp = buffer->head;
        pthread_mutex_unlock(&(buffer->newBroadCastLock));
        stream_function_recur_rd( buffer, tmp, func,job_nr );
    }else{
        cstream_node_t * tmp = buffer->head;
        pthread_rwlock_unlock(&(buffer->headTailLock));
        stream_function_recur_rd( buffer, tmp, func,job_nr );
    }
}

void stream_function_recur_rd(cstream_t * buffer, cstream_node_t *node, generic_func_t func, int job_nr){
    pthread_rwlock_rdlock(&(node->nodeLock));
    if(node->next == NULL){
        pthread_mutex_lock(&(node->jobFlagLock));
        if(node->EOS){          //End of Stream routine freeride jobFlag mutex :D 
            pthread_mutex_unlock(&(node->jobFlagLock));
            pthread_rwlock_unlock(&(node->nodeLock));
            sem_post(&(node->garbage_collection_lock));    //let GC read EOS
            //thread terminate
            pthread_exit(CSTREAM_SUCCESS);

        }
        if(node->jobFlag[job_nr]){
            node->jobFlag[job_nr]--;            
            pthread_mutex_unlock(&(node->jobFlagLock));

            func((sensor_data_t)(node->data));          //this should be generic strictly speaking but I stick to sensor_data_t in this assignment. 

    // fprintf(stderr,"%"PRIu16, ((sensor_data_t)(node->data)).id );
            pthread_rwlock_unlock(&(node->nodeLock));
            
            pthread_mutex_lock(&(buffer->newBroadCastLock)); //Level 3 lock         
            while(node->next == NULL)
                pthread_cond_wait(&(buffer->newBroadcastCond),&(buffer->newBroadCastLock));         //wait for new node to be added
            pthread_mutex_unlock(&(buffer->newBroadCastLock));

            //recursion
            stream_function_recur_rd(buffer,node,func,job_nr);       //back to own node for safety
        }else{
            pthread_mutex_unlock(&(node->jobFlagLock));
            pthread_rwlock_unlock(&(node->nodeLock));

            
            pthread_mutex_lock(&(buffer->newBroadCastLock)); //Level 3 lock
            while(node->next == NULL)
                pthread_cond_wait(&(buffer->newBroadcastCond),&(buffer->newBroadCastLock));         //wait for new node to be added
            pthread_mutex_unlock(&(buffer->newBroadCastLock));

            //recursion
            stream_function_recur_rd(buffer,node,func,job_nr);       //back to own node for safety
        }
    }else{
        pthread_mutex_lock(&(node->jobFlagLock));
        if(node->EOS){          //End of Stream routine freeride jobFlag mutex
            pthread_mutex_unlock(&(node->jobFlagLock));
            pthread_rwlock_unlock(&(node->nodeLock));
            sem_post(&(node->garbage_collection_lock));
            //thread terminate
            pthread_exit(CSTREAM_SUCCESS);

        }
        if(node->jobFlag[job_nr]){
            node->jobFlag[job_nr]--;            
            pthread_mutex_unlock(&(node->jobFlagLock));


            func((sensor_data_t)node->data);          //this should be generic strictly speaking but I stick to sensor_data_t in this assignment. 
            cstream_node_t *tmp = node->next;

            node->thread_pass --;
            if(node->thread_pass == 0)
                sem_post(&(node->garbage_collection_lock));

            pthread_rwlock_unlock(&(node->nodeLock));
            stream_function_recur_rd(buffer,tmp,func,job_nr);
        }else{
            pthread_mutex_unlock(&(node->jobFlagLock));
            cstream_node_t *tmp = node->next;

            node->thread_pass --;
            if(node->thread_pass == 0)
                sem_post(&(node->garbage_collection_lock));

            pthread_rwlock_unlock(&(node->nodeLock));
            stream_function_recur_rd(buffer,tmp,func,job_nr);
        }
    }
}

//thread safe
int cstream_insert(cstream_t *buffer, sensor_data_t *data) {
    cstream_node_t *dummy;
    if (buffer == NULL) return CSTREAM_FAILURE;
    dummy = malloc(sizeof(cstream_node_t));
    if (dummy == NULL) return CSTREAM_FAILURE;

    //initializing new node with mutex, readwrite,  and semaphore locks. 
    dummy->data = *data;
    dummy->next = NULL;
    dummy->thread_pass = buffer->threadCount;
    dummy->EOS = 0;                 // not end of stream 
    pthread_rwlock_init(&(dummy->nodeLock),NULL);
    pthread_mutex_init(&(dummy->jobFlagLock),NULL);

    sem_init(&(dummy->garbage_collection_lock),0, 0);
    
    for(int i = 0; i < buffer->jobCount; i ++){
        dummy->jobFlag[i + 1] = 1;
    }
    //Lock priority HeadTailLock >> nodeLock , deadlock advoidance
    pthread_rwlock_wrlock(&(buffer->headTailLock));
    if (buffer->tail == NULL) // buffer empty (buffer->head should also be NULL
    {
        pthread_mutex_lock(&(buffer->newBroadCastLock));    //BroadCast Routine
        buffer->head = buffer->tail = dummy;
        pthread_cond_broadcast(&(buffer->newBroadcastCond));
        pthread_mutex_unlock(&(buffer->newBroadCastLock));
        
    }else{ // buffer not empty
        //Header-> BroadCast -> nodeLock order always 
        pthread_mutex_lock(&(buffer->newBroadCastLock));    //BroadCast Routine
        pthread_rwlock_wrlock(&(buffer->tail->nodeLock));
        cstream_node_t* tmp = buffer->tail;
        buffer->tail->next = dummy;
        buffer->tail       = dummy;
        pthread_cond_broadcast(&(buffer->newBroadcastCond));
        pthread_rwlock_unlock(&(tmp->nodeLock));
        pthread_mutex_unlock(&(buffer->newBroadCastLock));
    }
    pthread_rwlock_unlock(&(buffer->headTailLock));
    return CSTREAM_SUCCESS;
}


int cstream_EOS_insert(cstream_t *buffer) {     //this is the termination Call
    cstream_node_t *dummy;
    if (buffer == NULL) return CSTREAM_FAILURE;
    dummy = malloc(sizeof(cstream_node_t));
    if (dummy == NULL) return CSTREAM_FAILURE;

    //initializing new node with mutex, readwrite,  and semaphore locks. 
    dummy->next = NULL;
    dummy->thread_pass = buffer->threadCount;
    dummy->EOS = 1;                 // not end of stream 
    pthread_rwlock_init(&(dummy->nodeLock),NULL);
    pthread_mutex_init(&(dummy->jobFlagLock),NULL);

    sem_init(&(dummy->garbage_collection_lock),0, 0);
    
    for(int i = 0; i < buffer->jobCount; i ++){
        dummy->jobFlag[i + 1] = 1;
    }
    //Lock priority HeadTailLock >> nodeLock , deadlock advoidance
    pthread_rwlock_wrlock(&(buffer->headTailLock));
    if (buffer->tail == NULL) // buffer empty (buffer->head should also be NULL
    {
        pthread_mutex_lock(&(buffer->newBroadCastLock));    //BroadCast Routine
        buffer->head = buffer->tail = dummy;
        pthread_cond_broadcast(&(buffer->newBroadcastCond));
        pthread_mutex_unlock(&(buffer->newBroadCastLock));
        
    }else{ // buffer not empty
        //Header-> BroadCast -> nodeLock order always 
        pthread_mutex_lock(&(buffer->newBroadCastLock));    //BroadCast Routine
        pthread_rwlock_wrlock(&(buffer->tail->nodeLock));
        cstream_node_t* tmp = buffer->tail;
        buffer->tail->next = dummy;
        buffer->tail       = dummy;
        pthread_cond_broadcast(&(buffer->newBroadcastCond));
        pthread_rwlock_unlock(&(tmp->nodeLock));
        pthread_mutex_unlock(&(buffer->newBroadCastLock));
    }
    pthread_rwlock_unlock(&(buffer->headTailLock));
    return CSTREAM_SUCCESS;
}






void *my_print(sensor_data_t data){
    fprintf(stderr,"function call %"PRIu16 " id is called \n", ((sensor_data_t)data).id );
}

void *my_print1(sensor_data_t data){
    fprintf(stderr,"this is second thread function call %"PRIu16 " id is called \n", ((sensor_data_t)data).id );
}

int main() {
    cstream_t * my_stream;
    cstream_init(&my_stream,2,2);
    stream_function_init(my_stream,my_print,1);
    stream_function_init(my_stream,my_print1,2);
    for(int i = 1; i < 10; i ++ ){
        sensor_data_t mydata;
        mydata.id    = i;
        mydata.ts    = time(NULL);
        mydata.value = 1232;
        cstream_insert(my_stream,&mydata);
    }
    cstream_free(&my_stream);
}

