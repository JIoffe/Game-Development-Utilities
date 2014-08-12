#pragma once
#include <vector>
#include "TextureTreeNode.h"


class Globals{
public:
	static std::vector<TextureTreeNode*> EmptyNodesList;  //Rather than stepping through all the nodes, we'll just step through these
};
