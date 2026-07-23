#define _POSIX_SOURCE 200809L
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_scene_animation.h>

static double ease_out_cubic(double t) {
	double m = t - 1.0;
	return m * m * m + 1.0;
}

static double spring_value_at(double t, double zeta, double k) {
	double w0 = sqrt(k);
	if (fabs(zeta - 1.0) < 1e-6) {
		return 1.0 - exp(-w0 * t) * (1.0 + w0 * t);
	}
	if (zeta < 1.0) {
		double wd = w0 * sqrt(1.0 - zeta * zeta);
		double A = zeta / sqrt(1.0 - zeta * zeta);
		return 1.0 - exp(-zeta * w0 * t) * (cos(wd * t) + A * sin(wd * t));
	}
	double b = w0 * sqrt(zeta * zeta - 1.0);
	double A = zeta / sqrt(zeta * zeta - 1.0);
	return 1.0 - exp(-zeta * w0 * t) * (cosh(b * t) + A * sinh(b * t));
}

static double spring_vel_at(double t, double zeta, double k) {
	double w0 = sqrt(k);
	if (fabs(zeta - 1.0) < 1e-6) {
		return t * w0 * w0 * exp(-w0 * t);
	}
	if (zeta < 1.0) {
		double wd = w0 * sqrt(1.0 - zeta * zeta);
		double A = zeta / sqrt(1.0 - zeta * zeta);
		double e = exp(-zeta * w0 * t);
		double c = cos(wd * t);
		double s = sin(wd * t);
		return e * (zeta * w0 * (c + A * s) + wd * (s - A * c));
	}
	double b = w0 * sqrt(zeta * zeta - 1.0);
	double A = zeta / sqrt(zeta * zeta - 1.0);
	double e = exp(-zeta * w0 * t);
	double ch = cosh(b * t);
	double sh = sinh(b * t);
	return e * (zeta * w0 * (ch + A * sh) - b * (sh + A * ch));
}

static bool spring_is_settled(double t, double zeta, double k, double eps) {
	if (t <= 0.0) return false;
	return fabs(spring_value_at(t, zeta, k) - 1.0) < eps
		&& fabs(spring_vel_at(t, zeta, k)) < eps;
}

static double sec_since(struct timespec *start) {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (now.tv_sec - start->tv_sec)
		+ (now.tv_nsec - start->tv_nsec) / 1e9;
}

static double lerp(double a, double b, double t) {
	return a + (b - a) * t;
}

static double prop_interp(struct timespec *start, struct wlr_scene_anim_spec *spec) {
	double t = sec_since(start);
	if (spec->easing == WLR_EASING_EASE_OUT_CUBIC) {
		double elapsed_ms = t * 1000.0;
		double p = fmin(elapsed_ms / spec->duration_ms, 1.0);
		return ease_out_cubic(p);
	}
	if (spec->easing == WLR_EASING_SPRING) {
		return fmax(spring_value_at(t, spec->damping_ratio,
			spec->stiffness), 0.0);
	}
	// LINEAR
	double elapsed_ms = t * 1000.0;
	return fmin(elapsed_ms / spec->duration_ms, 1.0);
}

static bool prop_done(struct timespec *start, struct wlr_scene_anim_spec *spec) {
	if (spec->easing == WLR_EASING_LINEAR ||
			spec->easing == WLR_EASING_EASE_OUT_CUBIC) {
		return sec_since(start) * 1000.0 >= spec->duration_ms;
	}
	return spring_is_settled(sec_since(start),
		spec->damping_ratio, spec->stiffness, spec->epsilon);
}

static void anim_apply(struct wlr_scene_animation *anim) {
	if (!anim->node) {
		return;
	}

	double v = prop_interp(&anim->start_time, &anim->spec);

	if (anim->position) {
		double x = lerp(anim->pos_from_x, anim->pos_to_x, v);
		double y = lerp(anim->pos_from_y, anim->pos_to_y, v);
		wlr_scene_node_set_position(anim->node, (int)x, (int)y);
	} else {
		struct wlr_scene_node_visual vis = {
			.x = (float)lerp(anim->from.x, anim->to.x, v),
			.y = (float)lerp(anim->from.y, anim->to.y, v),
			.width = (float)lerp(anim->from.width, anim->to.width, v),
			.height = (float)lerp(anim->from.height, anim->to.height, v),
			.scale_x = (float)lerp(anim->from.scale_x, anim->to.scale_x, v),
			.scale_y = (float)lerp(anim->from.scale_y, anim->to.scale_y, v),
			.opacity = (float)lerp(anim->from.opacity, anim->to.opacity, v),
		};
		wlr_scene_node_set_visual(anim->node, &vis);
	}
}

static int on_anim_tick(void *data) {
	struct wlr_scene_animator *a = data;
	struct wlr_scene_animation *anim, *tmp;
	bool running = false;

	wl_list_for_each_safe(anim, tmp, &a->animations, link) {
		if (prop_done(&anim->start_time, &anim->spec)) {
			// Snap to final state
			if (anim->position) {
				wlr_scene_node_set_position(anim->node,
					(int)anim->pos_to_x, (int)anim->pos_to_y);
			} else {
				wlr_scene_node_set_visual(anim->node, &anim->to);
			}
			if (anim->done) {
				anim->done(anim->done_data);
			}
			wl_list_remove(&anim->link);
			wl_list_remove(&anim->node_destroy.link);
			free(anim);
		} else {
			anim_apply(anim);
			running = true;
		}
	}

	if (running) {
		wl_event_source_timer_update(a->timer, 16);
	} else {
		wl_event_source_timer_update(a->timer, 0);
	}
	return 0;
}

static void handle_node_destroy(struct wl_listener *listener, void *data) {
	struct wlr_scene_animation *anim =
		wl_container_of(listener, anim, node_destroy);
	anim->node = NULL; // prevent use-after-free in tick
	wl_list_remove(&anim->link);
	wl_list_remove(&anim->node_destroy.link);
	free(anim);
}

struct wlr_scene_animator *wlr_scene_animator_create(
		struct wl_event_loop *loop) {
	struct wlr_scene_animator *a = calloc(1, sizeof(*a));
	if (!a) {
		return NULL;
	}
	wl_list_init(&a->animations);
	wl_signal_init(&a->request_frame);
	a->timer = wl_event_loop_add_timer(loop, on_anim_tick, a);
	if (!a->timer) {
		free(a);
		return NULL;
	}
	return a;
}

void wlr_scene_animator_destroy(struct wlr_scene_animator *a) {
	struct wlr_scene_animation *anim, *tmp;
	wl_list_for_each_safe(anim, tmp, &a->animations, link) {
		wl_list_remove(&anim->link);
		wl_list_remove(&anim->node_destroy.link);
		free(anim);
	}
	if (a->timer) {
		wl_event_source_remove(a->timer);
	}
	free(a);
}

struct wlr_scene_animation *wlr_scene_animate(
		struct wlr_scene_animator *animator,
		struct wlr_scene_node *node,
		const struct wlr_scene_node_visual *to,
		const struct wlr_scene_anim_spec *spec,
		void (*done)(void *data),
		void *done_data) {
	// Retarget existing visual animation on this node, if any
	struct wlr_scene_animation *anim;
	wl_list_for_each(anim, &animator->animations, link) {
		if (anim->node == node && !anim->position) {
			// Snapshot current visual as new "from"
			if (node->visual) {
				anim->from = *node->visual;
			} else {
				anim->from = (struct wlr_scene_node_visual){
					.scale_x = 1.0f, .scale_y = 1.0f, .opacity = 1.0f,
				};
			}
			anim->to = *to;
			anim->spec = *spec;
			anim->done = done;
			anim->done_data = done_data;
			clock_gettime(CLOCK_MONOTONIC, &anim->start_time);
			wl_event_source_timer_update(animator->timer, 1);
			return anim;
		}
	}

	// Snapshot current visual state (or defaults) as "from"
	struct wlr_scene_node_visual from = { .scale_x = 1.0f, .scale_y = 1.0f, .opacity = 1.0f };
	if (node->visual) {
		from = *node->visual;
	} else {
		from.x = 0;
		from.y = 0;
		from.width = 0;
		from.height = 0;
		from.scale_x = 1.0f;
		from.scale_y = 1.0f;
		from.opacity = 1.0f;
	}

	anim = calloc(1, sizeof(*anim));
	if (!anim) {
		return NULL;
	}
	anim->node = node;
	anim->spec = *spec;
	anim->from = from;
	anim->to = *to;
	clock_gettime(CLOCK_MONOTONIC, &anim->start_time);
	anim->done = done;
	anim->done_data = done_data;

	anim->node_destroy.notify = handle_node_destroy;
	wl_signal_add(&node->events.destroy, &anim->node_destroy);

	wl_list_insert(&animator->animations, &anim->link);

	// Start the timer immediately if not already running
	wl_event_source_timer_update(animator->timer, 1);

	wl_signal_emit_mutable(&animator->request_frame, NULL);

	return anim;
}

struct wlr_scene_animation *wlr_scene_animate_position(
		struct wlr_scene_animator *animator,
		struct wlr_scene_node *node,
		double from_x, double from_y,
		double to_x, double to_y,
		const struct wlr_scene_anim_spec *spec,
		void (*done)(void *data),
		void *done_data) {
	// Retarget existing position animation on this node, if any
	struct wlr_scene_animation *anim;
	wl_list_for_each(anim, &animator->animations, link) {
		if (anim->node == node && anim->position) {
			anim->pos_from_x = from_x;
			anim->pos_from_y = from_y;
			anim->pos_to_x = to_x;
			anim->pos_to_y = to_y;
			anim->spec = *spec;
			anim->done = done;
			anim->done_data = done_data;
			clock_gettime(CLOCK_MONOTONIC, &anim->start_time);
			wl_event_source_timer_update(animator->timer, 1);
			return anim;
		}
	}

	anim = calloc(1, sizeof(*anim));
	if (!anim) {
		return NULL;
	}
	anim->node = node;
	anim->position = true;
	anim->spec = *spec;
	anim->pos_from_x = from_x;
	anim->pos_from_y = from_y;
	anim->pos_to_x = to_x;
	anim->pos_to_y = to_y;
	clock_gettime(CLOCK_MONOTONIC, &anim->start_time);
	anim->done = done;
	anim->done_data = done_data;

	anim->node_destroy.notify = handle_node_destroy;
	wl_signal_add(&node->events.destroy, &anim->node_destroy);

	wl_list_insert(&animator->animations, &anim->link);

	wl_event_source_timer_update(animator->timer, 1);

	wl_signal_emit_mutable(&animator->request_frame, NULL);

	return anim;
}

void wlr_scene_animation_cancel(struct wlr_scene_animation *anim) {
	if (!anim) {
		return;
	}
	if (wl_list_empty(&anim->link)) {
		return;
	}
	// Snap to final
	if (anim->node) {
		wlr_scene_node_set_visual(anim->node, &anim->to);
	}
	wl_list_remove(&anim->link);
	wl_list_remove(&anim->node_destroy.link);
	free(anim);
}
