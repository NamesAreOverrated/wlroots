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

float corner_alpha(vec2 pos, vec2 size, vec4 r) {
	vec2 p = pos - size * 0.5;
	r = vec4(r[1], r[2], r[0], r[3]);
	r.xy = (p.x > 0.0) ? r.xy : r.zw;
	r.x  = (p.y > 0.0) ? r.x  : r.y;
	vec2 half_size = size * 0.5;
	vec2 q = abs(p) - half_size + r.x;
	float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r.x;
	return 1.0 - smoothstep(0.0, fwidth(d), d);
}

void main() {
	vec2 pos = gl_FragCoord.xy - u_box.xy;
	vec2 size = u_box.zw;
	vec4 bt = u_border_thickness; // top, right, bottom, left

	float has_corners = u_corner_radius[0] + u_corner_radius[1]
		+ u_corner_radius[2] + u_corner_radius[3];
	float has_border = bt[0] + bt[1] + bt[2] + bt[3];

	if (has_border > 0.0) {
		float outer = corner_alpha(pos, size, u_corner_radius);

		vec2 inner_pos = pos - vec2(bt[3], bt[0]); // offset by (left, top)
		vec2 inner_size = size - vec2(bt[3] + bt[1], bt[0] + bt[2]);
		vec4 inner_r = max(u_corner_radius -
			vec4(max(bt[0], bt[3]), max(bt[0], bt[1]),
			     max(bt[2], bt[1]), max(bt[2], bt[3])), 0.0);
		float inner = 1.0 - corner_alpha(inner_pos, inner_size, inner_r);

		gl_FragColor = color * outer * inner;
	} else if (has_corners > 0.0) {
		gl_FragColor = color * corner_alpha(pos, size, u_corner_radius);
	} else {
		gl_FragColor = color;
	}
}
