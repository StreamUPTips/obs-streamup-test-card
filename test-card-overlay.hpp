// Label overlay renderer — turns the typed label + plate settings into a tight
// premultiplied bitmap on the CPU (QPainter). The filter uploads the result to a
// GPU texture and the effect places it. Kept off the graphics thread per the
// plugins-tree house rule (no QPainter in video_render).
#pragma once

#include <QImage>
#include <QString>
#include <QColor>

namespace StreamUP {
namespace TestCard {

struct LabelParams {
	QString text;       // final resolved label text (may be empty)
	QString font_face;
	int font_size = 48; // pixels (from the OBS font object)
	uint32_t font_flags = 0;
	QColor text_color = QColor(255, 255, 255, 255);
	bool plate_enabled = true;
	QColor plate_color = QColor(0, 0, 0, 140);
	int plate_padding = 28;
	int plate_radius = 18;
};

// Returns a Format_ARGB32_Premultiplied image sized tightly around the label
// (plus plate padding). Returns a null QImage when there is nothing to draw.
QImage render_label(const LabelParams &p);

} // namespace TestCard
} // namespace StreamUP
