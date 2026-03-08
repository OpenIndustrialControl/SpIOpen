# SpIOpen Frame Broker

This class is responsible for routing produced frames to all subscribing consumers.

## Overview

Any thread can publish a `FrameMessage` to the router by enqueing the message to the router's inbox via its public Publish function. The router then restructures the message as a Reference Counted Message and pushes it to the receiving queue of each subscriber that is registered to accept that MessageType. The reference count is increased by one just before enqueueing the message to an approved subscriber. As each subscriber processes the message they *must* call the message function to decrement the counter, with an additional feature of releasing the Frame back to the Frame Pool once the counter has reached zero.

## Design Considerations

- One producer routes to many consumers - use publisher/subscriber model
- Producers mark their messages with a message ID, as well as a source struct reference (source ID, debug string, pointer)
- The same message structure is used to send frames from publishers to the broker as from the broker to subscribers. In that way the broker itself is a 1:1 subscriber to every publishing thread.
- The frame messages are reference counted, such that when there are no remaining references to the frame it is returned to the pool. The producing thread, the broker, and all consuming threads each need a reference for the time they spend with the message.

## Implementation Details

- Thread and interrupt safe RTOS queues are used for all producer->consumer pairings (queuing to the broker and queueing to the subscribers).
- Only a pointer to a FrameMessage is actualyl enqueued to keep the copy semantics and reference counting in check.
- Because the queues are written in raw-C, some manually referencing counting is required for messages to be enqueued and dequened properly.