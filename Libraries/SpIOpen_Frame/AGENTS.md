# SpIOpen

This project is a platform-agnostic library to facilitate working with data frames in the SpIOpen protocol. The exact format of the protocol is detailed in @FrameFormat.md. The frame is typically held in a byte array buffer allocated from a common frame pool and also parsed into a helpful structure.

## Coding Style
- Optimize for use on resource constrained devices such as microcontrollers
- Do not use dynamic memory allocation
- Prefer to use tools from the embedded template library (etl namespace) instead of re-creating small helper functions or basic structures.
- This project is coded in modern c++ (17) and generally follows google style guidelines and testing (see .clang-format at project root), although some interoperability with legacy pure-C or MISRA-C libraries is required.