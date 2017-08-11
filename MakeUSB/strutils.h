#pragma once

//
// Get ascii character
//
char getasciichar( );

//
// Get unicode character
//
unsigned short getunicodechar( );

//
// Our own gets function so we can use spaces in the service name
//
unsigned int getstr( char* str );

//
// Widechar implementation of getstr
//
unsigned int getwstr( unsigned short* wstr );