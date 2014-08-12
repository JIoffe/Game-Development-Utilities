///////////////////////////////////////////
// Houses per-sprite collision data
// Bounding Box, spheres, anything relevant
// (C) Foofles

#pragma once

struct CollisionData{
	static const int SpotGridWidth = 6;
	float BoundingBox_Left;
	float BoundingBox_Right;
	float BoundingBox_Top;
	float BoundingBox_Bottom;

	int SpotGrid[SpotGridWidth][SpotGridWidth];
};