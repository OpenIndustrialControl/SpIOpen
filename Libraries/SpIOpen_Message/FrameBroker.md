# SpIOpen Frame Broker

This class is responsible for distributing published frames to all subscribing subscribers.

## Overview

Any thread can publish a `FrameMessage` to the broker by enqueueing the message to the broker's inbox via its public Publish function. The broker then restructures the message as a reference-counted message and pushes it to the receiving queue of each subscriber that is registered to accept that MessageType. The reference count is increased by one just before enqueueing the message to an approved subscriber. As each subscriber processes the message they *must* call the message release function to decrement the counter, with an additional feature of releasing the Frame back to the Frame Pool once the counter has reached zero.

## Design Considerations

- One publisher distributes to many subscribers—use a publisher/subscriber model.
- Publishers mark their messages with a message ID, as well as a source struct reference (source ID, debug string, pointer).
- The same message structure is used to send frames from publishers to the broker as from the broker to subscribers. In that way the broker itself is a 1:1 subscriber to every publishing thread.
- The frame messages are reference counted, such that when there are no remaining references to the frame it is returned to the pool. The publishing thread, the broker, and all subscriber threads each need a reference for the time they spend with the message.
- Subscribers register with their subscriber handle type. Beyond pointing to their queue, it contains an atomic message ID filter (bitmask for accepted message types). It also contains an atomic enqueue error counter that the broker should increment if it ever fails to enqueue a message. The subscription can fail for multiple reasons.
- The Broker can only add or remove subscribers when it is not active. The state of the broker is managed by some higher level application using Start/Stop functions and observing the state. Susbcribers that do not with to receive messages can set their atomic message filter to zero (None).
- There are not many types of messages by default. Flags in the least significant half of the messageType enum are reserved, flags in the most significant half are available for custom uses.

## Implementation Details

- Thread- and interrupt-safe RTOS queues are used for all publisher→subscriber pairings (queuing to the broker and queuing to the subscribers). Every function that publishes or subscribes a message must be thread safe from an ISR.
- Only a pointer to a FrameMessage is actualyl enqueued to keep the copy semantics and reference counting in check.
- Because the queues are written in raw-C, some manually referencing counting is required for messages to be enqueued and dequened properly.
- Mailbox queues are a wrapper around the RTOS queue structure. The function of the wrapper is to both shuffle between C and C++ style code, as well as manage reference counting that might get mixed up during queue copying.
- If the broker is unable to enqueue a Message to a specific subscriber, it must increment the enqueue error counter field of the subscriber's handle structure, then also increment a global enqueue error counter that is tracked within the Broker itself. Processing continues to the next potential subscriber after an enqueue error. External processes (canopennode) may reset the error counters so they should be implemented atomically
- Message Payloads (FrameBuffers) must not be modified after they are published. Therefore an Interface exists just to wrap the message into a read-only message. All of the subscriber queues use this read only message interface class instead of the underlying concerete class that the frame pool exposes.
- The Frame Broker has one thread that blocks on its own subscribing, inbound mailbox queue (that all publishers use to publish new messages) in an endless loop. All susbcribers are expected to implement similar blocking (not polling) behavior on their own inbound mailbox queues. The RTOS thread handle/start/stop is all managed internally to keep the interface C++ styled.
- The broker can be active or not (because the thread is running or not). There is a Broker State enum observable by the higher level app and Start/Stop/Reset functions (Reset will clear all subscriptions).
- The subscription function can fail and return a custom error enum (all subscription slots are taken, broker is active, etc)

## Lifecycle Interface Integration

`FrameBroker` implements `ILifecycleComponent<FrameBrokerConfig, LifecycleError>` from `SpIOpen_Lifecycle`.

- `Configure()` validates and normalizes broker thread and inbox mailbox configuration.
- `Initialize()` allocates/assigns stack resources and initializes the internal inbox mailbox.
- `Start()` creates and starts the internal routing thread.
- `Stop()` terminates routing activity and drains inbox references safely.
- `Deinitialize()` tears down broker-owned resources and returns to configured state.
- `Reset()` clears subscriptions and runtime counters when lifecycle state allows it.

Shared lifecycle state and error semantics are documented in `SpIOpen_Lifecycle/README.md`.
