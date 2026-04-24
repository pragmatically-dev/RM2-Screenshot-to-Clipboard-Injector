#pragma once

#include <memory>
#include <QList>
#include <QRectF>

struct LinePoint {
	float x;
	float y;
	unsigned short speed;
	unsigned short width;
	unsigned char direction;
	unsigned char pressure;
} __attribute__((packed));
static_assert(sizeof(LinePoint) == 0xe, "LinePoint size mismatch");

struct Line {
	int tool;
	int color;
	unsigned int rgba;

	QList<LinePoint> points;

	double maskScale;
	float thickness;

	// these bounds are used for early hit testing on erase & select
	QRectF bounds;

	static Line fromPoints(QList<LinePoint> &&points, const QRectF& bounds);
};
