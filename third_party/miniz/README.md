# miniz 3.1.2

ZIP reader/inflate sources from the upstream 3.1.2 tag:
https://github.com/richgel999/miniz/tree/3.1.2

Upstream sources are unmodified. `miniz_export.h` is a local static-link definition. Deflate, archive writing, zlib APIs, and compatibility aliases are disabled by CMake. Jellyflower uses the memory ZIP reader for user-provided cursor imports, with size limits, CRC verification, and fixed output paths. No command-line extractor or runtime download is required.

MIT license: see LICENSE; its full notice is also included in the distributed THIRD_PARTY_NOTICES.md.
