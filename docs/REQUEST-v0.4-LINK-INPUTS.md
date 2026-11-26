# Private request to the author of the Android port

Subject — Relink inputs for the v0.4 ARM64 Android build

Hi, I maintain an experimental Android recovery branch of the N.O.V.A. HD port. We are trying to test changes to the open runtime while preserving the translated game and existing saves. Our reference is the v0.4 Android release at commit 930cd60. Its libnovahd.so has SHA-256 ab6a0489e69bce594bd9eb32abb42eaf30b73ea577adbc7406b1c7ee9dcfc24c.

Do you still have a build directory or cache from that release? We are looking for the 48 AArch64 relocatable translation objects (shard00 through shard47), the matching dispatch-runtime object, image_meta.c and the SHA-256 of the corresponding image.bin. We already have the packed image from the release. Alternatively, the generated shard C files, dispatcher C, protos.h, matching recomp.h and image metadata would allow us to compile the objects ourselves.

The compiler target, NDK version, normal/checked translation mode, generator revision, build commands and checksums would help us verify compatibility. If any files can legally be shared for private testing, could you let us know which ones and propose an authorized private transfer? Please keep proprietary binaries and game resources out of public issues. Build metadata and instructions alone would also help if sharing generated inputs is restricted.

Thank you for your work on the port.
