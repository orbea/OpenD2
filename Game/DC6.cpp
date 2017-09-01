#include "Diablo2.hpp"
#include <memory>

#define DC6_HEADER_VERSION	6

/*
 *	Decode a DC6 frame in place
 *	@author	Necrolis/eezstreet
 *	TODO: optimize this a lot
 */
static void DC6_DecodeFrame(BYTE* pPixels, BYTE* pOutPixels, DC6Frame* pFrame)
{
	DWORD x = 0, y;

	if (pFrame->fh.dwFlip > 0)
	{
		y = 0;
	}
	else
	{
		y = pFrame->fh.dwHeight;
	}

	for (size_t i = 0; i < pFrame->fh.dwLength; i++)
	{
		BYTE pixel = pPixels[i];
		if (pixel == 0x80)
		{	// pixel row termination
			x = 0;
			if (pFrame->fh.dwFlip > 0)
			{
				y++;
			}
			else
			{
				y--;
			}
		}
		else if (pixel & 0x80)
		{	// Write PIXEL & 0x7F transparent pixels
			x += pixel & 0x7F;
		}
		else
		{
			while (pixel--)
			{
				pOutPixels[(y * pFrame->fh.dwWidth) + x++] = pPixels[++i];
			}
		}
	}
}

/*
 *	Loads a DC6 from an MPQ
 *	@author	eezstreet
 */
#define DECODE_BUFFER_SIZE	2048 * 2048
static BYTE gpDecodeBuffer[DECODE_BUFFER_SIZE];
static BYTE gpReadBuffer[DECODE_BUFFER_SIZE];

void DC6_LoadImage(char* szPath, DC6Image* pImage)
{
	memset(pImage, 0, sizeof(DC6Image));

	pImage->f = FSMPQ_FindFile(szPath, nullptr, (D2MPQArchive**)&pImage->mpq);
	Log_WarnAssert(pImage->f != (fs_handle)-1);

	// Now comes the fun part: reading and decoding the actual thing
	size_t dwFileSize = MPQ_FileSize((D2MPQArchive*)pImage->mpq, pImage->f);

	BYTE* pByteReadHead = gpReadBuffer;

	Log_WarnAssert(MPQ_FileSize((D2MPQArchive*)pImage->mpq, pImage->f) < DECODE_BUFFER_SIZE);
	MPQ_ReadFile((D2MPQArchive*)pImage->mpq, pImage->f, gpReadBuffer, dwFileSize);

	memcpy(&pImage->header, pByteReadHead, sizeof(pImage->header));
	pByteReadHead += sizeof(pImage->header);

	// Validate the header
	Log_WarnAssert(pImage->header.dwVersion == DC6_HEADER_VERSION);

	// Table of pointers
	DWORD* pFramePointers = (DWORD*)pByteReadHead;
	DWORD dwNumFrames = pImage->header.dwDirections * pImage->header.dwFrames;
	pByteReadHead += sizeof(DWORD) * dwNumFrames;

	pImage->pFrames = (DC6Frame*)malloc(sizeof(DC6Frame) * dwNumFrames);
	Log_ErrorAssert(pImage->pFrames != nullptr);

	// Copy frame headers. Compute the total number of pixels that we need to allocate in the process.
	DWORD dwTotalPixels = 0;
	int i, j;
	DWORD dwFramePos;
	DWORD dwOffset = 0;

	pImage->pPixels = (BYTE*)malloc(dwTotalPixels);
	Log_ErrorAssert(pImage->pPixels != nullptr);

	for (i = 0; i < pImage->header.dwDirections; i++)
	{
		for (j = 0; j < pImage->header.dwFrames; j++)
		{
			dwFramePos = (i * pImage->header.dwFrames) + j;
			memcpy(&pImage->pFrames[dwFramePos].fh, gpReadBuffer + pFramePointers[dwFramePos], 
				sizeof(DC6Frame::DC6FrameHeader));
			pImage->pFrames[dwFramePos].fh.dwNextBlock = dwTotalPixels * sizeof(BYTE);
			pImage->pFrames[dwFramePos].pFramePixels = pImage->pPixels + dwTotalPixels;
			dwTotalPixels += pImage->pFrames[dwFramePos].fh.dwWidth * pImage->pFrames[dwFramePos].fh.dwHeight;
		}
	}

	// Decode all of the blocks
	for (i = 0; i < pImage->header.dwDirections; i++)
	{
		for (j = 0; j < pImage->header.dwFrames; j++)
		{
			dwFramePos = (i * pImage->header.dwFrames) + j;
			DC6Frame* pFrame = &pImage->pFrames[dwFramePos];
			pByteReadHead = gpReadBuffer + pFramePointers[dwFramePos] + sizeof(DC6Frame);
			DC6_DecodeFrame(pByteReadHead, gpDecodeBuffer + dwOffset, pFrame);
			dwOffset += pFrame->fh.dwWidth * pFrame->fh.dwHeight;
		}
	}

	memcpy(pImage->pPixels, gpDecodeBuffer, dwTotalPixels);
}

/*
 *	Frees a DC6 resource
 *	@author	eezstreet
 */
void DC6_UnloadImage(DC6Image* pImage)
{
	free(pImage->pPixels);
	free(pImage->pFrames);
}

/*
 *	Get pointer to pixels at specific frame, and how many pixels there are
 */
BYTE* DC6_GetPixelsAtFrame(DC6Image* pImage, int nDirection, int nFrame, size_t* pNumPixels)
{
	if (pImage == nullptr)
	{
		return nullptr;
	}

	if (nDirection >= pImage->header.dwDirections)
	{
		return nullptr;
	}

	if (nFrame >= pImage->header.dwFrames)
	{
		return nullptr;
	}

	DC6Frame* pFrame = &pImage->pFrames[(nDirection * pImage->header.dwFrames) + nFrame];

	if (pNumPixels != nullptr)
	{
		*pNumPixels = pFrame->fh.dwWidth * pFrame->fh.dwHeight;
	}

	return pFrame->pFramePixels;
}

/*
 *	Retrieve some data about a DC6 frame
 */
void DC6_PollFrame(DC6Image* pImage, DWORD nDirection, DWORD nFrame, 
	DWORD* dwWidth, DWORD* dwHeight, DWORD* dwOffsetX, DWORD* dwOffsetY)
{
	if (pImage == nullptr)
	{
		return;
	}

	if (nDirection >= pImage->header.dwDirections)
	{
		return;
	}

	if (nFrame >= pImage->header.dwFrames)
	{
		return;
	}

	DC6Frame* pFrame = &pImage->pFrames[(nDirection * pImage->header.dwFrames) + nFrame];
	if (dwWidth != nullptr)
	{
		*dwWidth = pFrame->fh.dwWidth;
	}

	if (dwHeight != nullptr)
	{
		*dwHeight = pFrame->fh.dwHeight;
	}

	if (dwOffsetX != nullptr)
	{
		*dwOffsetX = pFrame->fh.dwOffsetX;
	}

	if (dwOffsetY != nullptr)
	{
		*dwOffsetY = pFrame->fh.dwOffsetY;
	}
}