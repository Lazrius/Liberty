#include "DACOM.h"
#include "DACOManager.h"
#include <tchar.h>

#include <filesystem>

//--------------------------------------------------------------------------//
//---------------------------DACOManager Methods----------------------------//
//--------------------------------------------------------------------------//
IComponentFactory* CreateProfileParserFactory(void);  // extern to profile parser
IComponentFactory* CreateProfileParserFactory2(void);  // extern to profile parser

//--------------------------------------------------------------------------//
//
DACOManager::DACOManager(void)
{
	reference_cnt = 1;
	initialized = 0;
	pCurrentLibrary = 0;
	innerParser = NULL;

	{
		IComponentFactory* factory = CreateProfileParserFactory();
		RegisterComponent(factory, IID_IProfileParser);
		factory->Release();
	}

	{
		IComponentFactory* factory = CreateProfileParserFactory2();
		RegisterComponent(factory, IID_IProfileParser2);

		PROFPARSEDESC2 pdesc;
		pdesc.inner = &innerParser;
		pdesc.outer = this;
		factory->CreateInstance(&pdesc, (void**)&parser);

		factory->Release();
	}
}


//--------------------------------------------------------------------------//
//
GENRESULT DACOManager::QueryInterface(const C8* interface_name, void** instance)
{
	GENRESULT result = GR_INTERFACE_UNSUPPORTED;

	ASSERT(instance);
	*instance = 0;

	if (strcmp(interface_name, IID_ICOManager) == 0)
	{
		AddRef();
		*instance = static_cast<ICOManager*>(this);
		result = GR_OK;
	}
	else if (strcmp(interface_name, IID_ICOManagerLiberty) == 0)
	{
		*instance = static_cast<ICOManagerLiberty*>(this);
		result = GR_OK;
	}
	else if (innerParser)
	{
		result = innerParser->QueryInterface(interface_name, instance);
	}

	return result;
}
//--------------------------------------------------------------------------//
//
U32 DACOManager::AddRef(void)
{
	return 1;
}

//--------------------------------------------------------------------------//
//
U32 DACOManager::Release(void)
{
	return 1;
}
//--------------------------------------------------------------------------//
//
GENRESULT DACOManager::CreateInstance(DACOMDESC* descriptor, //)
	void** instance)
{
	GENRESULT result = GR_INTERFACE_UNSUPPORTED;

	ASSERT(instance);

	*instance = 0;

	if ((descriptor == NULL) ||
		(descriptor->interface_name == NULL) ||
		(!strcmp(descriptor->interface_name, IID_ICOManager)))
	{
		//
		// If no descriptor provided, or if component manager instance 
		// explicitly requested, return component manager object pointer
		//

		result = GR_OK;
		*instance = this;
		AddRef();
	}
	else
	{
		//
		// Otherwise, pass client request to all registered interface providers 
		// until creation request is accepted
		//

		REGISTERED_OBJECT* obj = NULL;

		while ((obj = object_list.next(obj)) != NULL)
		{
			if (!strcmp(obj->interface_name, descriptor->interface_name))
			{
				result = obj->component->CreateInstance(descriptor,
					instance);
				if (result == GR_OK)
					break;
			}
		}
	}

	if (FAILED(result))
	{
		debug_point;
	}
	return result;
}

GENRESULT DACOManager::CreateInstanceEx(DACOMDESC* descriptor, void** instance, U32 low_priority, U32 high_priority)
{
	GENRESULT result = GR_INTERFACE_UNSUPPORTED;

	ASSERT(instance);
	*instance = 0;

	if ((descriptor == NULL) || (descriptor->interface_name == NULL) || (!strcmp(descriptor->interface_name, IID_ICOManager)))
	{
		// If no descriptor provided, or if component manager instance 
		// explicitly requested, return component manager object pointer

		*instance = this;
		AddRef();
		result = GR_OK;
	}
	else
	{
		// Otherwise, pass client request to all registered interface providers 
		// until creation request is accepted

		REGISTERED_OBJECT* obj = nullptr;
		while ((obj = object_list.next(obj)))
		{
			if (strcmp(obj->interface_name, descriptor->interface_name) == 0)
			{
				if (obj->priority >= low_priority && obj->priority <= high_priority)
				{
					if (SUCCEEDED(result = obj->component->CreateInstance(descriptor, instance)))
					{
						break;
					}
				}
			}
		}
	}

	return result;
}

//--------------------------------------------------------------------------//
//
GENRESULT DACOManager::RegisterComponent(IComponentFactory* component, //)
	const C8* interface_name,
	U32           priority)
{
	//
	// Validate registration parameters
	//

	if ((component == NULL) ||
		(interface_name == NULL))
	{
		return GR_INVALID_PARMS;
	}
	
	//
	// Allocate new REGISTERED_OBJECT entry, linking it into the list 
	// just before the first entry with lower or equal priority (if any)
	//
	// Higher-priority implementations appear earlier in the list, 
	// and will be visited first by CreateInstance()
	//

	REGISTERED_OBJECT* obj = NULL;

	while ((obj = object_list.next(obj)) != NULL)
	{
		if (priority >= obj->priority)
		{
			break;
		}
	}

	obj = object_list.alloc(obj);

	obj->priority = priority;
	obj->component = component;
	obj->interface_name = interface_name;
	obj->library = pCurrentLibrary;

	//
	// Add a reference to account for the stored obj->component pointer 
	//

	component->AddRef();

	//
	// Increment registration count for this DLL to signal valid component
	// registration, and return success
	//

	++registration_cnt;

	const char* priority_string = "CUSTOM";
	switch (priority)
	{
	case DACOM_HIGH_PRIORITY:
		priority_string = "HIGH";
		break;
	case DACOM_NORMAL_PRIORITY:
		priority_string = "NORMAL";
		break;
	case DACOM_LOW_PRIORITY:
		priority_string = "LOW";
		break;
	}

	GENERAL_TRACE_1(TEMPSTR("DACOM: RegisterComponent: Registered component '%s' priority:%s [0x%X].\n", interface_name, priority_string, priority));

	return GR_OK;
}

//--------------------------------------------------------------------------//
//
GENRESULT DACOManager::UnregisterComponent(IComponentFactory* component, //)
	const C8* interface_name)
{
	//
	// Search list backwards to release components in inverse order of
	// registration
	//

	REGISTERED_OBJECT* obj = NULL;

	while ((obj = object_list.prev(obj)) != NULL)
	{
		//
		// Component instance must match, as well as interface name 
		// (if supplied)
		//

		if ((obj->component == component)
			&&
			((interface_name == NULL) ||
				(!strcmp(interface_name, obj->interface_name))))
		{
			//
			// Release reference before invalidating object pointer
			//

			component->Release();

			object_list.free(obj);
			return GR_OK;
		}
	}

	return GR_INVALID_PARMS;
}

//--------------------------------------------------------------------------//
//
GENRESULT DACOManager::EnumerateComponents(const C8* interface_name, //)
	DACOMENUMCALLBACK callback,
	void* context)
{
	//
	// Enumerate call requires valid callback
	//

	if (callback == NULL)
	{
		return GR_INVALID_PARMS;
	}

	//
	// Walk object list, calling client-supplied function for all components
	// that are providers of the specified interface
	//

	REGISTERED_OBJECT* obj = NULL;

	while ((obj = object_list.next(obj)) != NULL)
	{
		//
		// Component instance must match, as well as interface name 
		// (if supplied)
		//

		if ((interface_name == NULL) || (!strcmp(interface_name,
			obj->interface_name)))
		{
			if (!callback(obj->component,
				obj->interface_name,
				obj->priority,
				context))
			{
				// 
				// Stop enumerating components if callback returns FALSE
				// 

				break;
			}
		}
	}

	return GR_OK;
}

//--------------------------------------------------------------------------//
//
GENRESULT DACOManager::AddLibrary(const C8* DLL_filename)
{
	SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS);

	C8        buffer[MAX_PATH + 8];
	C8* ptr;
	S32       len;

	//
	// Reject invalid library name
	//

	if (DLL_filename == NULL)
	{
		return GR_INVALID_PARMS;
	}

	//
	// Remove previous instances loaded with same name 
	//

	//if (initialized == 0)
	//	RemoveLibrary(DLL_filename);

	//
	// Allocate new entry for library
	// 

	ADDED_LIBRARY* library = library_list.alloc();

	if (library == NULL)
	{
		return GR_OUT_OF_SPACE;
	}

	//
	// Attempt to load library
	//
	// If load fails, or if no components are registered by this library,
	// destroy library entry and return failure
	//

	registration_cnt = 0;
	pCurrentLibrary = library;

	if ((library->instance = LoadLibraryExA(DLL_filename, nullptr, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS)) == nullptr) {
		GENERAL_NOTICE(TEMPSTR("DACOM: AddLibrary: Unable to load DLL '%s', ignoring...\n", DLL_filename));
	}
	else {
		U32 m, n, b;
		DACOM_GetDllVersion(DLL_filename, &m, &n, &b);
		const char* binary_type = "Vanilla";
		if (GetProcAddress(library->instance, "Liberty"))
		{
			binary_type = "Liberty";
		}
		GENERAL_NOTICE(TEMPSTR("DACOM: AddLibrary: DLL '%s' [%d.%d.%d] Type=%s\n", DLL_filename, m, n, b, binary_type));
	}

	pCurrentLibrary = 0;

	if (library->instance == nullptr)
	{
		library_list.free(library);
		return GR_GENERIC;
	}

	//
	// Otherwise, determine base filename and copy it to library entry
	//

	strncpy_s(buffer, DLL_filename, MAX_PATH);
	buffer[MAX_PATH] = 0;
	GetShortPathNameA(buffer, buffer, MAX_PATH);
	_strupr_s(buffer);

	//
	// Truncate extension (".DLL"), and remove path prefix ("C:\FOO\");
	//

	if ((ptr = strrchr(buffer, '\\')) == NULL)
	{
		ptr = buffer;
	}

	if ((ptr = strrchr(ptr, '.')) != NULL)
	{
		*ptr = 0;
	}

	if ((ptr = strrchr(buffer, '\\')) != NULL)
	{
		ptr++;
	}
	else
	{
		ptr = buffer;
	}

	//
	// Copy up to 8 characters to DLL descriptor
	// 

	len = strlen(ptr);

	ptr = ptr + (len - __min(8, len));

	memcpy(library->base_name,
		ptr,
		sizeof(library->base_name));

	//
	// Return success
	//

	return GR_OK;
}

//--------------------------------------------------------------------------//
//
GENRESULT DACOManager::RemoveLibrary(const C8* DLL_filename)
{
	C8        buffer[MAX_PATH + 8];
	C8* ptr;
	S32       len;

	//
	// Reject invalid library name
	//

	if (DLL_filename == NULL)
	{
		return GR_INVALID_PARMS;
	}

	//
	// Determine base library filename
	//

	strncpy_s(buffer, DLL_filename, MAX_PATH);
	buffer[MAX_PATH] = 0;
	GetShortPathNameA(buffer, buffer, MAX_PATH);
	_strupr_s(buffer);

	//
	// Truncate extension (".DLL"), and remove path prefix ("C:\FOO\");
	//

	if ((ptr = strrchr(buffer, '\\')) == NULL)
	{
		ptr = buffer;
	}

	if ((ptr = strrchr(ptr, '.')) != NULL)
	{
		*ptr = 0;
	}

	if ((ptr = strrchr(buffer, '\\')) != NULL)
	{
		ptr++;
	}
	else
	{
		ptr = buffer;
	}

	//
	// Copy up to 8 characters to DLL descriptor
	// 

	len = strlen(ptr);

	ptr = ptr + (len - __min(8, len));

	//
	// Find library's list entry and destroy it
	//

	ADDED_LIBRARY* lib = NULL;

	while ((lib = library_list.next(lib)) != NULL)
	{
		if (!strcmp(ptr, lib->base_name))
		{
			//
			// Library entry found; unlink related components, the unload the library module
			//
			UnregisterRelatedComponents(lib);

			FreeLibrary(lib->instance);
			library_list.free(lib);

			return GR_OK;
		}
	}

	//
	// Matching library not found -- return failure
	//

	return GR_GENERIC;
}
//--------------------------------------------------------------------------//
//
GENRESULT DACOManager::ShutDown(void)
{
	//
	// Clean up any outstanding
	// object- and library-list entries in their inverse order of
	// creation
	//

	REGISTERED_OBJECT* obj;

	while ((obj = object_list.last()) != NULL)
	{
		obj->component->Release();
		object_list.free(obj);
	}

	//
	// Free all libraries in inverse order of loading
	//

	ADDED_LIBRARY* lib;

	while ((lib = library_list.last()) != NULL)
	{
		FreeLibrary(lib->instance);
		library_list.free(lib);
	}

	initialized = false;

	return GR_OK;
}

//--------------------------------------------------------------------------//
//
GENRESULT DACOManager::SetINIConfig(const C8* info, U32 flags)
{
	IProfileParser::ACCESS access = IProfileParser::READ_ACCESS;

	U32 maj, min, build;
	DACOM_GetVersion(&maj, &min, &build);

	if ((flags & (DACOM_INI_WRITABLE | DACOM_INI_STRING)) == (DACOM_INI_WRITABLE | DACOM_INI_STRING))
		return GR_INVALID_PARMS;

	if (flags & DACOM_INI_STRING)
		access = IProfileParser::ACCESS(-1);

	if (flags & DACOM_INI_WRITABLE)
		access = IProfileParser::WRITE_ACCESS;

	GENRESULT result = parser->Initialize(info, access);

	if (result == GR_OK)
		initialize();
	return result;
}
//--------------------------------------------------------------------------//
//  path is of form "path\\"
//
BOOL32 DACOManager::LoadAllFromDirectory(const C8* pathName, const C8* searchName)
{
	HANDLE          search_handle;
	WIN32_FIND_DATAA found;
	C8              name_buffer[MAX_PATH + 4];
	C8              filePath[MAX_PATH + 4];
	BOOL			result = 0;
	const C8* tmp;

	if ((tmp = strrchr(searchName, '\\')) != 0)
		tmp++;
	else
		if ((tmp = strrchr(searchName, ':')) != 0)
			tmp++;

	if (tmp)
	{
		int len = tmp - searchName;
		memcpy(filePath, searchName, len);
		filePath[len] = 0;

		strcpy_s(name_buffer, searchName);
	}
	else
	{
		strcpy_s(name_buffer, pathName);
		strcat_s(name_buffer, searchName);
		strcpy_s(filePath, pathName);
	}

	search_handle = FindFirstFileA(name_buffer, &found);

	if (search_handle != INVALID_HANDLE_VALUE)
	{
		result = 1;
		do
		{

			strcpy_s(name_buffer, filePath);
			strcat_s(name_buffer, found.cFileName);

			AddLibrary(name_buffer);
		} while (FindNextFileA(search_handle, &found));

		FindClose(search_handle);
	}

	return result;
}
//--------------------------------------------------------------------------//
//
void DACOManager::LoadAllFromPath(const C8* searchName)
{
	C8 app_path[MAX_PATH + 4];
	C8 load_path[MAX_PATH + 4];
	C8 search_path[4096];      // jmiles: Env vars can be REAL long in Win95
	C8* endptr, * startptr;

	GetModuleFileNameA(NULL, app_path, sizeof(app_path));
	if ((endptr = strrchr(app_path, '\\')) != 0)				// just the path, please
		endptr[1] = 0;
	if (GetEnvironmentVariableA(/*TEXT*/("DACOM"), search_path, sizeof(search_path)) == 0)
		search_path[0] = 0;
	startptr = search_path;

	do
	{
		if ((endptr = strchr(startptr, ';')) != 0)			// get end of first path
			*endptr++ = 0;

		if (GetAbsolutePath(load_path, app_path, startptr, sizeof(load_path)))
			LoadAllFromDirectory(load_path, searchName);

	} while ((startptr = endptr) != 0);

	LoadAllFromDirectory(app_path, searchName);
}

static char* parseFilename(char* buffer)
{
	char* ptr, * ptr2;

	// if file

	if ((ptr = strchr(buffer, '\"')) != 0)
	{
		if ((ptr2 = strchr(ptr + 1, '\"')) != 0)
		{
			*ptr2 = 0;
			return ptr + 1;
		}
	}

	if ((ptr = strchr(buffer, ' ')) != 0)
		*ptr = 0;
	if ((ptr = strchr(buffer, '\t')) != 0)
		*ptr = 0;
	if ((ptr = strchr(buffer, '\r')) != 0)
		*ptr = 0;

	return buffer;
}

BOOL32 DACOManager::initialize(void)
{
	// scan the load path for DLLs

	if (initialized == 0)
	{
		HANDLE hSection;

		if ((hSection = parser->CreateSection("DACOM")) != nullptr)
		{
			GENERAL_TRACE_1("DACOM: initialize: Loading DACOM settings via [DACOM] section.\n");
			unsigned line = 0;
			char buffer[MAX_PATH];
			while (parser->ReadProfileLine(hSection, line++, buffer, sizeof(buffer)) != 0)
			{
				char* ptr;

				ptr = buffer;
				while (*ptr == ' ')
				{
					ptr++;
				}

				constexpr auto iniKey = "dll_search_path";
				if (strncmp(ptr, iniKey, strlen(iniKey)) != 0)
				{
					continue;
				}

				ptr += strlen(iniKey);
				while (*ptr == ' ')
				{
					ptr++;
				}

				if (*ptr != '=')
				{
					continue;
				}

				ptr++;
				while (*ptr == ' ')
				{
					ptr++;
				}

				switch (*ptr)
				{
					case 0:
					case '#':
					case ';': break; // do nothing
					default:
					{
						auto dir = std::filesystem::absolute(ptr);
						GENERAL_TRACE_1(
							TEMPSTR("DACOM: Adding DLL Path '%s' ...\n", dir.string().c_str()));
						AddDllDirectory(dir.wstring().c_str());
					}
					break;
				} // end switch
			} // end while
		}

		if ((hSection = parser->CreateSection("Libraries")) == 0)
		{
			GENERAL_NOTICE("DACOM: initialize: [Libraries] group not found in INI. Loading *.DLL from DACOM path.\n");
			LoadAllFromPath("*.dll");
		}
		else
		{
			U32 line = 0;
			char buffer[MAX_PATH];

			GENERAL_TRACE_1("DACOM: initialize: Loading DLL's from [Libraries] group.\n");

			while (parser->ReadProfileLine(hSection, line++, buffer, sizeof(buffer)) != 0)
			{
				char* ptr;

				ptr = buffer;
				while (*ptr == ' ')
					ptr++;

				switch (*ptr)
				{
				case '-':
					ptr++;
					while (*ptr == ' ')
						ptr++;
					ptr = parseFilename(ptr);
					GENERAL_TRACE_1(TEMPSTR("DACOM: initialize: Removing '%s'...\n", ptr));
					RemoveLibrary(ptr);
					break;

				case 0:
				case '#':
				case ';':
					break;			// do nothing

				default:
					ptr = parseFilename(ptr);
					GENERAL_TRACE_1(TEMPSTR("DACOM: initialize: Adding '%s' ...\n", ptr));
					if (strchr(ptr, ':') || strchr(ptr, '\\'))	  // path is provided
						LoadAllFromDirectory("", ptr);
					else
						LoadAllFromPath(ptr);
					break;
				} // end switch
			} // end while
		}

		initialized++;
	}

	return initialized;
}

void DACOManager::UnregisterRelatedComponents(ADDED_LIBRARY* library)
{
	// Search list backwards to release components in inverse order of registration

	REGISTERED_OBJECT* obj = NULL;

	while ((obj = object_list.prev(obj)) != NULL)
	{
		if (obj->library == library)
		{
			// Release reference before invalidating object pointer

			obj->component->Release();

			object_list.free(obj);
			obj = NULL;
		}
	}
}

BOOL32 __stdcall DACOManager::GetAbsolutePath(C8* lpOutput, const C8* lpBaseDir, const C8* lpInput, U32 lSize)
{
	int len;
	C8* ptr;
	BOOL32 result = 1;

	ASSERT(lpInput);

	if (--lSize <= 0)
		return 0;
	lpOutput[lSize] = 0;

	// is this already an absolute path?
	if (lpInput[0] == DA_SWITCH_CHAR || lpInput[1] == ':')
	{
		strncpy(lpOutput, lpInput, lSize);
		lpOutput[lSize - 1] = 0;
		len = strlen(lpOutput);
		if (len && len < lSize && lpOutput[len - 1] != DA_SWITCH_CHAR)
		{
			lpOutput[len] = DA_SWITCH_CHAR;
			lpOutput[len + 1] = 0;
		}

		return 1;
	}

	strncpy(lpOutput, lpBaseDir, lSize);

	// output now of the form "\\Path\\"

	if (lpInput[0] == '.' && lpInput[1] == DA_SWITCH_CHAR)
		lpInput += 2;

	while (lpInput[0] == '.' && lpInput[1] == '.')
	{
		len = strlen(lpOutput);
		if (len > 2)
		{
			lpOutput[len - 1] = 0;		// get rid of trailing '\\'

			if ((ptr = strrchr(lpOutput, DA_SWITCH_CHAR)) != 0)
				ptr[1] = 0;
			else
				result = 0;
		}
		else
			result = 0;
		lpInput += 2;
		if (lpInput[0] == DA_SWITCH_CHAR)
			lpInput++;
	}

	len = strlen(lpOutput);
	if (lSize - len > 0)
		strncpy(lpOutput + len, lpInput, lSize - len);
	len = strlen(lpOutput);
	if (len && len < lSize && lpOutput[len - 1] != DA_SWITCH_CHAR)
	{
		lpOutput[len] = DA_SWITCH_CHAR;
		lpOutput[len + 1] = 0;
	}

	return result;
}

GENRESULT DACOManager::EnumerateInterfaces(IDAComponent* component, DACOMENUMCALLBACK callback, void* context)
{
	//
	// Enumerate call requires valid callback
	//

	if (callback == NULL)
	{
		return GR_INVALID_PARMS;
	}

	//
	// Walk object list, calling client-supplied function for all components
	// that are providers of the specified interface
	//

	REGISTERED_OBJECT* obj = NULL;

	while ((obj = object_list.next(obj)) != NULL)
	{
		//
		// Component must return a successful interface query
		//

		IDAComponent* component_interface;
		GENRESULT queryResult = component->QueryInterface(obj->interface_name, (void**)&component_interface);
		if (queryResult == GR_OK)
		{
			U32 refcount = component_interface->Release();
			(void)refcount; // unused

			if (!callback(obj->component,
				obj->interface_name,
				obj->priority,
				context))
			{
				// 
				// Stop enumerating interfaces if callback returns FALSE
				// 

				break;
			}
		}
	}

	return GR_OK;
}