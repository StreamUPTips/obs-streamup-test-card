#include "test-card-overlay.hpp"

#include <obs.h> // OBS_FONT_* flags

#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QRect>

namespace StreamUP {
namespace TestCard {

QImage render_label(const LabelParams &p)
{
	const QString text = p.text;
	if (text.trimmed().isEmpty())
		return QImage();

	QFont font(p.font_face.isEmpty() ? QStringLiteral("Arial") : p.font_face);
	font.setPixelSize(p.font_size > 0 ? p.font_size : 48);
	if (p.font_flags & OBS_FONT_BOLD)
		font.setWeight(QFont::Bold);
	if (p.font_flags & OBS_FONT_ITALIC)
		font.setItalic(true);
	if (p.font_flags & OBS_FONT_UNDERLINE)
		font.setUnderline(true);
	if (p.font_flags & OBS_FONT_STRIKEOUT)
		font.setStrikeOut(true);
	font.setStyleStrategy(QFont::PreferAntialias);

	// Measure the (possibly multi-line) text block.
	QFontMetrics fm(font);
	const int flags = Qt::AlignCenter | Qt::TextWordWrap;
	QRect bounds = fm.boundingRect(QRect(0, 0, 1 << 20, 1 << 20),
				       Qt::AlignLeft | Qt::TextExpandTabs, text);

	// Padding: the plate padding when the plate is on, otherwise a small margin
	// so antialiased glyph edges and descenders are never clipped.
	const int pad = p.plate_enabled ? qMax(0, p.plate_padding)
					: qMax(4, p.font_size / 8);

	int w = qMax(1, bounds.width() + 2 * pad);
	int h = qMax(1, bounds.height() + 2 * pad);

	QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
	img.fill(Qt::transparent);

	QPainter pnt(&img);
	pnt.setRenderHint(QPainter::Antialiasing, true);
	pnt.setRenderHint(QPainter::TextAntialiasing, true);

	if (p.plate_enabled) {
		const qreal r = qMax(0, p.plate_radius);
		pnt.setPen(Qt::NoPen);
		pnt.setBrush(p.plate_color);
		pnt.drawRoundedRect(QRectF(0.5, 0.5, w - 1.0, h - 1.0), r, r);
	}

	pnt.setFont(font);
	pnt.setPen(p.text_color);
	pnt.drawText(QRect(0, 0, w, h), flags, text);
	pnt.end();

	return img;
}

} // namespace TestCard
} // namespace StreamUP
