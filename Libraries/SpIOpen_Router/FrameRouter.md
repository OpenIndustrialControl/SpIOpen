# SpIOpen Frame Router

This class is responsible for routing produced frames to all subscribing consumers. 

## Overview

## Design Considerations

- One producer routes to many consumers - use publisher/subscriber model
- The producers generally make the same type of message, so you can choose to mark messages either by who produced them (ie MISO chain input port) or what type of message they are (ie master-bound frame). Producers like gateways are the typical exception for multiple similar but independent producers.

## Implementation Details