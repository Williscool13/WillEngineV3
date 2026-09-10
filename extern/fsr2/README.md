# FidelityFX Super Resolution 2.2.1

The FSR 2 upscaler in `shaders/anti-aliasing/fsr2_*.slang` is an in-house Slang port of the compute shaders from
https://github.com/GPUOpen-Effects/FidelityFX-FSR2 at commit 1680d1ed (v2.2.1), MIT licensed (LICENSE.txt).

No FSR2 library, backend, or source is vendored. The port keeps the algorithm of the reconstruct, depth clip, lock,
accumulate and RCAS passes; the luminance pyramid is a two-dispatch reduction instead of SPD, and the reactive mask is
generated from the pre-overlay colour snapshot in the same way as FSR2's autogen reactive pass.

The port was done by AI.
