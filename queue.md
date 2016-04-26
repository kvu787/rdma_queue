# Specification

We implement a queue using RDMA compare-and-swap and fetch-and-add operations.
It borrows ideas from the CRQ algorithm. Because it uses a single fixed size
circular array, it is non-blocking only for buffered enqs, in contrast to LCRQ
which is non-blocking for an arbitrary number of enqs [1].

[1] http://www.cs.technion.ac.il/~mad/publications/ppopp2013-x86queues.pdf

It satisfies the following properties:
- Distributed
- Buffered (enqs are non-blocking if there are fewer than `BUFFER_LEN`
  outstanding writes)
- Multi-producer
- Single-consumer
- Linearizable
  - A weaker, serializable, but more performant queue can be implemented with a small adjustment to
    the enq operation

# C implementation

```c
// compile with cc -std=c11 -Wall queue.c
#include <stdbool.h>
#include <stdlib.h>

int faa(int *addr) {
  // wraps fetch-and-add instruction/call
  return 0;
}

bool cas(int *addr, int old, int new) {
  // wraps compare-and-swap instruction/calls
  return false;
}

void yield() {
  // yields processor
}

// block states
const int Free = 0;    // block ready to be written to
const int Writing = 1; // producer is writing to this block
const int Used = 2;    // block ready to be read from
const int Reading = 3; // consumer is reading from this block

// queue is composed of a circular array of blocks
struct block_t {
	int state;  // see block states above
	int ticket; // the ticket that the next enqueuer will take
	int turn;   // the enqueuer with ticket == turn can proceed
	int data;   // actual data being enqueued/dequeued
};

// queue_t tracks producer and consumer offsets
struct queue_t {
	int buffer_size;
	int producer_offset;    // next block to write to
	int consumer_offset;    // next block to read from
	struct block_t *buffer; // array of blocks
};

// creates a new queue with the specified buffer size
struct queue_t *NewQueue(int buffer_size) {
	// init queue
	struct queue_t *queue = malloc(sizeof(struct queue_t));
	queue->buffer_size = buffer_size;
	queue->producer_offset = 0;
	queue->consumer_offset = 0;
	queue->buffer = malloc(buffer_size * sizeof(struct block_t));

	// init buffer blocks
	for (int i = 0; i < buffer_size; ++i) {
		struct block_t *block = &queue->buffer[i];
		block->state = Free;
		block->ticket = 0;
		block->turn = 0;
		block->data = 0;
	}

  return queue;
}

void Enq(struct queue_t *q, int data) {
	// get next block to write
	int offset = faa(&q->producer_offset) % q->buffer_size;
	struct block_t *block = &q->buffer[offset];
	// take the next ticket
	int myTurn = faa(&block->turn);
	// wait for turn
	while (myTurn != block->ticket) {
		yield();
	}
	// Free -> Writing, to prevent reads
	cas(&block->state, Free, Writing);
	// write data
	block->data = data;
	// Writing -> Used, to allow reads
	cas(&block->state, Writing, Used);
}

int Deq(struct queue_t *q) {
	// get next block to read
	int offset = faa(&q->consumer_offset) % q->buffer_size;
	struct block_t *block = &q->buffer[offset];
	// wait for state == Used before reading
	while (!cas(&block->state, Used, Reading)) {
		yield();
	}
	// read data
	int retval = block->data;
	// Reading -> Free, to allow writes
	cas(&block->state, Reading, Free);
	// let the next producer continue
	faa(&block->turn);
	return retval;
}
```

# Performance

If there are items to deq:
- Deqs perform O(1) reads, writes, and uncontended atomic operations
  - This means O(1) round trips for RDMA

If the buffer is not full:
- Enqs are non-blocking
- Enqs perform O(1) reads, writes, and uncontended atomic operations
  - This means O(1) round trips for RDMA

If the buffer is full
- Enqs block
- Enqs are still fair (due to turn taking mechanism)

# Illustration

The following example shows the changes to buffer state from a series of enqs
and deqs on a buffer of length 4. We abbreviate the state names as

F = free  
W = writing  
U = used  
R = reading  

---

Buffer starts as empty.  
All blocks start in free state.  
Consumer (*) and producer (^) offsets point to block 0.  

```
F  F  F  F
*
^
```

a, b, and c start enq concurrently.

```
Wa Wb Wc F
*
         ^
```

a and c finish enq.

```
Ua Wb Uc F
*
         ^
```

d starts and finishes enq. Buffer is now full.

```
Ua Wb Uc Ud
*
^
```

b finishes enq.  
Consumer starts deq.

```
R  Ub Uc Ud
*
^
```

6 more machines enq.  
Buffer is still full, so machines take tickets and wait for their respective
turns.

```
i  j
e  f  g  h
R  Ub Uc Ud
*
      ^
```

Consumer finishes 3 deqs.  
Enqs proceed in order of arrival.

```
i   j     h
Ue Uf Ug Ud
         *
      ^
```

Consumer finishes 3 deqs.

```
Ui Uj Ug Uh
      *
      ^
```

Consumer finishes 2 deqs.

```
Ui Uj F  F
*
      ^
```

Consumer finishes 2 deqs.  
Buffer is now empty.

```
F  F  F  F
      *
      ^
```
