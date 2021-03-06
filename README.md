# C Thread Stream
A easy to use multithreading thread pool library for C.
It is a handy stream-like job scheduler with an automatic garbage collector for non I/O bound computation.

* scalability
* easy to integrate
* easy to use

## Use Case

```C
void *my_print(sensor_data_t data){
    fprintf(stderr,"function call %"PRIu16 "- id is called \n", ((sensor_data_t)data).id );
}

void *my_print1(sensor_data_t data){
    fprintf(stderr,"this is second thread function call %"PRIu16 "- id is called \n", ((sensor_data_t)data).id );
}
```
declare a thread-safe, reentrant function

```C
int main() {
    cstream_t * my_stream;
    cstream_init(&my_stream,2,2);
    stream_function_init(my_stream,my_print,1);
    stream_function_init(my_stream,my_print1,2);
    for(int i = 1; i < 20000; i ++ ){
        sensor_data_t mydata;
        mydata.id    = i;
        mydata.ts    = time(NULL);
        mydata.value = 1232;
        cstream_insert(my_stream,&mydata);
    }
    cstream_free(&my_stream);
}
```


initiate cstream struct, with 2 thread and 2 jobs. (2 kinds of function.)
it is possible to initiate more thread than jobs. If you want multiple thread on one job you can initiate
multiple thread with stream_function_init with same job number. The stream will do correct job stealing for you.
```C
    cstream_t * my_stream;
    cstream_init(&my_stream,2,2);
```

Initiate thread to the stream with stream_function_init( pointer to cstream struct, job function, job identification number). 
no work is done at this stage. A thread is initialized and attatched to the stream.

job identification number should start from 1 and increase consecutively.

```C
    stream_function_init(my_stream,my_print,1);
    stream_function_init(my_stream,my_print1,2);
```

insert data to be processed by job function. (function should not write to the data element, writable + dependancy schedulable job initializer comming soon)
already initialized job functions will process data automatically.
A garbage collector will free resources when all job functions have visited and left the data stream element.
```C
        cstream_insert(my_stream,&mydata);
```
Example output
```
./cstream
function call 1 id is called 
function call 2 id is called 
function call 3 id is called 
function call 4 id is called 
function call 5 id is called 
function call 6 id is called 
this is second thread function call 1 id is called 
this is second thread function call 2 id is called 
this is second thread function call 3 id is called 
this is second thread function call 4 id is called 
Garbage Collector deletion started 
Garbage Collector deletion started 
Garbage Collector deletion started 
Garbage Collector deletion started 
this is second thread function call 5 id is called 
this is second thread function call 6 id is called 
Garbage Collector deletion started 
Garbage Collector deletion started 
Garbage Collector deletion started 
GarbageCollector At EOS, Joining All Job thread and Terminating 
```
Valgrind check after 1,000,000 insertion.
```
GarbageCollector At EOS, Joining All Job thread and Terminating 
==970== 
==970== HEAP SUMMARY:
==970==     in use at exit: 0 bytes in 0 blocks
==970==   total heap usage: 1,000,011 allocs, 1,000,011 frees, 256,002,926 bytes allocated
==970== 
==970== All heap blocks were freed -- no leaks are possible
==970== 
==970== For lists of detected and suppressed errors, rerun with: -s
==970== ERROR SUMMARY: 0 errors from 0 contexts (suppressed: 0 from 0)
```

## Make
Previously due to tail recursion, -O3 level optimization was needed. However, it is no longer needed.
```Make
test:
	gcc -O3 -g cstream.h cstream.c -lpthread -o cstream
	./cstream

test_full:
	gcc -O3 -g cstream.h cstream.c -lpthread -o cstream
	valgrind --leak-check=full \
         --show-leak-kinds=all \
         --track-origins=yes \
		./cstream
```

## Remarks
This is unfinished work, A non-side effect functions with dependency will be supported later.
Currently dynamically allocated data element is not supported but the functionality will be added soon. 
