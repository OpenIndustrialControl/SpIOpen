# SpIOpen

This project is a platform-agnostic library to facilitate routing data frames in the SpIOpen protocol. 
It involves several memory pools that pre-allocate Messages, and a single Broker that handles a publish/subscribe model of message delivery.
It relies on a real time operating system implementing the CMSIS-RTOSv2 API, with a specific heavy reliance on message queues.
The structure and naming is heavily inspired by the Embeded Template Library Messaging Frameworks, specifically its reference counted message, memory pool, and message broker elements. However it is re-implemented here to rely specifically on a queue-based memory pool for publishers and queue based message processing in subscribers to match the real time multi threaded capabilities of the RTOS.

## Coding Style
- Optimize for use on resource constrained devices such as microcontrollers
- Do not use dynamic memory allocation. There are some exceptions for initialization (not not operational) heap allocation, usually behind a KConfig option
- Prefer to use tools from the embedded template library (etl namespace) instead of re-creating small helper functions or basic structures.
- This project is coded in modern c++ (17) and generally follows google style guidelines and testing (see .clang-format at project root), although some interoperability with legacy pure-C or MISRA-C libraries is required.