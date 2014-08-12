#include <windows.h>
#include <Commdlg.h>
#include <commctrl.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <gdiplus.h> // Let's let windows do all the graphics grunt work for us..
#include "CollisionData.h"
#include "Globals.h"
#include "resource.h"
using namespace std;
using namespace Gdiplus;
#define FoofMAXFILEBUFFER 320000
const int Padding = 2;
OPENFILENAME ofn;
char cFileName[FoofMAXFILEBUFFER];
char cFileTitle[4000];
OPENFILENAME sfn;
char sfPath[MAX_PATH];
TextureTreeNode mRoot;
vector<Bitmap*> ImageList;  
vector<std::string*> PathList;
vector<std::string*> TitleList;
vector<CollisionData*> ImageCollisionDataList;
Bitmap * pFinalCompositeDIB = NULL;
BYTE * pCompositeData = NULL;
Rect Destination;   //For Zooming
int ZoomAmount = 0;
HDC hCompositeDC;
//Bitmap FinalCompositeImage;
HWND hStatusBar;
int StatusBarHeight;
bool bDrawBorders = true;
stringstream uvStream(stringstream::in | stringstream::out);
//COPIED FROM MSDN So take it up with them if you have problems...
BOOL CALLBACK DlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam){
	switch(msg){
		case WM_INITDIALOG:
			break;
		case WM_COMMAND:{
			switch(LOWORD(wParam))
            {
				case IDAOK:{
					EndDialog(hwnd, IDOK);
					break;
				}
				case IDGOSPEEDRACER:{
					EndDialog(hwnd, IDGOSPEEDRACER);
					break;
				}
				case IDC_LOADLIST:{
					ofn.lpstrFilter = "Text File\0*.txt\0\0";
					if(GetOpenFileName( &ofn ) != 0){
						int nPaths = 0;
						ifstream FileIn(ofn.lpstrFile);
						FileIn >> nPaths;
						if(nPaths > 0){
							const HWND hListBox = GetDlgItem(hwnd, IDC_PATHLIST1);
							for(int i = 0; i < nPaths; i++){
								string * pPathString = new string;
								string * pTitleString = new string;
								FileIn >> *pPathString;
								FileIn >> *pTitleString;
								PathList.push_back(pPathString);
								TitleList.push_back(pTitleString);
								SendMessage(hListBox, LB_ADDSTRING, 0, (LPARAM)pTitleString->c_str());
							}
						}
					}
					break;
				}
				case IDC_ADDIMGS:{
						ofn.lpstrFilter = "PNG Image\0*.png\0JPG Image\0*.jpg\0\0";
					int Result = GetOpenFileName( &ofn );
					if(Result == 0){
						DWORD dwError = CommDlgExtendedError();
						if(dwError == FNERR_BUFFERTOOSMALL){
							MessageBox(NULL, "SDS#$!$%A", "DSAdsadsadz", MB_OK);
						}
					}else{
						const bool bMultipleSelections = (ofn.lpstrFile[ofn.nFileOffset - 1] == '\0'); // If there's a null between directory
						// and file title, then we have multiple entries.
						const HWND hListBox = GetDlgItem(hwnd, IDC_PATHLIST1);
						if(bMultipleSelections){
							for(CHAR * lpstrFileTitle = ofn.lpstrFile + ofn.nFileOffset; *lpstrFileTitle; lpstrFileTitle += (strlen(lpstrFileTitle) + 1)){
								char Path[MAX_PATH];
								strcpy(Path, ofn.lpstrFile);
								strcat(Path, "\\");
								strcat(Path, lpstrFileTitle);
								
								SendMessage(hListBox, LB_ADDSTRING, 0, (LPARAM)lpstrFileTitle);
								string * pPathString = new string(Path);
								string * pTitleString = new string(lpstrFileTitle);
								PathList.push_back(pPathString);
								TitleList.push_back(pTitleString);
								//ParseImageFile(Path, lpstrFileTitle);
							}
						}else{
							SendMessage(hListBox, LB_ADDSTRING, 0, (LPARAM)ofn.lpstrFileTitle);
								string * pPathString = new string(ofn.lpstrFile);
								string * pTitleString = new string(ofn.lpstrFileTitle);
								PathList.push_back(pPathString);
								TitleList.push_back(pTitleString);
							//ParseImageFile(ofn.lpstrFile, ofn.lpstrFileTitle);
						}
					}
					break;
				}
				default:
					break;
			}
			break;
		}
		case WM_CLOSE:{
			EndDialog(hwnd, IDOK);
			break;
		}
		default:
			return FALSE;
	}
	return TRUE;
}
void ParseImageFile(const char * lpstrFilePath, const char * lpstrTitle){
	WCHAR wFileName[5000];
	// Convert to a wchar_t*
	size_t convertedChars = 0;
	mbstowcs_s(&convertedChars, wFileName, strlen(lpstrFilePath) + 1, lpstrFilePath, _TRUNCATE);

	Bitmap * TexChunk =  new Bitmap(wFileName);
	//Store the file name for later.
	PropertyItem TitlePropItem;
	//
	CHAR * Title = new CHAR[strlen(lpstrTitle)+1];
	strcpy(Title, lpstrTitle);
	TitlePropItem.id = PropertyTagImageTitle;
	TitlePropItem.length = strlen(Title) + 1; //Include null terminator 
	TitlePropItem.type = PropertyTagTypeASCII;
	TitlePropItem.value = Title;

	TexChunk->SetPropertyItem(&TitlePropItem);
	ImageList.push_back(TexChunk);

	//Compute collision data per sprite
	//TODO - Activate only if necessary.
	BitmapData bmpdata;
	const int SpriteWidth = TexChunk->GetWidth();
	const int SpriteHeight = TexChunk->GetHeight();
	Rect region(0, 0, SpriteWidth, SpriteHeight); // We want to lock the entire image
	TexChunk->LockBits(&region,
		ImageLockModeRead,
		PixelFormat32bppARGB,
		&bmpdata);
	UINT * pixelData = (UINT*)bmpdata.Scan0;

	//Go through every pixel and compute the bounds UGH
	//TODO - Find better way.
	float Top = 0.0f, Bottom = 0.0f, Left = 1.0f,Right = 0.0f;
	bool bGotTop = false;
	//Top is whatever Y we first encounter an opaque pixel.
	//Bottom is the last Y we encounter an opaque pixel
	//Left is the smallest X that...
	//Right is the largest X that...
	const float MinimumPercentOfArea = 0.75f; //If the pixels in the field represent more than this
	// then this field will be considered for collision
	const BYTE MinAlpha = 30;
	const UINT stride = bmpdata.Stride;
	//Compute % of area of an individual pixel
	const float IndividualPercent = 100.0f / (SpriteWidth * SpriteHeight);
	float SpotGridWeights[CollisionData::SpotGridWidth][CollisionData::SpotGridWidth];
	//Make sure it's 0.
	for(int y = 0; y < CollisionData::SpotGridWidth; y++){
		for(int x = 0; x < CollisionData::SpotGridWidth; x++){
			SpotGridWeights[x][y] = 0.0f;
		}
	}
	for(int y = 0; y < SpriteHeight; y++){
		const UINT YAdvance = y * stride / 4;
		const float YAmount = (float)y / SpriteHeight;
		const int SpotGridY = (int)(CollisionData::SpotGridWidth * YAmount);
		for(int x = 0; x < SpriteWidth; x++){
			BYTE AlphaValue = (pixelData[YAdvance + x] & 0xFF000000) >> 24; //A in ARGB
			if(AlphaValue > MinAlpha){
				const float XAmount = (float)x / SpriteWidth;
				const int SpotGridX = (int)(XAmount * CollisionData::SpotGridWidth);
				SpotGridWeights[SpotGridX][SpotGridY] += IndividualPercent;
				if(!bGotTop){
					Top = YAmount;
					bGotTop = true;
				}
				if(YAmount > Bottom){
					Bottom = YAmount;
				}
				if(XAmount < Left){
					Left = XAmount;
				}
				if(XAmount > Right){
					Right = XAmount;
				}
			}//Endif Alpha > Min
		}
	} //END SPRITE PIXEL LOOP
	TexChunk->UnlockBits(&bmpdata);
	CollisionData * pCD = new CollisionData; //Default Const.
	pCD->BoundingBox_Bottom = Bottom;
	pCD->BoundingBox_Left = Left;
	pCD->BoundingBox_Right = Right;
	pCD->BoundingBox_Top = Top;
	for(int y = 0; y < CollisionData::SpotGridWidth; y++){
		for(int x = 0; x < CollisionData::SpotGridWidth; x++){
			if(SpotGridWeights[x][y] > MinimumPercentOfArea){
				pCD->SpotGrid[x][y] = 1;
			}else{
				pCD->SpotGrid[x][y] = 0;
			}
		}
	}
	ImageCollisionDataList.push_back(pCD);
}
float Lerp(float a, float b, float s){
	return b - ((1.0f - s) * (b - a));
}
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid)
{
   UINT  num = 0;          // number of image encoders
   UINT  size = 0;         // size of the image encoder array in bytes

   ImageCodecInfo* pImageCodecInfo = NULL;

   GetImageEncodersSize(&num, &size);
   if(size == 0)
      return -1;  // Failure

   pImageCodecInfo = (ImageCodecInfo*)(malloc(size));
   if(pImageCodecInfo == NULL)
      return -1;  // Failure

   GetImageEncoders(num, size, pImageCodecInfo);

   for(UINT j = 0; j < num; ++j)
   {
      if( wcscmp(pImageCodecInfo[j].MimeType, format) == 0 )
      {
         *pClsid = pImageCodecInfo[j].Clsid;
         free(pImageCodecInfo);
         return j;  // Success
      }    
   }

   free(pImageCodecInfo);
   return -1;  // Failure
}

void OpenAndProcess(){


	//do{
		if(DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_ADDIMAGES), NULL, DlgProc) != IDGOSPEEDRACER){
			return;
		}

		const int nImages = PathList.size();
		if(nImages > 0){
			for(int i = 0; i < nImages; i++){
				ParseImageFile(PathList[i]->c_str(), TitleList[i]->c_str());
			}
		}else{
			return;
		}
		////Because I'm fucking tired of clicking.
		//string FileNameArray[] = { "C:/Users/Malvina/Documents/fooflew/GAMESPRITES/BLOCK_A.png" ,
		//	 "C:/Users/Malvina/Documents/fooflew/GAMESPRITES/Metal_Plate.png" ,
		//	  "C:/Users/Malvina/Documents/fooflew/GAMESPRITES/BG5.png" ,
		//	  "C:/Users/Malvina/Documents/fooflew/GAMESPRITES/jump_button_new.png" ,
		//	  "C:/Users/Malvina/Documents/fooflew/GAMESPRITES/fire_button_new.png" ,
		//	  "C:/Users/Malvina/Documents/fooflew/GAMESPRITES/dir_arrows.png" ,
		//	  "C:/Users/Malvina/Documents/fooflew/GAMESPRITES/zero.png" ,
		//	  "C:/Users/Malvina/Documents/fooflew/GAMESPRITES/number1.png" ,
		//	  "C:/Users/Malvina/Documents/fooflew/GAMESPRITES/number2.png" ,
		//	  "C:/Users/Malvina/Documents/fooflew/GAMESPRITES/number3.png" ,
		//	  "C:/Users/Malvina/Documents/fooflew/GAMESPRITES/number4.png" ,
		//	  "C:/Users/Malvina/Documents/fooflew/GAMESPRITES/number5.png" ,
		//	  "C:/Users/Malvina/Documents/fooflew/GAMESPRITES/number6.png" ,
		//	  "C:/Users/Malvina/Documents/fooflew/GAMESPRITES/number7.png" ,
		//	  "C:/Users/Malvina/Documents/fooflew/GAMESPRITES/number8.png" ,
		//	  "C:/Users/Malvina/Documents/fooflew/GAMESPRITES/number9.png" ,
		//	  "C:/Users/Malvina/Documents/fooflew/GAMESPRITES/SCORE1.png" ,
		//	  "C:/Users/Malvina/Documents/fooflew/GAMESPRITES/explosion.png" ,
		//	  "C:/Users/Malvina/Documents/fooflew/GAMESPRITES/BAT_1.png" ,
		//	  "C:/Users/Malvina/Documents/fooflew/GAMESPRITES/BAT_2.png" ,
		//	  "C:/Users/Malvina/Documents/fooflew/GAMESPRITES/BAT_3.png" ,
		//	  "C:/Users/Malvina/Documents/fooflew/GAMESPRITES/BAT_4.png" ,
		//	  "C:/Users/Malvina/Documents/fooflew/GAMESPRITES/BAT_5.png" ,
		//	  "C:/Users/Malvina/Documents/fooflew/GAMESPRITES/bunny_1.png" ,
		//	  "C:/Users/Malvina/Documents/fooflew/GAMESPRITES/bunny_2.png" ,
		//	  "C:/Users/Malvina/Documents/fooflew/GAMESPRITES/bunny_3.png" ,
		//	  "C:/Users/Malvina/Documents/fooflew/GAMESPRITES/bunny_4.png" ,
		//	  "C:/Users/Malvina/Documents/fooflew/GAMESPRITES/bunny_5.png" ,
		//	  "C:/Users/Malvina/Documents/fooflew/GAMESPRITES/bunny_6.png" ,
		//	  "C:/Users/Malvina/Documents/fooflew/GAMESPRITES/stupid_energy_ball1.png", 	
		//	  "C:/Users/Malvina/Documents/fooflew/GAMESPRITES/stupid_energy_ball2.png", 
		//	  "C:/Users/Malvina/Documents/fooflew/GAMESPRITES/PLATFORM_LEFT.png", 
		//	  "C:/Users/Malvina/Documents/fooflew/GAMESPRITES/PLATFORM_CENTER.png",
		//	"C:/Users/Malvina/Documents/fooflew/GAMESPRITES/PLATFORM_RIGHT.png",
		//	"C:/Users/Malvina/Documents/fooflew/GAMESPRITES/ROCK_PILLAR.png",
		//		"C:/Users/Malvina/Documents/fooflew/GAMESPRITES/ROCK_PILLAR2.png",
		//	"C:/Users/Malvina/Documents/fooflew/GAMESPRITES/coin.png",
		//	"C:/Users/Malvina/Documents/fooflew/GAMESPRITES/laser_bullet.png",
		//"C:/Users/Malvina/Documents/fooflew/GAMESPRITES/biggem.png",
		//"C:/Users/Malvina/Documents/fooflew/GAMESPRITES/robot1.png",
		//"C:/Users/Malvina/Documents/fooflew/GAMESPRITES/robot2.png",};

		//const int SpriteCount = sizeof(FileNameArray) / sizeof(string);
		//for(int i = 0; i < SpriteCount; i++){
		//	size_t TitleBegin = FileNameArray[i].rfind("/");
		//	string Title = FileNameArray[i];
		//	Title.erase(0, TitleBegin+1);
		//	ParseImageFile(FileNameArray[i].c_str(), Title.c_str());
		//}
	//}
//	while(Result != 0);

	//Loop through images and figure the smallest square we need to fit them all
	int Area = 0;
	for(vector<Bitmap*>::iterator it = ImageList.begin(); it != ImageList.end(); it++){
		Bitmap * iImage = *it;
		Area += (iImage->GetHeight() + Padding) * (iImage->GetWidth() + Padding);
	}
	//Find the smallest square to a power of 2
	//That can fit this area.
	double AreaSqRoot = sqrt((double)Area);
	int AtlasWidth = 2;
	do{
		AtlasWidth *= 2;
	}while(AtlasWidth < AreaSqRoot);
	AtlasWidth = 2048;
	//AtlasWidth*= 2;
	//=======================================================
	//Create the DIB that will hold the full atlas
	//=======================================================

	pFinalCompositeDIB = new Bitmap(AtlasWidth, AtlasWidth);
	//Also Initialize Zooming Rect to hold the whole thing..
	Destination.X = 0;
	Destination.Y = 0;
	Destination.Width = AtlasWidth;
	Destination.Height = AtlasWidth;
	ZoomAmount = AtlasWidth / 4;
	//Now that we know how big, let's start chopping it up!
	mRoot.TopLeft.x = 0;
	mRoot.TopLeft.y = 0;
	mRoot.BottomRight.x = AtlasWidth;
	mRoot.BottomRight.y = AtlasWidth;
	

	//*****************************************************
	//Let 'er rip.
	//*****************************************************
	Globals::EmptyNodesList.push_back(&mRoot);
	{
		uvStream << "Foof72 " << ' ' << ImageList.size() << ' ';
		Graphics g(pFinalCompositeDIB);
		//TEMP TEMP TEMP 
		int CurrentSprite = 0;
		for(vector<Bitmap*>::iterator it = ImageList.begin(); it != ImageList.end(); it++){

			Bitmap * iImage = *it;
			//Output image title to our atlas data stream
			{
				UINT uSize = iImage->GetPropertyItemSize(PropertyTagImageTitle);
				PropertyItem* title = (PropertyItem*)malloc(uSize);
				iImage->GetPropertyItem(PropertyTagImageTitle, uSize, title);
				uvStream << (LPCSTR)title->value << ' ';
				free(title);
			}
			int height = iImage->GetHeight() + Padding;
			int width = iImage->GetWidth() + Padding;

			TextureTreeNode * pTightestFit = NULL; // hyuk hyuk
			int PaddingX = 0;
			int PaddingY = 0;
			for(vector<TextureTreeNode*>::iterator node_it = Globals::EmptyNodesList.begin(); node_it != Globals::EmptyNodesList.end(); node_it++){
				int CurrentPaddingX, CurrentPaddingY;
				int node_width = (*node_it)->BottomRight.x - (*node_it)->TopLeft.x;
				int node_height = (*node_it)->BottomRight.y - (*node_it)->TopLeft.y;
				if(node_width == width && node_height == height){
					//Perfect fit
					pTightestFit = (*node_it);
					break;
				}
				else{
					CurrentPaddingX = node_width - width;
					CurrentPaddingY = node_height - height;
					if(CurrentPaddingX >= 0 && CurrentPaddingY >= 0){
						//If we actually fit. Negative values mean we don't.
						if(pTightestFit == NULL){
							pTightestFit = (*node_it);
							PaddingY = CurrentPaddingY;
							PaddingX = CurrentPaddingX;
						}
						else{
							if(CurrentPaddingX < PaddingX || CurrentPaddingY < PaddingY){
								PaddingY = CurrentPaddingY;
								PaddingX = CurrentPaddingX;
								pTightestFit = (*node_it);
							}
						}
					}
				}
			} //End For
			//============================================================
			//Now that we know what the tightest fit is, if there is one
			//We have to insert the image into it
			//============================================================
			if(pTightestFit != NULL){
				if(!(PaddingX == 0 && PaddingY == 0)){
					//If we're not a perfect fit we need to split it
					if(PaddingX > PaddingY){
						//Split Horizontally
						pTightestFit->pChildren[0] = new TextureTreeNode();
						pTightestFit->pChildren[0]->TopLeft.x = pTightestFit->TopLeft.x + width;
						pTightestFit->pChildren[0]->TopLeft.y = pTightestFit->TopLeft.y;
						pTightestFit->pChildren[0]->BottomRight = pTightestFit->BottomRight;
						Globals::EmptyNodesList.push_back(pTightestFit->pChildren[0]);
						if(PaddingY > 0){
							//Split remaining section
							pTightestFit->pChildren[1] = new TextureTreeNode();
							pTightestFit->pChildren[1]->TopLeft.x = pTightestFit->TopLeft.x;
							pTightestFit->pChildren[1]->TopLeft.y = pTightestFit->TopLeft.y + height;
							pTightestFit->pChildren[1]->BottomRight.x = pTightestFit->TopLeft.x + width;
							pTightestFit->pChildren[1]->BottomRight.y = pTightestFit->BottomRight.y;
							Globals::EmptyNodesList.push_back(pTightestFit->pChildren[1]);
						}
					}else{
						//Split Vertically
						pTightestFit->pChildren[1] = new TextureTreeNode();
						pTightestFit->pChildren[1]->TopLeft.x = pTightestFit->TopLeft.x;
						pTightestFit->pChildren[1]->TopLeft.y = pTightestFit->TopLeft.y + height;
						pTightestFit->pChildren[1]->BottomRight = pTightestFit->BottomRight;
						Globals::EmptyNodesList.push_back(pTightestFit->pChildren[1]);
						if(PaddingX > 0){
							//Split remaining section
							pTightestFit->pChildren[0] = new TextureTreeNode();
							pTightestFit->pChildren[0]->TopLeft.x = pTightestFit->TopLeft.x + width;
							pTightestFit->pChildren[0]->TopLeft.y = pTightestFit->TopLeft.y;
							pTightestFit->pChildren[0]->BottomRight.x = pTightestFit->BottomRight.x;
							pTightestFit->pChildren[0]->BottomRight.y = pTightestFit->TopLeft.y + height;
							Globals::EmptyNodesList.push_back(pTightestFit->pChildren[0]);
						}
					}
				} //End Splitting
				//Remove this cell from our elligible bachelor list...
				for(vector<TextureTreeNode*>::iterator node_it = Globals::EmptyNodesList.begin(); node_it != Globals::EmptyNodesList.end(); node_it++){
					if((*node_it) == pTightestFit){
						Globals::EmptyNodesList.erase(node_it);
						break;
					}
				}
				//==========================================================
				// Copy this image into our atlas bitmap
				//===========================================================
				//char Moo[200];
				//sprintf(Moo, "X: %i, Y: %i", pTightestFit->TopLeft.x, pTightestFit->TopLeft.y);
				//MessageBox(NULL, Moo, "WTF", MB_OK);
				//Repeat edge pixels to solve bleeding issue.
				g.DrawImage((*it),  Rect(pTightestFit->TopLeft.x ,pTightestFit->TopLeft.y+1, 1, height-2), 0, 0, 1, height-2,UnitPixel);
				g.DrawImage((*it),  Rect(pTightestFit->TopLeft.x+width-1 ,pTightestFit->TopLeft.y+1, 1, height-2), width-3, 0, 1, height-2,UnitPixel);
				g.DrawImage((*it),  Rect(pTightestFit->TopLeft.x+1 ,pTightestFit->TopLeft.y, width-2, 1), 0, 0, width-2, 1,UnitPixel);
				Pen borderPen(Color(200, 45, 255), 1);
				//g.DrawLine(&borderPen, Point(pTightestFit->TopLeft.x, pTightestFit->TopLeft.y), Point(pTightestFit->TopLeft.x + width - 1, pTightestFit->TopLeft.y));
				Pen bPen(Color(200, 200, 2), 1);
				//g.DrawLine(&borderPen, Point(pTightestFit->TopLeft.x, pTightestFit->TopLeft.y + height-1), Point(pTightestFit->TopLeft.x + width - 1, pTightestFit->TopLeft.y + height-1));
				g.DrawImage((*it),  Rect(pTightestFit->TopLeft.x+1 ,pTightestFit->TopLeft.y + height-1, width-2, 1), 0, height-3, width-2, 1,UnitPixel);
				g.DrawImage((*it), Rect(pTightestFit->TopLeft.x + 1,pTightestFit->TopLeft.y + 1, width-2, height-2));


				//TODO - TEMP.
				if(0){
					//DRaw bounding box and circles.
					Pen borderPen(Color(200, 45, 255), 1);
					g.DrawRectangle(&borderPen, pTightestFit->TopLeft.x + ImageCollisionDataList[CurrentSprite]->BoundingBox_Left * width,
									pTightestFit->TopLeft.y + ImageCollisionDataList[CurrentSprite]->BoundingBox_Top * width,
									(ImageCollisionDataList[CurrentSprite]->BoundingBox_Right - ImageCollisionDataList[CurrentSprite]->BoundingBox_Left)*width,
									(ImageCollisionDataList[CurrentSprite]->BoundingBox_Bottom - ImageCollisionDataList[CurrentSprite]->BoundingBox_Top)*height);
					//Ciiiircles!
					//Determine drawn width of circle
					const int CircleWidth = width / CollisionData::SpotGridWidth;
					for(int y = 0; y < CollisionData::SpotGridWidth; y++){
						for(int x = 0; x < CollisionData::SpotGridWidth; x++){
							if(ImageCollisionDataList[CurrentSprite]->SpotGrid[x][y] != 0){
								g.DrawEllipse(&borderPen, x*CircleWidth + pTightestFit->TopLeft.x, y*CircleWidth + pTightestFit->TopLeft.y, CircleWidth, CircleWidth);
							}
						}
					}
				}
				//for(int i = 0; i < nSprites; i++){
				//	const CollisionData * pCD = ImageCollisionDataList[i];
				//	const Bitmap * pBMP = ImageList[i];
				//}
				//Save a log of UV coordinates (0.0 - 1.0)
				uvStream << (float)(pTightestFit->TopLeft.x + 1) / AtlasWidth << ' ' 
						 << (float)(pTightestFit->TopLeft.y + 1) / AtlasWidth << ' '
					     << (float)(pTightestFit->TopLeft.x + width - 2) / AtlasWidth << ' '
					     << (float)(pTightestFit->TopLeft.y + height - 2) / AtlasWidth << ' ';
			} //Endif (pTightestFit != NULL)
			else{
				//We couldn't fit... Uh oh.
			}
			CurrentSprite++;
			GdiFlush();
		} //End For (imagelist) loop
	}
	//Update our status bar
	char StatusText[MAX_PATH];
	int sectionwidths[] = {230, -1};
    SendMessage(hStatusBar, SB_SETPARTS, 2, (LPARAM)sectionwidths);
	sprintf(StatusText, "Created Atlas containing %i textures.", ImageList.size());
	SendMessage(hStatusBar, SB_SETTEXT, 0, (LPARAM)StatusText);
	sprintf(StatusText, "Size: %i x %i", AtlasWidth, AtlasWidth);
	SendMessage(hStatusBar, SB_SETTEXT, 1, (LPARAM)StatusText);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch(msg){
		case WM_COMMAND:{
			switch(LOWORD(wParam)){
				case IDM_OPEN1:{
					OpenAndProcess();
					InvalidateRect(hwnd, NULL, 0); //Force Repaint
					break;
				}
				case IDM_SAVE1:{
					if(pFinalCompositeDIB){				
						if(GetSaveFileName(&sfn) != 0){
							WCHAR wFileName[MAX_PATH];
							//Make sure we have .png at the end
							int sfLength = strlen(sfPath);
							sfLength -= 4;
							if(strcmp(sfPath+sfLength, ".png") != 0){
								strcat(sfPath, ".png");
							}
							// Convert to a wchar_t*
							size_t convertedChars = 0;
							mbstowcs_s(&convertedChars, wFileName, strlen(sfPath) + 1, sfPath, _TRUNCATE);
							CLSID pngClsid;
							GetEncoderClsid(L"image/png", &pngClsid);
	
							pFinalCompositeDIB->Save(wFileName, &pngClsid, NULL);

							//Save UV data
							//Use the same name , just change extension
							char CollisionPath[MAX_PATH];
							strcpy(CollisionPath, sfPath);
							strcpy(sfPath+(strlen(sfPath) - 4), ".uv");						
							ofstream FileOut(sfPath, ios::trunc | ios::out);
							
							//Get Length of uvStream;
							int uvLength = 2 + ImageList.size() * 5;
							string Reader;
							for(int i = 0; i < uvLength; i++){
								uvStream >> Reader;
								FileOut << Reader << endl;
							}
							FileOut.close();
							//SAVE LIST OF FILES FOR FUTURE REFERENCE
							{
								char cPathList[MAX_PATH];
								strcpy(cPathList, CollisionPath);
								strcpy(cPathList+(strlen(cPathList) - 4), ".txt");		//Regular ASCII TEXT FILE				
								ofstream PathFileOut(cPathList, ios::trunc | ios::out);
								const int nPaths = PathList.size();
								PathFileOut << nPaths << endl;
								for(int i = 0; i < nPaths; i++){
									PathFileOut << *PathList[i] << endl;
									PathFileOut << *TitleList[i] << endl;
								}
							}
							{
								strcpy(CollisionPath + (strlen(CollisionPath) - 4), "_collision.col"); //.Col for collisions.
								//const int nEntries = ImageCollisionDataList.size();
								ofstream CollisionFileOut(CollisionPath, ios::trunc | ios::out);
								CollisionFileOut << ImageList.size() << endl;
								CollisionFileOut << CollisionData::SpotGridWidth << endl
												 << 2.0f / (float)CollisionData::SpotGridWidth << endl;
								for(vector<CollisionData*>::iterator i = ImageCollisionDataList.begin(); i < ImageCollisionDataList.end(); i++){
									
									CollisionFileOut << Lerp(-1.0f, 1.0f, (*i)->BoundingBox_Left) << endl;
									CollisionFileOut << Lerp(-1.0f, 1.0f, (*i)->BoundingBox_Right) << endl;
									CollisionFileOut << Lerp(1.0f, -1.0f, (*i)->BoundingBox_Top) << endl;
									CollisionFileOut << Lerp(1.0f, -1.0f, (*i)->BoundingBox_Bottom) << endl;
									//Output Spot Grid
									for(int y = 0; y < CollisionData::SpotGridWidth; y++){
										for(int x = 0; x < CollisionData::SpotGridWidth; x++){
											CollisionFileOut << (*i)->SpotGrid[x][y] << endl;
										}
									}
								}
								CollisionFileOut.close();
							}
						}
					}
					break;
				}
				case IDM_ABOUT1:{
					DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_ABOUT), hwnd, DlgProc);
					break;
				}
				case IDM_ZOOM_IN1:{
					Destination.Width += ZoomAmount;
					Destination.Height += ZoomAmount;
					InvalidateRect(hwnd, NULL, NULL);
					break;
				}
				case IDM_ZOOM_OUT1:{
					Destination.Width -= ZoomAmount;
					Destination.Height -= ZoomAmount;
					InvalidateRect(hwnd, NULL, NULL);
					break;
				}
				case IDM_SET_ZOOM_1_1:{
					Destination.Width = pFinalCompositeDIB->GetWidth();
					Destination.Height = pFinalCompositeDIB->GetWidth();
					InvalidateRect(hwnd, NULL, NULL);
					break;
				}
				case IDM_EXIT1:{
					DestroyWindow(hwnd);
					break;
				}
				default:
					break;
			}
			break;
		}
		case WM_CLOSE:{
			DestroyWindow(hwnd);
			break;
		}
		case WM_DESTROY:{
			PostQuitMessage(0);
			break;
		}
		case WM_PAINT:{
			PAINTSTRUCT ps;
			
			HDC hDC = BeginPaint(hwnd, &ps);
			Graphics graphics(hDC);
			//Draw the alpha looking type stuff
			HatchBrush hBGBrush(HatchStyleDiagonalCross, Color(125, 125, 132),
			 Color(55, 55, 55));
			RECT clientRect;
			GetClientRect(hwnd, &clientRect);
						
			graphics.FillRectangle(&hBGBrush, 0, 0, clientRect.right, clientRect.bottom - StatusBarHeight);
			if(pFinalCompositeDIB){
				//graphics.DrawImage(pFinalCompositeDIB,0, 0);
				graphics.DrawImage(pFinalCompositeDIB, Destination, 0, 0, pFinalCompositeDIB->GetWidth(), pFinalCompositeDIB->GetHeight(), UnitPixel);
				if(bDrawBorders){
					//User has specified that we want to see the borders.
					 Pen borderPen(Color(255, 255, 255), 2);
					// graphics.DrawLine(&borderPen, 0,0, 400, 500);
				}
			}
			//graphics.DrawImage(ImageList[0], 0, 0);
			EndPaint(hwnd, &ps);
			break;
		}
		case WM_SIZE:{
			RECT clientRect;
			GetClientRect(hwnd, &clientRect);
			SetWindowPos(hStatusBar, NULL, 0, clientRect.bottom - StatusBarHeight, clientRect.right, clientRect.bottom, SWP_NOZORDER);
			break;
		}
		default:
			return DefWindowProc(hwnd, msg, wParam, lParam); //Defer processing since we ignored this event.
	}
	return 0;
}
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, 
    LPSTR lpCmdLine, int nCmdShow)
{
	GdiplusStartupInput gdiplusStartupInput;
	ULONG_PTR           gdiplusToken;
	// Initialize GDI+ and common controls
	GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
	InitCommonControls();

 	ZeroMemory( &ofn , sizeof( ofn));
	ofn.lStructSize = sizeof (OPENFILENAME);
	ofn.hwndOwner = NULL  ;
	ofn.hInstance = hInstance;
	ofn.lpstrFile = cFileName ;
	ofn.nMaxFile = FoofMAXFILEBUFFER;
	ofn.lpstrFilter = "PNG Image\0*.png\0JPG Image\0*.jpg\0\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = cFileTitle;
	ofn.nMaxFileTitle = MAX_PATH ;
	ofn.lpstrInitialDir= NULL ;

	ofn.Flags = OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT |OFN_EXPLORER ;
 
	
	//Limit to only PNG FORMAT...
	ZeroMemory( &sfn , sizeof( sfn));
	sfn.lStructSize = sizeof (OPENFILENAME);
	sfn.lpstrFile = sfPath ;
	sfn.nMaxFile = MAX_PATH;
	sfn.lpstrFilter = "PNG Image\0*.png\0\0";
	sfn.nFilterIndex = 1;
	sfn.hInstance = hInstance;
	//=============================================
	//Set up  Window
	WNDCLASSEX wc;
	HWND hwnd;
	MSG Msg;

	ZeroMemory(&wc, sizeof(WNDCLASSEX));
	wc.style         = CS_HREDRAW | CS_VREDRAW;
	wc.cbSize        = sizeof(WNDCLASSEX);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    //wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.lpszClassName = "FWindow";
    wc.hIconSm       = LoadIcon(NULL, IDI_APPLICATION);
	wc.lpszMenuName  = MAKEINTRESOURCE(IDR_MENU1);

    if(!RegisterClassEx(&wc))
    {
        MessageBox(NULL, "Couldn't create preview window! Press OK to exit.", "Error!",
            MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }
	hwnd = CreateWindowEx(WS_EX_CLIENTEDGE,"FWindow","Foof Texture Atlas Packer", WS_OVERLAPPEDWINDOW |  WS_HSCROLL | WS_VSCROLL,
		CW_USEDEFAULT, CW_USEDEFAULT, 300, 300, NULL, NULL, hInstance, NULL);
	

	hStatusBar = CreateWindowEx(0, STATUSCLASSNAME, NULL,
        WS_CHILD | WS_VISIBLE , 0, 0, 300, 20,
        hwnd, NULL, hInstance, NULL);
    if(!hwnd || !hStatusBar)
    {
        MessageBox(NULL, "Couldn't Create Window! FFFFFFFFUUUUUUUUU", "Error!", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    ShowWindow(hStatusBar, nCmdShow);
	SendMessage(hStatusBar, SB_SETTEXT, 0, (LPARAM)"Select File -> Open to load images to pack.");
    UpdateWindow(hStatusBar);
	{
		RECT sRect;
		GetClientRect(hStatusBar, &sRect);
		StatusBarHeight = sRect.bottom;
	}
	//And here it goes:
	while(GetMessage(&Msg, NULL, 0, 0) > 0) // if NOT WM_QUIT (0) or errors (-1)
    {
        TranslateMessage(&Msg);
        DispatchMessage(&Msg);
    }
	GdiplusShutdown(gdiplusToken);
    return 0;
}
