# HarfBuzz 14.3.0 (pruned)

Upstream: https://github.com/harfbuzz/harfbuzz, release 14.3.0.
Used only at bake time by the editor for Slug glyph-outline encoding (hb-gpu).

Only `src/` files in the transitive `#include "..."` closure of the three compiled
TUs (`harfbuzz.cc`, `hb-gpu.cc`, `hb-gpu-draw.cc`) are vendored, plus the standalone
`hb-gpu-*.glsl/.hlsl` reference shaders and `COPYING`. Platform-integration `.cc`
files (coretext/directwrite/gdi/glib/graphite2/uniscribe/ft) are present because the
amalgamation includes them, but compile empty without their `HAVE_*` defines.
`hb-gpu-paint.cc` (color font painting) is deliberately excluded.

To update: extract the new release, recompute the closure over the same three roots,
and replace `src/` wholesale. The closure must also follow macro-indirect includes
(`#include HB_STRING_ARRAY_LIST` resolves to `hb-ot-cff1-std-str.hh` and
`hb-ot-post-macroman.hh`); after copying, verify every quoted include in the tree
resolves on disk (the only expected miss is `config.h`, guarded by `HAVE_CONFIG_H`).
