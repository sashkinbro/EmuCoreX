// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

//#version 420 // Keep it for text editor detection

layout(std140, binding = 1) uniform cb20
{
	vec2  VertexScale;
	vec2  VertexOffset;

	vec2  TextureScale;
	vec2  TextureOffset;

	vec2  PointSize;

	uint  MaxDepth;
	float LineAA1Width;
};

#ifdef VERTEX_SHADER

out SHADER
{
	vec4 t_float;
	vec4 t_int;
	#if VS_IIP != 0
		vec4 c;
	#else
		flat vec4 c;
	#endif
} VSout;

const float exp_min32 = exp2(-32.0f);

#if VS_EXPAND == 0

layout(location = 0) in vec2  i_st;
layout(location = 2) in vec4  i_c;
layout(location = 3) in float i_q;
layout(location = 4) in uvec2 i_p;
layout(location = 5) in uint  i_z;
layout(location = 6) in uvec2 i_uv;
layout(location = 7) in vec4  i_f;

void texture_coord()
{
	vec2 uv = vec2(i_uv) - TextureOffset;
	vec2 st = i_st - TextureOffset;

	// Float coordinate
	VSout.t_float.xy = st;
	VSout.t_float.w  = i_q;

	// Integer coordinate => normalized
	VSout.t_int.xy = uv * TextureScale;
#if VS_FST
	// Integer coordinate => integral
	VSout.t_int.zw = uv;
#else
	// Some games uses float coordinate for post-processing effect
	VSout.t_int.zw = st / TextureScale;
#endif
}

void vs_main()
{
	// Clamp to max depth, gs doesn't wrap
	highp uint z = min(i_z, MaxDepth);

	// pos -= 0.05 (1/320 pixel) helps avoiding rounding problems (integral part of pos is usually 5 digits, 0.05 is about as low as we can go)
	// example: ceil(afterseveralvertextransformations(y = 133)) => 134 => line 133 stays empty
	// input granularity is 1/16 pixel, anything smaller than that won't step drawing up/left by one pixel
	// example: 133.0625 (133 + 1/16) should start from line 134, ceil(133.0625 - 0.05) still above 133
	gl_Position.xy = vec2(i_p) - vec2(0.05f, 0.05f);
	gl_Position.xy = gl_Position.xy * VertexScale - VertexOffset;
	gl_Position.w = 1.0f;

#if HAS_CLIP_CONTROL
	gl_Position.z = float(z) * exp_min32;
#else
	// GLES doesn't support ARB_clip_control, so remap it to -1..1. This isn't lossless, but
	// gets rid of really bad Z-fighting.
	gl_Position.z = min(float(z) * exp2(-23.0f), 2.0f) - 1.0f;
#endif

	texture_coord();

	VSout.c = i_c;
	VSout.t_float.z = i_f.x; // pack for with texture

	#if VS_POINT_SIZE
		gl_PointSize = PointSize.x;
	#endif
}

#else // VS_EXPAND

struct RawVertex
{
	vec2 ST;
	uint RGBA;
	float Q;
	uint XY;
	uint Z;
	uint UV;
	uint FOG;
};

layout(std140, binding = 2) readonly buffer VertexBuffer {
	RawVertex vertex_buffer[];
};

struct ProcessedVertex
{
	vec4 p;
	vec4 t_float;
	vec4 t_int;
	vec4 c;
};

ProcessedVertex load_vertex(uint index)
{
#if defined(GL_ARB_shader_draw_parameters) && GL_ARB_shader_draw_parameters
	RawVertex rvtx = vertex_buffer[index + gl_BaseVertexARB];
#else
	RawVertex rvtx = vertex_buffer[index];
#endif

	vec2 i_st = rvtx.ST;
	vec4 i_c = vec4(uvec4(bitfieldExtract(rvtx.RGBA, 0, 8), bitfieldExtract(rvtx.RGBA, 8, 8),
	                      bitfieldExtract(rvtx.RGBA, 16, 8), bitfieldExtract(rvtx.RGBA, 24, 8)));
	float i_q = rvtx.Q;
	uvec2 i_p = uvec2(bitfieldExtract(rvtx.XY, 0, 16), bitfieldExtract(rvtx.XY, 16, 16));
	uint i_z = rvtx.Z;
	uvec2 i_uv = uvec2(bitfieldExtract(rvtx.UV, 0, 16), bitfieldExtract(rvtx.UV, 16, 16));
	vec4 i_f = unpackUnorm4x8(rvtx.FOG);

	ProcessedVertex vtx;

	uint z = min(i_z, MaxDepth);
	vtx.p.xy = vec2(i_p) - vec2(0.05f, 0.05f);
	vtx.p.xy = vtx.p.xy * VertexScale - VertexOffset;
#if HAS_CLIP_CONTROL
	vtx.p.z = float(z) * exp_min32;
#else
	vtx.p.z = min(float(z) * exp2(-23.0f), 2.0f) - 1.0f;
#endif
	vtx.p.w = 1.0f;

	vec2 uv = vec2(i_uv) - TextureOffset;
	vec2 st = i_st - TextureOffset;

	vtx.t_float.xy = st;
	vtx.t_float.w  = i_q;

	vtx.t_int.xy = uv * TextureScale;
#if VS_FST
	vtx.t_int.zw = uv;
#else
	vtx.t_int.zw = st / TextureScale;
#endif

	vtx.c = i_c;
	vtx.t_float.z = i_f.x;

	return vtx;
}

// Convert XY from NDC to GS pixel coordinates (i.e. 1.0 = 1 GS pixel).
vec2 get_xy_unscaled(vec2 xy)
{
	return round(xy / VertexScale) / 16.0f;
}

// Get the XY deltas in GS pixel coordinates, using first vertex as the origin.
mat2 get_xy_deltas_unscaled(ProcessedVertex v0, ProcessedVertex v1, ProcessedVertex v2)
{
	vec2 xy0 = get_xy_unscaled(v0.p.xy);
	vec2 xy1 = get_xy_unscaled(v1.p.xy);
	vec2 xy2 = get_xy_unscaled(v2.p.xy);
	return mat2(xy1 - xy0, xy2 - xy0);
}

// Get the AA1 outward expand direction to the edge formed by the first two vertices.
// This is up or down for shallow (X dominant) edges, and right or left for steep (Y dominant) edges.
// Similar expansion to line AA1 except instead of expanding on both sides of the line,
// expand on on the side towards the outside of the triangle.
vec2 get_aa1_triangle_expand_dir(ProcessedVertex v0, ProcessedVertex v1, ProcessedVertex v2)
{
	mat2 xy_deltas = get_xy_deltas_unscaled(v0, v1, v2);
	vec2 line_delta = xy_deltas[0];
	vec2 line_opposite = xy_deltas[1];

	vec2 line_normal = vec2(line_delta.y, -line_delta.x);
	vec2 line_expand = abs(line_delta.x) >= abs(line_delta.y) ? vec2(0.0f, 1.0f) : vec2(1.0f, 0.0f);

	if ((dot(line_expand, line_normal) >= 0.0f) == (dot(line_opposite, line_normal) >= 0.0f))
	{
		// Expand direction point towards the interior so flip it.
		line_expand = -line_expand;
	}

	return line_expand;
}

mat2 get_inverse(mat2 mat, float det)
{
	return mat2(mat[1][1], -mat[0][1], -mat[1][0], mat[0][0]) * (1 / det);
}

// Extrapolate triangle attributes from the first vertex along the given direction.
// dp_mat is derived from the input vertices, it is passed in to avoid recomputing.
void extrapolate_aa1_triangle_edge(inout ProcessedVertex v0, ProcessedVertex v1, ProcessedVertex v2, mat2 dp_mat, vec2 dp)
{
	// Get texture deltas
	#if VS_TME
		#if VS_FST
			mat2 dt = mat2(v1.t_int.zw - v0.t_int.zw, v2.t_int.zw - v0.t_int.zw);
		#else
			mat2 dt = mat2(v1.t_float.xy - v0.t_float.xy, v2.t_float.xy - v0.t_float.xy);
		#endif
	#endif

	// Get color delta if interpolating
	#if VS_IIP
		mat2x4 dc = mat2x4(v1.c - v0.c, v2.c - v0.c);
	#endif

	vec2 dz = vec2(v1.p.z - v0.p.z, v2.p.z - v0.p.z); // Z deltas

	vec2 df = vec2(v1.t_float.z - v0.t_float.z, v2.t_float.z - v0.t_float.z); // Fog deltas

	vec2 dq = vec2(v1.t_float.w - v0.t_float.w, v2.t_float.w - v0.t_float.w); // Q deltas

	// To prevent unstable extrapolation, do not extrapolate if the
	// minimum perpendicular length of the triangle is < 2 pixels.
	float dp_det = determinant(dp_mat); // Twice signed triangle area.
	float len0 = length(dp_mat[0]);
	float len1 = length(dp_mat[1]);
	float len2 = length(dp_mat[1] - dp_mat[0]);
	float min_perp_length = abs(dp_det) / max(max(len0, len1), len2);

	// Get the position -> barycentric weight matrix
	mat2 inv_dp_mat = get_inverse(dp_mat, dp_det);

	vec2 weights = min_perp_length < 2 ? vec2(0) : inv_dp_mat * dp;

	v0.p.xy += dp * PointSize; // Extrapolate position

	// Extrapolate texture coords
	#if VS_TME
		#if VS_FST
			v0.t_int.zw += dt * weights;
			v0.t_int.xy = v0.t_int.zw * TextureScale;
		#else
			v0.t_float.xy += dt * weights;
			v0.t_int.zw = v0.t_float.xy / TextureScale;
			v0.t_float.w += dot(dq, weights);
		#endif
	#endif

	// Extrapolate and clamp color
	#if VS_IIP
		v0.c += dc * weights;
		v0.c = clamp(v0.c, vec4(0), vec4(255));
	#endif

	v0.p.z += dot(dz, weights); // Extrapolate depth

	v0.t_float.z += dot(df, weights); // Extrapolate fog
}

void main()
{
	ProcessedVertex vtx;

#if defined(GL_ARB_shader_draw_parameters) && GL_ARB_shader_draw_parameters
	uint vid = uint(gl_VertexID - gl_BaseVertexARB);
#else
	uint vid = uint(gl_VertexID);
#endif

#if VS_EXPAND == 1 // Point

	vtx = load_vertex(vid >> 2);

	vtx.p.x += ((vid & 1u) != 0u) ? PointSize.x : 0.0f;
	vtx.p.y += ((vid & 2u) != 0u) ? PointSize.y : 0.0f;

#elif VS_EXPAND == 2 // Line

	uint vid_base = vid >> 2;
	bool is_bottom = (vid & 2u) != 0u;
	bool is_right = (vid & 1u) != 0u;
	uint vid_other = is_bottom ? vid_base - 1 : vid_base + 1;
	vtx = load_vertex(vid_base);
	ProcessedVertex other = load_vertex(vid_other);

	vec2 line_vector = normalize(vtx.p.xy - other.p.xy);
	vec2 line_normal = vec2(line_vector.y, -line_vector.x);
	vec2 line_width = (line_normal * PointSize) / 2;
	// line_normal is inverted for bottom point
	vec2 offset = ((uint(is_bottom) ^ uint(is_right)) != 0u) ? line_width : -line_width;
	vtx.p.xy += offset;

	// Lines will be run as (0 1 2) (1 2 3)
	// This means that both triangles will have a point based off the top line point as their first point
	// So we don't have to do anything for !IIP

#elif VS_EXPAND == 3 // Sprite

	// Sprite points are always in pairs
	uint vid_base = vid >> 1;
	uint vid_lt = vid_base & ~1u;
	uint vid_rb = vid_base | 1u;

	ProcessedVertex lt = load_vertex(vid_lt);
	ProcessedVertex rb = load_vertex(vid_rb);
	vtx = rb;

	bool is_right = ((vid & 1u) != 0u);
	vtx.p.x = is_right ? lt.p.x : vtx.p.x;
	vtx.t_float.x = is_right ? lt.t_float.x : vtx.t_float.x;
	vtx.t_int.xz = is_right ? lt.t_int.xz : vtx.t_int.xz;

	bool is_bottom = ((vid & 2u) != 0u);
	vtx.p.y = is_bottom ? lt.p.y : vtx.p.y;
	vtx.t_float.y = is_bottom ? lt.t_float.y : vtx.t_float.y;
	vtx.t_int.yw = is_bottom ? lt.t_int.yw : vtx.t_int.yw;

#endif

	gl_Position = vtx.p;
	VSout.t_float = vtx.t_float;
	VSout.t_int = vtx.t_int;
	VSout.c = vtx.c;
}

#endif // VS_EXPAND

#endif // VERTEX_SHADER
