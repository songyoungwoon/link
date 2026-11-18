# cgltf

Unmodified official cgltf v1.15 single-header loader by Johannes Kuhlmann and contributors.

- Upstream: https://github.com/jkuhlmann/cgltf/tree/v1.15
- License: MIT; see [LICENSE](LICENSE).
- Pinned download URLs and SHA-256 checksums: [SOURCE.json](SOURCE.json).

LINK compiles the implementation in `native/src/model.cpp`. The LINK adapter only accepts bounded self-contained GLB 2 assets; it never invokes cgltf external-file or URI loading. The vendored library is broader than LINK's initial supported model profile.
