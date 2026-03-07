# SpIOpen Node

A combination of SpIOpen Router and CanOpenNode on one device.

## Overview

This project aims to be tightly coupled with the open source [CanOpenNode](https://github.com/CANopenNode/CANopenNode) project.
A combination of SpIOpen Router and CanOpenNode on one device allows settings within the CanOpen object dictionary to control the behavior and configuration of the SpIOpen Router.
A base library is implemented to manage the shared communicaiton properties, and a master and slave library are also implemented as a launching point for further development.
Real time behavior is achieved through the CMSIS-RTOSv2 interface, with a specific implementation (including FreeRTOS wrapper) selected at build time.