// Shared unit-quad vertex offsets for GLSL and Metal shaders.
// Two triangles cover the unit rectangle:
//   triangle 1: (0,0) (1,0) (0,1)
//   triangle 2: (1,0) (1,1) (0,1)
// Edit the values here only — every grid/gui quad pipeline pulls from this
// header so winding order or UV-flip changes stay in one place.
//
// Each #define expands to two comma-separated floats so the shader can wrap
// them in its native vec2 / float2 constructor.

#define QUAD_OFFSET_0 0.0, 0.0
#define QUAD_OFFSET_1 1.0, 0.0
#define QUAD_OFFSET_2 0.0, 1.0
#define QUAD_OFFSET_3 1.0, 0.0
#define QUAD_OFFSET_4 1.0, 1.0
#define QUAD_OFFSET_5 0.0, 1.0
