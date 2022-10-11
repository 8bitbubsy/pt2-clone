#include <wchar.h>
#include "pt2_unicode.h"

// this is probably broken, but it "works" for now
uint32_t unicharToAnsi(char *dstBuffer, const UNICHAR *inputString, uint32_t maxDstLen)
{
	if (inputString == NULL || dstBuffer == NULL)
		return 0;

	uint32_t i = 0;
	while (i < maxDstLen && inputString[i] != '\0')
	{
		UNICHAR ch = inputString[i];
#ifdef _WIN32
		if (ch >= 256)
#else
		if ((uint8_t)ch > 127)
#endif
		{
			*dstBuffer++ = '?';

#ifdef _WIN32
			if ((ch & 0xFF00) == 0xD800) // lead surrogate, skip the next widechar (trail)
				i++;
#endif
		}
		else
		{
			*dstBuffer = (char)ch;
			if ((signed)*dstBuffer < ' ')
				*dstBuffer = '?';

			dstBuffer++;
		}

		i++;
	}

	if (maxDstLen > 1)
		*dstBuffer = '\0';

	return i;
}
