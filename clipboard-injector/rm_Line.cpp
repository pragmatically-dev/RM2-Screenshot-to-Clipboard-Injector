#include "rm_Line.hpp"

Line Line::fromPoints(QList<LinePoint> &&points, const QRectF& bounds) {
	Line line = {};
	line.tool = 0x17; // Ballpoint v2
	line.color = 0; // Black
	line.rgba = 0xff000000;
	line.points = QList(points.begin(), points.end());
	line.maskScale = 1.0;
	line.thickness = 0.0f;
	line.bounds = bounds;
	return line;
}
