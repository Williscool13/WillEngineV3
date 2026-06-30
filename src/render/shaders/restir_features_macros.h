// Source of truth for the ReSTIR compile-time feature gates. Included directly by C++ (editor) and copied verbatim to
// shaders/include/restir_features_macros.slang by copy_shader_macros (a plain copy, NOT the -E interop preprocess, so the raw
// #defines survive for the shader-side #if). Pure preprocessor: valid in both C++ and Slang. Edit here, never the generated copy.
#ifndef WILL_ENGINE_RESTIR_FEATURES
#define WILL_ENGINE_RESTIR_FEATURES

// [base] BRDF-guided initial candidate: closest-hit ray vs the scene (1) or analytic nearest-light scan (0).
#define RESTIR_ENABLE_BRDF_RAY 1

// [base, temporal] Shadow-test the fresh candidate in base and bake occlusion into W
#define RESTIR_ENABLE_INITIAL_VISIBILITY 1

// [temporal] RTXDI permutation sampling: reflect the history tap within a 4x4 block to decorrelate temporal reuse.
#define RESTIR_ENABLE_PERMUTATION_SAMPLING 1

// [temporal] Multi-tap reprojection search for a valid history surface (vs a single reprojected tap).
#define RESTIR_ENABLE_TEMPORAL_SEARCH 1

// [temporal] Moving-shadow antilag.
#define RESTIR_ENABLE_ANTILAG 1

// [temporal] RELAX moving-shadow confidence chain.
#define RESTIR_ENABLE_CONFIDENCE 1

// [spatial] Adaptive spatial dilation.
#define RESTIR_ENABLE_SPATIAL_DILATE 1

#endif // WILL_ENGINE_RESTIR_FEATURES
