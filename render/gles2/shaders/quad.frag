#extension GL_OES_standard_derivatives : enable

#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif

varying vec2 v_texcoord;
uniform vec4 color;
uniform vec4 u_box;
uniform vec4 u_corner_radius;
uniform vec4 u_border_thickness;
uniform vec4 u_shadow; // x=blur_sigma, y=opacity, z=unused, w=extension
uniform vec4 u_shadow_color;

float corner_sdf(vec2 pos, vec2 size, vec4 r) {
	vec2 p = pos - size * 0.5;
	r = vec4(r[1], r[2], r[0], r[3]);
	r.xy = (p.x > 0.0) ? r.xy : r.zw;
	r.x  = (p.y > 0.0) ? r.x  : r.y;
	vec2 half_size = size * 0.5;
	vec2 q = abs(p) - half_size + r.x;
	return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r.x;
}

float corner_alpha(vec2 pos, vec2 size, vec4 r) {
	return 1.0 - smoothstep(0.0, fwidth(corner_sdf(pos, size, r)), corner_sdf(pos, size, r));
}

void main() {
	vec2 pos = gl_FragCoord.xy - u_box.xy;
	vec2 size = u_box.zw;
	vec4 bt = u_border_thickness; // top, right, bottom, left
	float ext = u_shadow.w; // container rect offset from VFX origin (0 = no expansion)

	// Container rect: inset from expanded VFX bounds by ext
	vec2 cpos = pos - vec2(ext);
	vec2 csize = size - vec2(2.0 * ext);

	float has_corners = u_corner_radius[0] + u_corner_radius[1]
		+ u_corner_radius[2] + u_corner_radius[3];
	float has_border = bt[0] + bt[1] + bt[2] + bt[3];
	float has_shadow = (u_shadow.x > 0.0 && u_shadow.y > 0.0
		&& u_shadow_color.a > 0.0) ? 1.0 : 0.0;

	vec4 inner_r;
	inner_r[0] = max(0.0, u_corner_radius[0] - max(bt[0], bt[3]));
	inner_r[1] = max(0.0, u_corner_radius[1] - max(bt[0], bt[1]));
	inner_r[2] = max(0.0, u_corner_radius[2] - max(bt[1], bt[2]));
	inner_r[3] = max(0.0, u_corner_radius[3] - max(bt[2], bt[3]));

	vec4 result = vec4(0.0);

	// 1. Shadow (behind everything)
	if (has_shadow > 0.0) {
		float d = corner_sdf(cpos, csize, u_corner_radius);
		float dist = max(0.0, d);

		// Clip shadow from the content area (inside inner rect)
		vec2 inner_pos = cpos - vec2(bt[3], bt[0]);
		vec2 inner_size = csize - vec2(bt[3] + bt[1], bt[0] + bt[2]);
		float inner_d = corner_sdf(inner_pos, inner_size, inner_r);
		float content_mask = smoothstep(0.0, fwidth(inner_d), inner_d);

		float a = content_mask * exp(-(dist * dist) / (2.0 * u_shadow.x * u_shadow.x));
		a *= u_shadow.y * u_shadow_color.a;
		result = u_shadow_color * a;
	}

	// 2. Border rim (independent — on top of shadow)
	if (has_border > 0.0) {
		float outer = corner_alpha(cpos, csize, u_corner_radius);

		vec2 inner_pos = cpos - vec2(bt[3], bt[0]);
		vec2 inner_size = csize - vec2(bt[3] + bt[1], bt[0] + bt[2]);
		float inner_d = corner_sdf(inner_pos, inner_size, inner_r);
		float inner = smoothstep(-fwidth(inner_d), 0.0, inner_d);

		float ba = outer * inner;
		result = result * (1.0 - ba) + color * ba;

		gl_FragColor = result;
		return;
	}

	// 3. Shadow-only (no border) — shadow already in result
	if (has_shadow > 0.0) {
		gl_FragColor = result;
		return;
	}

	// 4. Fallback: plain rect (no border, no shadow) — just color * corner clip
	float ca = corner_alpha(cpos, csize, u_corner_radius);
	gl_FragColor = color * ca;
}
