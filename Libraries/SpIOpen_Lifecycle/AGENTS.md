# SpIOpen Lifecycle

This project is a platform-agnostic library to facilitate transitioning a software component through a configuration, initialization, and finally active state (then back down again when no longer needed).

## Coding Style
- Optimize for use on resource constrained devices such as microcontrollers
- Do not use dynamic memory allocation. There are some exceptions for initialization (not not operational) heap allocation, usually behind a KConfig option
- Prefer to use tools from the embedded template library (etl namespace) instead of re-creating small helper functions or basic structures.
- This project is coded in modern c++ (17) and generally follows google style guidelines and testing (see .clang-format at project root), although some interoperability with legacy pure-C or MISRA-C libraries is required.
