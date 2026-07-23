/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_SCENE_ANIMATION_H
#define WLR_TYPES_WLR_SCENE_ANIMATION_H

#include <wlr/types/wlr_scene.h>

struct wlr_scene_animator;
struct wlr_scene_animation;

enum wlr_easing {
	WLR_EASING_LINEAR,
	WLR_EASING_EASE_OUT_CUBIC,
	WLR_EASING_SPRING,
};

struct wlr_scene_anim_spec {
	enum wlr_easing easing;
	double duration_ms;
	double damping_ratio;   // spring only
	double stiffness;       // spring only
	double epsilon;         // spring settling threshold
};

/** One animator per scene, drives all animations. */
struct wlr_scene_animator {
	struct wl_list animations;      // wlr_scene_animation.link
	struct wl_event_source *timer;
	struct wl_signal request_frame; // emitted when an animation is active
};

/** A single animated property set on one node. */
struct wlr_scene_animation {
	struct wl_list link;
	struct wlr_scene_node *node;
	bool position; // true = animate node->x/y, false = animate node->visual
	struct wlr_scene_anim_spec spec;
	union {
		struct {
			struct wlr_scene_node_visual from, to;
		};
		struct {
			double pos_from_x, pos_from_y, pos_to_x, pos_to_y;
		};
	};
	struct timespec start_time;
	struct wl_listener node_destroy;
	void (*done)(void *data);
	void *done_data;
};

struct wlr_scene_animator *wlr_scene_animator_create(
	struct wl_event_loop *loop);

void wlr_scene_animator_destroy(
	struct wlr_scene_animator *animator);

/**
 * Start an animation on node's visual properties.
 *
 * Snapshots current node->visual (or zero-clear defaults if NULL) as "from".
 * Animates to the given "to" visual.
 */
struct wlr_scene_animation *wlr_scene_animate(
	struct wlr_scene_animator *animator,
	struct wlr_scene_node *node,
	const struct wlr_scene_node_visual *to,
	const struct wlr_scene_anim_spec *spec,
	void (*done)(void *data),
	void *done_data);

/**
 * Animate node->x/y from (from_x, from_y) to (to_x, to_y).
 *
 * Snapshots node->x/y (not visual). Interpolation sets node->x/y
 * via wlr_scene_node_set_position on each tick.
 */
struct wlr_scene_animation *wlr_scene_animate_position(
	struct wlr_scene_animator *animator,
	struct wlr_scene_node *node,
	double from_x, double from_y,
	double to_x, double to_y,
	const struct wlr_scene_anim_spec *spec,
	void (*done)(void *data),
	void *done_data);

/**
 * Cancel an animation immediately and reset visual to "to" state.
 * If node is being destroyed, the animation auto-cancels.
 */
void wlr_scene_animation_cancel(struct wlr_scene_animation *anim);

#endif
