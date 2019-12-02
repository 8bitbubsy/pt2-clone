#include <stdint.h>
#include <wchar.h>
#include "unicode.h"

// this is probably broken, but it "works" for now
uint32_t unicharToAnsi(char *dstBuffer, const UNICHAR *inputString, uint32_t maxDstLen)
{
	uint32_t i;
	UNICHAR wideChar;

	if (inputString == NULL || dstBuffer == NULL)
		return 0;

	i = 0;
	while (i < maxDstLen && inputString[i] != '\0')
	{
		wideChar = inputString[i];
#ifdef _WIN32
		if (wideChar >= 256)
#else
		if (wideChar < 0)
#endif
		{
			*dstBuffer++ = '?';

#ifdef _WIN32
			if ((wideChar & 0xFF00) == 0xD800) // lead surrogate, skip the next widechar (trail)
				i++;
#endif
		}
		else
		{
			*dstBuffer++ = (char)wideChar;
		}

		i++;
	}

	if (maxDstLen > 1)
		*dstBuffer = '\0';

	return i;
}
