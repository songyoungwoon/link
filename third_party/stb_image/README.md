# stb_image — HDR-only environment import

Pinned upstream commit and original-file digests are recorded in SOURCE.json.
The unmodified header is compiled in one private translation unit with
STBI_ONLY_HDR, STBI_NO_STDIO and a 2048-pixel axis limit. LINK selects the MIT
alternative of the original dual-license text in LICENSE. The bounded wrapper
also preflights the complete Radiance scanline stream before invoking stb_image.
