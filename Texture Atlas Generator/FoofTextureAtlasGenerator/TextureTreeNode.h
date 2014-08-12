#pragma once
#include "Globals.h"

struct Coord2D{
	int x;
	int y;
};
struct TextureTreeNode{
	TextureTreeNode * pChildren[2];
	int TextureIndex;
	Coord2D TopLeft;
	Coord2D BottomRight;
	bool bisNode;     //If this connects to more
	bool bIsFilled;   //If this holds data

	//void Insert
	void RenderBorders();
};
