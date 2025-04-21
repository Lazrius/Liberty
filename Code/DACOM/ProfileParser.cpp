#include "DACOM.h"
#include "ProfileParser.h"

#include <string.h>

#define compare _stricmp
#define compare_len _strnicmp

IComponentFactory* CreateProfileParserFactory(void)
{
	return new DAComponentFactory2<DAComponentAggregate<ProfileParser>, PROFPARSEDESC>(IID_IProfileParser);
}

void ProfileParser::free(void)
{
	::free(fileBuffer);
	fileBuffer = 0;
}

GENRESULT ProfileParser::Initialize(const C8* fileName, ACCESS access)
{
	HANDLE hFile = INVALID_HANDLE_VALUE;
	GENRESULT result = GR_FILE_ERROR;
	U32 dwAccess = GENERIC_READ;
	U32 dwOpen = OPEN_EXISTING;
	bool bBufferType = (S32(access) < 0);

	if (access == WRITE_ACCESS)
	{
		dwAccess = GENERIC_READ | GENERIC_WRITE;
		dwOpen = OPEN_ALWAYS;
	}

	if (bBufferType || (hFile = CreateFileA(fileName, dwAccess, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, dwOpen, FILE_ATTRIBUTE_NORMAL, 0)) != INVALID_HANDLE_VALUE)
	{
		U32 fileSize;
		C8* oldBuffer = fileBuffer;
		DWORD bytesRead;

		if (bBufferType == false)
			fileSize = ::GetFileSize(hFile, 0);
		else
		{
			if (S32(access) == -1)
				fileSize = strlen(fileName);
			else
				fileSize = U32(access) & ~0x80000000;
		}

		if ((fileBuffer = (C8*)calloc(fileSize + 8, 1)) != 0)
		{
			if (bBufferType)
				memcpy(fileBuffer + 1, fileName, fileSize);
			else
				::ReadFile(hFile, fileBuffer + 1, fileSize, &bytesRead, 0);
			::free(oldBuffer);
			bufferSize = fileSize + 1;

			oldBuffer = fileBuffer + 1;
			while (*oldBuffer == ' ')
				oldBuffer++;			// skip white space
			do
			{
				if (*oldBuffer == '[')      // next section
				{
					char* tmp, * tmp2;

					if ((tmp = strchr(oldBuffer, ']')) != 0)
					{
						tmp2 = strchr(oldBuffer, '\n');			// if no return between [ ... ]

						if (tmp2 == 0 || tmp2 > tmp)
						{
							// assume that previous character is not important,
							// put double 00 for '[', single 0 for ']'

							oldBuffer[-1] = 0;
							*oldBuffer = 0;		// mark it
							*tmp = 0;
							oldBuffer = tmp + 1;
						}
					}
				}
			} while ((oldBuffer = (C8*)getLine(oldBuffer, 1)) != 0);

			result = GR_OK;
		}
		else
		{
			fileBuffer = oldBuffer;
			result = GR_OUT_OF_MEMORY;
		}

		if (bBufferType == false)
			::CloseHandle(hFile);
	}

	return result;
}

const char* ProfileParser::getLine(const char* buffer, int line)
{
	// find 0D0A, then skip white space

	const char* tmp;

	while (line > 0)
	{
		if ((tmp = strchr(buffer, '\n')) == 0)
		{
			int len = strlen(buffer);

			buffer += len + 1;
			if (buffer[0] == 0)
			{
				buffer = 0;
				goto Done;
			}
		}
		else
		{
			buffer = tmp + 1;
			switch (*buffer)
			{
			case 0:
				buffer = 0;
				goto Done;
			case '\r':
				buffer++;
				break;
			}

			while (*buffer == ' ')
				buffer++;
			if (*buffer == 0)
			{
				buffer = 0;
				goto Done;
			}

			line--;
		}

	}

Done:
	return buffer;
}

const char* ProfileParser::getSection(const char* buffer, int count)
{
	int len;

	while (count > 0)
	{
		len = strlen(buffer);
		buffer += len;
		if (buffer[1] != 0)		// found a ']'
		{
			buffer++;
		}
		else
			if (buffer[2] != 0)		// found a '['
			{
				count--;
				buffer += 2;
			}
			else
				return 0;			// end of the line
	}

	return buffer;
}

BOOL32 ProfileParser::EnumerateSections(ENUM_PROC proc, void* context)
{
	BOOL32 result = 0;
	const char* buffer = fileBuffer;

	if (proc == 0 || fileBuffer == 0)
		goto Done;

	result++;
	buffer = getSection(buffer, 1);
	while (result && buffer)
	{
		result = proc(this, buffer, context);
		buffer = getSection(buffer, 1);
	}

Done:
	return result;
}

HANDLE ProfileParser::CreateSection(const C8* sectionName, CREATE_MODE mode)
{
	HANDLE result = 0;

	if (mode != PP_OPENEXISTING)
		goto Done;						// nothing else is supported yet
	if (fileBuffer == 0)
		goto Done;						// not initialized?

	//
	// is requested sectionName already found within buffer?
	//

	if (sectionName > fileBuffer && sectionName < fileBuffer + bufferSize)
	{
		result = (HANDLE)(sectionName - fileBuffer);
	}
	else
	{
		//
		// else we must search for the section name 
		//
		const char* buffer = fileBuffer;

		buffer = getSection(buffer, 1);
		while (buffer)
		{
			if (compare(buffer, sectionName) == 0)
			{
				result = (HANDLE)(buffer - fileBuffer);
				goto Done;
			}

			buffer = getSection(buffer, 1);
		}
	}

Done:
	return result;
}

BOOL32 ProfileParser::CloseSection(HANDLE hSection)
{
	return 1;
}

U32 ProfileParser::ReadProfileLine(HANDLE hSection, U32 lineNumber, C8* buffer, U32 bufferSize)
{
	U32 result = 0;
	const char* tmp, * tmp2;

	if ((tmp = getLine(hSection, lineNumber + 1)) != 0)		// line 0 is really the section name
	{
		if ((tmp2 = strchr(tmp, '\n')) != 0)
		{
			result = tmp2 - tmp;
			if (result && tmp[result - 1] == '\r')
				result--;	// get rid of extra char
		}
		else
			result = strlen(tmp);
		if (bufferSize < result + 1)
			result = bufferSize - 1;

		if (buffer)
		{
			if (result == 0 && bufferSize > 1)
			{
				*buffer = ' ';
				result++;
			}
			else
				memcpy(buffer, tmp, result);
			buffer[result] = 0;
		}
		else
			if (result == 0)
				result++;
	}

	return result;
}

U32 ProfileParser::ReadKeyValue(HANDLE hSection, const C8* keyName, C8* buffer, U32 outBufferSize)
{
	U32 result = 0;
	const char* tmp, * tmp2;
	int cmplen;

	cmplen = strlen(keyName);
	tmp = fileBuffer + ((U32)hSection);

	while ((tmp = getLine(tmp, 1)) != 0)
	{
		// is there enough room in the buffer for a match to be possible?
		if (U32(tmp) - U32(fileBuffer) + cmplen <= bufferSize)
		{
			// make sure last character of tmp[] is not a letter,number,or underscore
			if (__iscsym(tmp[cmplen]) == 0)
			{
				if (compare_len(tmp, keyName, cmplen) == 0)
				{
					if ((tmp = strchr(tmp, '=')) != 0)
					{
						tmp++;
						while (*tmp == ' ')
							tmp++;

						if ((tmp2 = strchr(tmp, '\n')) != 0)
						{
							result = tmp2 - tmp;
							if (result && tmp[result - 1] == '\r')
								result--;	// get rid of extra char
						}
						else
							result = strlen(tmp);
						if (outBufferSize < result + 1)
							result = outBufferSize - 1;

						if (buffer)
						{
							if (result == 0 && outBufferSize > 1)
							{
								*buffer = ' ';
								result++;
							}
							else
								memcpy(buffer, tmp, result);
							buffer[result] = 0;
						}
						else
							if (result == 0)
								result++;
						break;
					}
				}
			}
		}
	}

	return result;
}
