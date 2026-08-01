#extension GL_OES_EGL_image_external : require
#extension GL_OES_standard_derivatives : enable

#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif

varying vec2 v_texcoord;
uniform samplerExternalOES texture0;
uniform float alpha;
uniform vec4 u_box;
uniform vec4 u_corner_radius;

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
	vec4 tex_color = texture2D(texture0, v_texcoord) * alpha;
	if (u_corner_radius[0] != 0.0 || u_corner_radius[1] != 0.0 ||
	    u_corner_radius[2] != 0.0 || u_corner_radius[3] != 0.0) {
		vec2 pos = gl_FragCoord.xy - u_box.xy;
		gl_FragColor = tex_color * corner_alpha(pos, u_box.zw, u_corner_radius);
	} else {
		gl_FragColor = tex_color;
	}
}
