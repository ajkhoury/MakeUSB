#include "strutils.h"

#include <stdio.h>

char getasciichar( )
{
	return (char)_fgetchar( );
}

unsigned short getunicodechar( )
{
	return (unsigned short)_fgetwchar( );
}

unsigned int getstr( char* str )
{
	char ch = _fgetchar( );
	unsigned int size = 0;
	if (ch != 0xA)
	{
		str[size] = ch;
		size += 1;
	}

	ch = 0;
	while (1)
	{
		ch = _fgetchar( );
		if (ch != 0xA)
		{
			str[size] = ch;
			size += 1;
		}
		else
			break;
	}
	// Null terminate
	str[size] = '\0';
	return size;
}

unsigned int getwstr( unsigned short* wstr )
{
	unsigned short ch = _fgetwchar( );
	unsigned int size = 0;

	if (ch != 0xA)
	{
		wstr[size] = ch;
		size += 1;
	}

	ch = 0;
	while (1)
	{
		ch = _fgetwchar( );

		if (ch != 0xA)
		{
			wstr[size] = ch;
			size += 1;
		}
		else
			break;
	}

	// Null terminate
	wstr[size] = L'\0';
	
	return size;
}