// StreamUP Test Card — an OBS video filter that replaces whatever it is applied
// to with a labelled test card. A native fallback for the obs-shaderfilter
// version, with real typed text (any font), a settings panel that adapts to the
// chosen background, and optional auto-labelling from the parent source's name.
//
// Rendering split: the GPU effect (streamup-test-card.effect) draws the
// background pattern; QPainter renders the label + plate into a small bitmap on
// the CPU (test-card-overlay.cpp), which the effect composites and positions.

#include "test-card-filter.hpp"
#include "test-card-overlay.hpp"
#include "version.h"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <graphics/graphics.h>
#include <graphics/image-file.h>
#include <graphics/vec2.h>
#include <graphics/vec4.h>
#include <util/platform.h>

#include <streamup/ui/feedback.hpp>

#include <QColor>
#include <QImage>
#include <QString>
#include <QWidget>

#include <new>
#include <mutex>
#include <vector>
#include <cstring>

// ---------------------------------------------------------------------------
// Settings keys
// ---------------------------------------------------------------------------
#define S_BACKGROUND      "background_style"
#define S_SOLID_COLOR     "solid_color"
#define S_BARS_LEVEL      "bars_level"
#define S_GRID_COLOR      "grid_color"
#define S_GRID_SPACING    "grid_spacing"
#define S_GRID_LINE_WIDTH "grid_line_width"
#define S_CROSSHAIR_WIDTH "crosshair_width"
#define S_IMAGE_FILE      "background_image"
#define S_IMAGE_FIT       "image_fit"
#define S_IMAGE_ASPECT    "image_aspect"
#define S_TEXT            "text"
#define S_USE_SRC_NAME    "use_source_name"
#define S_FONT            "font"
#define S_TEXT_COLOR      "text_color"
#define S_TEXT_OFF_X      "text_offset_x"
#define S_TEXT_OFF_Y      "text_offset_y"
#define S_PLATE_ENABLED   "plate_enabled"
#define S_PLATE_COLOR     "plate_color"
#define S_PLATE_PADDING   "plate_padding"
#define S_PLATE_RADIUS    "plate_radius"
#define S_BORDER_ENABLED  "border_enabled"
#define S_BORDER_COLOR    "border_color"
#define S_BORDER_WIDTH    "border_width"

namespace su = StreamUP::UIStyles;

// ---------------------------------------------------------------------------
// Filter state
// ---------------------------------------------------------------------------
struct test_card_filter {
	obs_source_t *context = nullptr;
	gs_effect_t *effect = nullptr;

	// effect params
	gs_eparam_t *p_overlay = nullptr, *p_bg_image = nullptr;
	gs_eparam_t *p_dimension = nullptr, *p_overlay_size = nullptr, *p_text_offset = nullptr;
	gs_eparam_t *p_background_style = nullptr, *p_solid_color = nullptr, *p_bars_level = nullptr;
	gs_eparam_t *p_grid_color = nullptr, *p_grid_spacing = nullptr;
	gs_eparam_t *p_grid_line_width = nullptr, *p_crosshair_width = nullptr;
	gs_eparam_t *p_image_fit = nullptr, *p_image_aspect = nullptr;
	gs_eparam_t *p_border_color = nullptr, *p_border_width = nullptr;

	// cached numeric settings
	int background_style = 0;
	struct vec4 solid_color = {};
	struct vec4 grid_color = {};
	struct vec4 border_color = {};
	float bars_level = 0.75f;
	float grid_spacing = 80.0f;
	float grid_line_width = 1.0f;
	float crosshair_width = 3.0f;
	int image_fit = 0;
	float image_aspect = 16.0f / 9.0f;
	float border_width = 0.0f;
	float text_off_x = 0.0f;
	float text_off_y = 0.0f;

	// user background image
	gs_image_file_t bg_img = {};
	bool bg_img_loaded = false;
	char *bg_img_path = nullptr;

	// 1x1 transparent fallback so we never bind a null texture
	gs_texture_t *blank_tex = nullptr;

	// label bitmap (premultiplied BGRA), produced on the main thread in update()
	std::mutex overlay_mutex;
	std::vector<uint8_t> overlay_px;
	int overlay_w = 0;
	int overlay_h = 0;
	bool overlay_dirty = false;
	gs_texture_t *overlay_tex = nullptr;
	int tex_w = 0;
	int tex_h = 0;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
// OBS stores colours as 0xAABBGGRR (red in the low byte).
static inline void color_to_vec4(uint32_t c, struct vec4 *v)
{
	v->x = (float)(c & 0xFF) / 255.0f;
	v->y = (float)((c >> 8) & 0xFF) / 255.0f;
	v->z = (float)((c >> 16) & 0xFF) / 255.0f;
	v->w = (float)((c >> 24) & 0xFF) / 255.0f;
}

static inline QColor color_to_qcolor(uint32_t c)
{
	return QColor((int)(c & 0xFF), (int)((c >> 8) & 0xFF), (int)((c >> 16) & 0xFF),
		      (int)((c >> 24) & 0xFF));
}

static inline uint32_t rgba(int r, int g, int b, int a)
{
	return (uint32_t)((a << 24) | (b << 16) | (g << 8) | r);
}

static float aspect_from_preset(int preset)
{
	switch (preset) {
	case 1: return 1.0f;         // 1:1
	case 2: return 9.0f / 16.0f; // 9:16
	case 3: return 4.0f / 3.0f;  // 4:3
	case 4: return 3.0f / 4.0f;  // 3:4
	case 5: return 21.0f / 9.0f; // 21:9
	default: return 16.0f / 9.0f;
	}
}

// ---------------------------------------------------------------------------
// Source callbacks
// ---------------------------------------------------------------------------
static const char *tc_get_name(void *)
{
	return "StreamUP Test Card";
}

static void tc_reload_image(struct test_card_filter *f, const char *path)
{
	const char *cur = f->bg_img_path ? f->bg_img_path : "";
	if (strcmp(cur, path ? path : "") == 0)
		return; // unchanged

	obs_enter_graphics();
	if (f->bg_img_loaded) {
		gs_image_file_free(&f->bg_img);
		f->bg_img_loaded = false;
	}
	if (path && *path) {
		gs_image_file_init(&f->bg_img, path);
		gs_image_file_init_texture(&f->bg_img);
		f->bg_img_loaded = (f->bg_img.texture != nullptr);
	}
	obs_leave_graphics();

	bfree(f->bg_img_path);
	f->bg_img_path = bstrdup(path ? path : "");
}

static void tc_update(void *data, obs_data_t *s)
{
	auto *f = (struct test_card_filter *)data;

	f->background_style = (int)obs_data_get_int(s, S_BACKGROUND);
	color_to_vec4((uint32_t)obs_data_get_int(s, S_SOLID_COLOR), &f->solid_color);
	f->bars_level = (float)obs_data_get_double(s, S_BARS_LEVEL);
	color_to_vec4((uint32_t)obs_data_get_int(s, S_GRID_COLOR), &f->grid_color);
	f->grid_spacing = (float)obs_data_get_int(s, S_GRID_SPACING);
	f->grid_line_width = (float)obs_data_get_double(s, S_GRID_LINE_WIDTH);
	f->crosshair_width = (float)obs_data_get_double(s, S_CROSSHAIR_WIDTH);
	f->image_fit = (int)obs_data_get_int(s, S_IMAGE_FIT);
	f->image_aspect = aspect_from_preset((int)obs_data_get_int(s, S_IMAGE_ASPECT));
	f->text_off_x = (float)obs_data_get_int(s, S_TEXT_OFF_X);
	f->text_off_y = (float)obs_data_get_int(s, S_TEXT_OFF_Y);

	const bool border_en = obs_data_get_bool(s, S_BORDER_ENABLED);
	color_to_vec4((uint32_t)obs_data_get_int(s, S_BORDER_COLOR), &f->border_color);
	f->border_width = border_en ? (float)obs_data_get_int(s, S_BORDER_WIDTH) : 0.0f;

	tc_reload_image(f, obs_data_get_string(s, S_IMAGE_FILE));

	// Resolve the label text: parent source name, or the typed field.
	QString label;
	if (obs_data_get_bool(s, S_USE_SRC_NAME)) {
		obs_source_t *parent = obs_filter_get_parent(f->context);
		const char *pn = parent ? obs_source_get_name(parent) : nullptr;
		label = QString::fromUtf8(pn ? pn : "");
	} else {
		const char *t = obs_data_get_string(s, S_TEXT);
		label = QString::fromUtf8(t ? t : "");
	}

	StreamUP::TestCard::LabelParams lp;
	lp.text = label;
	if (obs_data_t *font_obj = obs_data_get_obj(s, S_FONT)) {
		lp.font_face = QString::fromUtf8(obs_data_get_string(font_obj, "face"));
		lp.font_size = (int)obs_data_get_int(font_obj, "size");
		lp.font_flags = (uint32_t)obs_data_get_int(font_obj, "flags");
		obs_data_release(font_obj);
	}
	lp.text_color = color_to_qcolor((uint32_t)obs_data_get_int(s, S_TEXT_COLOR));
	lp.plate_enabled = obs_data_get_bool(s, S_PLATE_ENABLED);
	lp.plate_color = color_to_qcolor((uint32_t)obs_data_get_int(s, S_PLATE_COLOR));
	lp.plate_padding = (int)obs_data_get_int(s, S_PLATE_PADDING);
	lp.plate_radius = (int)obs_data_get_int(s, S_PLATE_RADIUS);

	QImage img = StreamUP::TestCard::render_label(lp);

	std::lock_guard<std::mutex> lk(f->overlay_mutex);
	if (img.isNull()) {
		f->overlay_w = f->overlay_h = 0;
		f->overlay_px.clear();
	} else {
		f->overlay_w = img.width();
		f->overlay_h = img.height();
		const int stride = img.width() * 4;
		f->overlay_px.resize((size_t)stride * img.height());
		for (int y = 0; y < img.height(); y++)
			memcpy(f->overlay_px.data() + (size_t)y * stride, img.constScanLine(y), stride);
	}
	f->overlay_dirty = true;
}

static void *tc_create(obs_data_t *settings, obs_source_t *source)
{
	auto *f = (struct test_card_filter *)bzalloc(sizeof(struct test_card_filter));
	new (f) test_card_filter();
	f->context = source;

	obs_enter_graphics();
	char *path = obs_module_file("streamup-test-card.effect");
	f->effect = gs_effect_create_from_file(path, nullptr);
	bfree(path);

	if (f->effect) {
		f->p_overlay = gs_effect_get_param_by_name(f->effect, "overlay");
		f->p_bg_image = gs_effect_get_param_by_name(f->effect, "bg_image");
		f->p_dimension = gs_effect_get_param_by_name(f->effect, "dimension");
		f->p_overlay_size = gs_effect_get_param_by_name(f->effect, "overlay_size");
		f->p_text_offset = gs_effect_get_param_by_name(f->effect, "text_offset");
		f->p_background_style = gs_effect_get_param_by_name(f->effect, "background_style");
		f->p_solid_color = gs_effect_get_param_by_name(f->effect, "solid_color");
		f->p_bars_level = gs_effect_get_param_by_name(f->effect, "bars_level");
		f->p_grid_color = gs_effect_get_param_by_name(f->effect, "grid_color");
		f->p_grid_spacing = gs_effect_get_param_by_name(f->effect, "grid_spacing");
		f->p_grid_line_width = gs_effect_get_param_by_name(f->effect, "grid_line_width");
		f->p_crosshair_width = gs_effect_get_param_by_name(f->effect, "crosshair_width");
		f->p_image_fit = gs_effect_get_param_by_name(f->effect, "image_fit");
		f->p_image_aspect = gs_effect_get_param_by_name(f->effect, "image_aspect");
		f->p_border_color = gs_effect_get_param_by_name(f->effect, "border_color");
		f->p_border_width = gs_effect_get_param_by_name(f->effect, "border_width");
	} else {
		blog(LOG_ERROR, "[StreamUP Test Card] Failed to load streamup-test-card.effect");
	}

	// 1x1 transparent fallback texture.
	const uint8_t transparent[4] = {0, 0, 0, 0};
	const uint8_t *tptr = transparent;
	f->blank_tex = gs_texture_create(1, 1, GS_BGRA, 1, &tptr, GS_DYNAMIC);
	obs_leave_graphics();

	tc_update(f, settings);
	return f;
}

static void tc_destroy(void *data)
{
	auto *f = (struct test_card_filter *)data;

	obs_enter_graphics();
	if (f->effect)
		gs_effect_destroy(f->effect);
	if (f->overlay_tex)
		gs_texture_destroy(f->overlay_tex);
	if (f->blank_tex)
		gs_texture_destroy(f->blank_tex);
	if (f->bg_img_loaded)
		gs_image_file_free(&f->bg_img);
	obs_leave_graphics();

	bfree(f->bg_img_path);
	f->~test_card_filter();
	bfree(f);
}

static void tc_render(void *data, gs_effect_t *)
{
	auto *f = (struct test_card_filter *)data;
	if (!f->effect) {
		obs_source_skip_video_filter(f->context);
		return;
	}

	obs_source_t *target = obs_filter_get_target(f->context);
	uint32_t w = target ? obs_source_get_base_width(target) : 0;
	uint32_t h = target ? obs_source_get_base_height(target) : 0;
	if (w == 0 || h == 0) {
		obs_source_skip_video_filter(f->context);
		return;
	}

	int overlay_w, overlay_h;
	{
		std::lock_guard<std::mutex> lk(f->overlay_mutex);
		overlay_w = f->overlay_w;
		overlay_h = f->overlay_h;
		if (f->overlay_dirty) {
			const int ow = f->overlay_w > 0 ? f->overlay_w : 1;
			const int oh = f->overlay_h > 0 ? f->overlay_h : 1;
			const uint8_t transparent[4] = {0, 0, 0, 0};
			const uint8_t *ptr = (f->overlay_w > 0 && !f->overlay_px.empty())
						     ? f->overlay_px.data()
						     : transparent;
			if (!f->overlay_tex || f->tex_w != ow || f->tex_h != oh) {
				if (f->overlay_tex)
					gs_texture_destroy(f->overlay_tex);
				f->overlay_tex = gs_texture_create(ow, oh, GS_BGRA, 1, &ptr, GS_DYNAMIC);
				f->tex_w = ow;
				f->tex_h = oh;
			} else {
				gs_texture_set_image(f->overlay_tex, ptr, ow * 4, false);
			}
			f->overlay_dirty = false;
		}
	}

	if (!obs_source_process_filter_begin(f->context, GS_RGBA, OBS_ALLOW_DIRECT_RENDERING))
		return;

	struct vec2 dim;
	vec2_set(&dim, (float)w, (float)h);
	struct vec2 osz;
	vec2_set(&osz, (float)(overlay_w > 0 ? overlay_w : 0), (float)(overlay_h > 0 ? overlay_h : 0));
	struct vec2 toff;
	vec2_set(&toff, f->text_off_x, f->text_off_y);

	gs_effect_set_vec2(f->p_dimension, &dim);
	gs_effect_set_vec2(f->p_overlay_size, &osz);
	gs_effect_set_vec2(f->p_text_offset, &toff);
	gs_effect_set_int(f->p_background_style, f->background_style);
	gs_effect_set_vec4(f->p_solid_color, &f->solid_color);
	gs_effect_set_float(f->p_bars_level, f->bars_level);
	gs_effect_set_vec4(f->p_grid_color, &f->grid_color);
	gs_effect_set_float(f->p_grid_spacing, f->grid_spacing);
	gs_effect_set_float(f->p_grid_line_width, f->grid_line_width);
	gs_effect_set_float(f->p_crosshair_width, f->crosshair_width);
	gs_effect_set_int(f->p_image_fit, f->image_fit);
	gs_effect_set_float(f->p_image_aspect, f->image_aspect);
	gs_effect_set_vec4(f->p_border_color, &f->border_color);
	gs_effect_set_float(f->p_border_width, f->border_width);

	gs_effect_set_texture(f->p_overlay, f->overlay_tex ? f->overlay_tex : f->blank_tex);
	gs_effect_set_texture(f->p_bg_image, f->bg_img_loaded ? f->bg_img.texture : f->blank_tex);

	obs_source_process_filter_end(f->context, f->effect, w, h);
}

// ---------------------------------------------------------------------------
// Properties (adaptive: controls show/hide with the chosen background)
// ---------------------------------------------------------------------------
static void set_group_visible(obs_properties_t *props, const char *const *keys, size_t n, bool vis)
{
	for (size_t i = 0; i < n; i++) {
		obs_property_t *p = obs_properties_get(props, keys[i]);
		if (p)
			obs_property_set_visible(p, vis);
	}
}

static bool background_changed(obs_properties_t *props, obs_property_t *, obs_data_t *s)
{
	const int style = (int)obs_data_get_int(s, S_BACKGROUND);
	static const char *grid_keys[] = {S_GRID_COLOR, S_GRID_SPACING, S_GRID_LINE_WIDTH, S_CROSSHAIR_WIDTH};
	static const char *image_keys[] = {S_IMAGE_FILE, S_IMAGE_FIT, S_IMAGE_ASPECT};

	set_group_visible(props, grid_keys, 4, style == 1);
	set_group_visible(props, image_keys, 3, style == 3);
	obs_property_set_visible(obs_properties_get(props, S_BARS_LEVEL), style == 0);
	return true;
}

static bool use_src_name_changed(obs_properties_t *props, obs_property_t *, obs_data_t *s)
{
	const bool use = obs_data_get_bool(s, S_USE_SRC_NAME);
	obs_property_set_visible(obs_properties_get(props, S_TEXT), !use);
	return true;
}

static bool plate_changed(obs_properties_t *props, obs_property_t *, obs_data_t *s)
{
	const bool on = obs_data_get_bool(s, S_PLATE_ENABLED);
	static const char *keys[] = {S_PLATE_COLOR, S_PLATE_PADDING, S_PLATE_RADIUS};
	set_group_visible(props, keys, 3, on);
	return true;
}

static bool border_changed(obs_properties_t *props, obs_property_t *, obs_data_t *s)
{
	const bool on = obs_data_get_bool(s, S_BORDER_ENABLED);
	static const char *keys[] = {S_BORDER_COLOR, S_BORDER_WIDTH};
	set_group_visible(props, keys, 2, on);
	return true;
}

static bool feedback_clicked(obs_properties_t *, obs_property_t *, void *)
{
	QWidget *main = (QWidget *)obs_frontend_get_main_window();
	su::show_feedback_dialog(main, QStringLiteral("StreamUP Test Card"),
				 QStringLiteral("v" PROJECT_VERSION));
	return false;
}

static obs_properties_t *tc_properties(void *)
{
	obs_properties_t *props = obs_properties_create();

	// ── Background ──
	obs_property_t *bg = obs_properties_add_list(props, S_BACKGROUND, "Background",
						     OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(bg, "Colour Bars", 0);
	obs_property_list_add_int(bg, "Grid + Crosshair", 1);
	obs_property_list_add_int(bg, "Solid Colour", 2);
	obs_property_list_add_int(bg, "Image", 3);
	obs_property_set_modified_callback(bg, background_changed);

	obs_properties_add_color_alpha(props, S_SOLID_COLOR, "Solid / Backdrop Colour");
	obs_properties_add_float_slider(props, S_BARS_LEVEL, "Bars Brightness", 0.1, 1.0, 0.01);

	// ── Grid ──
	obs_properties_add_color_alpha(props, S_GRID_COLOR, "Grid Line Colour");
	obs_properties_add_int_slider(props, S_GRID_SPACING, "Grid Spacing (px)", 8, 400, 1);
	obs_properties_add_float_slider(props, S_GRID_LINE_WIDTH, "Grid Line Width (px)", 1.0, 10.0, 0.5);
	obs_properties_add_float_slider(props, S_CROSSHAIR_WIDTH, "Crosshair Width (px)", 0.0, 20.0, 0.5);

	// ── Image ──
	obs_properties_add_path(props, S_IMAGE_FILE, "Image File", OBS_PATH_FILE,
				"Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp);;All Files (*.*)", nullptr);
	obs_property_t *fit = obs_properties_add_list(props, S_IMAGE_FIT, "Fit Mode",
						      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(fit, "Stretch", 0);
	obs_property_list_add_int(fit, "Fit (letterbox)", 1);
	obs_property_list_add_int(fit, "Fill (crop)", 2);
	obs_property_t *asp = obs_properties_add_list(props, S_IMAGE_ASPECT, "Image Aspect",
						      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(asp, "16:9 (Widescreen)", 0);
	obs_property_list_add_int(asp, "1:1 (Square)", 1);
	obs_property_list_add_int(asp, "9:16 (Portrait)", 2);
	obs_property_list_add_int(asp, "4:3", 3);
	obs_property_list_add_int(asp, "3:4", 4);
	obs_property_list_add_int(asp, "21:9 (Ultrawide)", 5);

	// ── Label ──
	obs_property_t *usen = obs_properties_add_bool(props, S_USE_SRC_NAME,
						       "Use Source Name as Label");
	obs_property_set_long_description(
		usen, "Label the card with the name of the source this filter is applied to.");
	obs_property_set_modified_callback(usen, use_src_name_changed);
	obs_properties_add_text(props, S_TEXT, "Label Text", OBS_TEXT_DEFAULT);
	obs_properties_add_font(props, S_FONT, "Font");
	obs_properties_add_color_alpha(props, S_TEXT_COLOR, "Text Colour");
	obs_properties_add_int_slider(props, S_TEXT_OFF_X, "Text Offset X (px)", -1920, 1920, 1);
	obs_properties_add_int_slider(props, S_TEXT_OFF_Y, "Text Offset Y (px)", -1080, 1080, 1);

	// ── Label Plate ──
	obs_property_t *plate = obs_properties_add_bool(props, S_PLATE_ENABLED, "Label Plate");
	obs_property_set_modified_callback(plate, plate_changed);
	obs_properties_add_color_alpha(props, S_PLATE_COLOR, "Plate Colour");
	obs_properties_add_int_slider(props, S_PLATE_PADDING, "Plate Padding (px)", 0, 200, 1);
	obs_properties_add_int_slider(props, S_PLATE_RADIUS, "Plate Corner Radius (px)", 0, 200, 1);

	// ── Frame Border ──
	obs_property_t *border = obs_properties_add_bool(props, S_BORDER_ENABLED, "Frame Border");
	obs_property_set_modified_callback(border, border_changed);
	obs_properties_add_color_alpha(props, S_BORDER_COLOR, "Border Colour");
	obs_properties_add_int_slider(props, S_BORDER_WIDTH, "Border Width (px)", 1, 60, 1);

	// ── Footer + feedback ──
	obs_properties_add_button2(props, "feedback", "Send Feedback", feedback_clicked, nullptr);
	obs_property_t *footer = obs_properties_add_text(
		props, "footer",
		"<b>StreamUP Test Card</b> by Andi &middot; "
		"<a href=\"https://streamup.tips\">streamup.tips</a> v" PROJECT_VERSION " &middot; "
		"<a href=\"https://paypal.me/andilippi\">Send us a Beer!</a>",
		OBS_TEXT_INFO);
	obs_property_text_set_info_word_wrap(footer, false);

	return props;
}

static void tc_defaults(obs_data_t *s)
{
	obs_data_set_default_int(s, S_BACKGROUND, 0);
	obs_data_set_default_int(s, S_SOLID_COLOR, rgba(13, 13, 15, 255));
	obs_data_set_default_double(s, S_BARS_LEVEL, 0.75);

	obs_data_set_default_int(s, S_GRID_COLOR, rgba(90, 90, 102, 255));
	obs_data_set_default_int(s, S_GRID_SPACING, 80);
	obs_data_set_default_double(s, S_GRID_LINE_WIDTH, 1.0);
	obs_data_set_default_double(s, S_CROSSHAIR_WIDTH, 3.0);

	obs_data_set_default_int(s, S_IMAGE_FIT, 0);
	obs_data_set_default_int(s, S_IMAGE_ASPECT, 0);

	obs_data_set_default_string(s, S_TEXT, "CAMERA 1");
	obs_data_set_default_bool(s, S_USE_SRC_NAME, false);
	obs_data_set_default_int(s, S_TEXT_COLOR, rgba(255, 255, 255, 255));
	obs_data_set_default_int(s, S_TEXT_OFF_X, 0);
	obs_data_set_default_int(s, S_TEXT_OFF_Y, 0);

	obs_data_t *font = obs_data_create();
	obs_data_set_string(font, "face", "Arial");
	obs_data_set_int(font, "size", 160);
	obs_data_set_int(font, "flags", 0);
	obs_data_set_default_obj(s, S_FONT, font);
	obs_data_release(font);

	obs_data_set_default_bool(s, S_PLATE_ENABLED, true);
	obs_data_set_default_int(s, S_PLATE_COLOR, rgba(0, 0, 0, 140));
	obs_data_set_default_int(s, S_PLATE_PADDING, 28);
	obs_data_set_default_int(s, S_PLATE_RADIUS, 18);

	obs_data_set_default_bool(s, S_BORDER_ENABLED, false);
	obs_data_set_default_int(s, S_BORDER_COLOR, rgba(255, 255, 255, 255));
	obs_data_set_default_int(s, S_BORDER_WIDTH, 6);
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------
void register_test_card_filter(void)
{
	struct obs_source_info info = {};
	info.id = TEST_CARD_FILTER_ID;
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = tc_get_name;
	info.create = tc_create;
	info.destroy = tc_destroy;
	info.update = tc_update;
	info.video_render = tc_render;
	info.get_properties = tc_properties;
	info.get_defaults = tc_defaults;
	obs_register_source(&info);
}
