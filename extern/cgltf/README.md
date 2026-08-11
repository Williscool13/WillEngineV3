# cgltf

Single-file glTF 2.0 parser. https://github.com/jkuhlmann/cgltf

Pinned at v1.15, unmodified. MIT license (notice at end of cgltf.h, mirrored in NOTICES.txt).

`CGLTF_IMPLEMENTATION` lives in `src/editor/asset-generation/cgltf_impl.cpp`; all allocations route through `cgltf_options.memory` at the call site.
