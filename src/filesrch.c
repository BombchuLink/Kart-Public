/// \file
/// \brief Support to find files
///
///
///	 filesearch:
///
///	 ATTENTION : make sure there is enouth space in filename to put a full path (255 or 512)
///	 if needmd5check == 0 there is no md5 check
///	 if completepath then filename will be change with the full path and name
///	 maxsearchdepth == 0 only search given directory, no subdirs
///	 return FS_NOTFOUND
///	        FS_MD5SUMBAD;
///	        FS_FOUND

#include <stdio.h>
#ifdef __GNUC__
#include <dirent.h>
#endif
#if defined (_WIN32) && !defined (_XBOX)
//#define WIN32_LEAN_AND_MEAN
#define RPC_NO_WINDOWS_H
#include <windows.h>
#endif
#ifdef _WIN32_WCE
#include "sdl12/SRB2CE/cehelp.h"
#else
#include <sys/stat.h>
#endif
#include <string.h>

#include "filesrch.h"
#include "d_netfil.h"
#include "m_misc.h"
#include "z_zone.h"
#include "m_menu.h" // Addons_option_Onchange

#if (defined (_WIN32) && !defined (_WIN32_WCE)) && defined (_MSC_VER) && !defined (_XBOX)

#include <errno.h>
#include <io.h>
#include <tchar.h>

#define SUFFIX	"*"
#define	SLASH	"\\"
#define	S_ISDIR(m)	(((m) & S_IFMT) == S_IFDIR)

#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES	((DWORD)-1)
#endif

struct dirent
{
	long		d_ino;		/* Always zero. */
	unsigned short	d_reclen;	/* Always zero. */
	unsigned short	d_namlen;	/* Length of name in d_name. */
	char		d_name[FILENAME_MAX]; /* File name. */
};

/*
 * This is an internal data structure. Good programmers will not use it
 * except as an argument to one of the functions below.
 * dd_stat field is now int (was short in older versions).
 */
typedef struct
{
	/* disk transfer area for this dir */
	struct _finddata_t	dd_dta;

	/* dirent struct to return from dir (NOTE: this makes this thread
	 * safe as long as only one thread uses a particular DIR struct at
	 * a time) */
	struct dirent		dd_dir;

	/* _findnext handle */
#if _MSC_VER > 1200
	intptr_t    dd_handle;
#else
	long        dd_handle;
#endif

	/*
	 * Status of search:
	 *   0 = not started yet (next entry to read is first entry)
	 *  -1 = off the end
	 *   positive = 0 based index of next entry
	 */
	int			dd_stat;

	/* given path for dir with search pattern (struct is extended) */
	CHAR			dd_name[1];
} DIR;

/*
 * opendir
 *
 * Returns a pointer to a DIR structure appropriately filled in to begin
 * searching a directory.
 */

DIR *
opendir (const CHAR *szPath)
{
  DIR *nd;
  DWORD rc;
  CHAR szFullPath[MAX_PATH];

  errno = 0;

  if (!szPath)
    {
      errno = EFAULT;
      return (DIR *) 0;
    }

  if (szPath[0] == '\0')
    {
      errno = ENOTDIR;
      return (DIR *) 0;
    }

  /* Attempt to determine if the given path really is a directory. */
  rc = GetFileAttributesA(szPath);
  if (rc == INVALID_FILE_ATTRIBUTES)
    {
      /* call GetLastError for more error info */
      errno = ENOENT;
      return (DIR *) 0;
    }
  if (!(rc & FILE_ATTRIBUTE_DIRECTORY))
    {
      /* Error, entry exists but not a directory. */
      errno = ENOTDIR;
      return (DIR *) 0;
    }

  /* Make an absolute pathname.  */
  _fullpath (szFullPath, szPath, MAX_PATH);

  /* Allocate enough space to store DIR structure and the complete
   * directory path given. */
  nd = (DIR *) malloc (sizeof (DIR) + (strlen(szFullPath) + strlen (SLASH) +
			 strlen(SUFFIX) + 1) * sizeof (CHAR));

  if (!nd)
    {
      /* Error, out of memory. */
      errno = ENOMEM;
      return (DIR *) 0;
    }

  /* Create the search expression. */
  strcpy (nd->dd_name, szFullPath);

  /* Add on a slash if the path does not end with one. */
  if (nd->dd_name[0] != '\0' &&
      nd->dd_name[strlen (nd->dd_name) - 1] != '/' &&
      nd->dd_name[strlen (nd->dd_name) - 1] != '\\')
    {
      strcat (nd->dd_name, SLASH);
    }

  /* Add on the search pattern */
  strcat (nd->dd_name, SUFFIX);

  /* Initialize handle to -1 so that a premature closedir doesn't try
   * to call _findclose on it. */
  nd->dd_handle = -1;

  /* Initialize the status. */
  nd->dd_stat = 0;

  /* Initialize the dirent structure. ino and reclen are invalid under
   * Win32, and name simply points at the appropriate part of the
   * findfirst_t structure. */
  nd->dd_dir.d_ino = 0;
  nd->dd_dir.d_reclen = 0;
  nd->dd_dir.d_namlen = 0;
  ZeroMemory(nd->dd_dir.d_name, FILENAME_MAX);

  return nd;
}

/*
 * readdir
 *
 * Return a pointer to a dirent structure filled with the information on the
 * next entry in the directory.
 */
struct dirent *
readdir (DIR * dirp)
{
  errno = 0;

  /* Check for valid DIR struct. */
  if (!dirp)
    {
      errno = EFAULT;
      return (struct dirent *) 0;
    }

  if (dirp->dd_stat < 0)
    {
      /* We have already returned all files in the directory
       * (or the structure has an invalid dd_stat). */
      return (struct dirent *) 0;
    }
  else if (dirp->dd_stat == 0)
    {
      /* We haven't started the search yet. */
      /* Start the search */
      dirp->dd_handle = _findfirst (dirp->dd_name, &(dirp->dd_dta));

	  if (dirp->dd_handle == -1)
	{
	  /* Whoops! Seems there are no files in that
	   * directory. */
	  dirp->dd_stat = -1;
	}
      else
	{
	  dirp->dd_stat = 1;
	}
    }
  else
    {
      /* Get the next search entry. */
      if (_findnext (dirp->dd_handle, &(dirp->dd_dta)))
	{
	  /* We are off the end or otherwise error.
	     _findnext sets errno to ENOENT if no more file
	     Undo this. */
	  DWORD winerr = GetLastError();
	  if (winerr == ERROR_NO_MORE_FILES)
	    errno = 0;
	  _findclose (dirp->dd_handle);
	  dirp->dd_handle = -1;
	  dirp->dd_stat = -1;
	}
      else
	{
	  /* Update the status to indicate the correct
	   * number. */
	  dirp->dd_stat++;
	}
    }

  if (dirp->dd_stat > 0)
    {
      /* Successfully got an entry. Everything about the file is
       * already appropriately filled in except the length of the
       * file name. */
      dirp->dd_dir.d_namlen = (unsigned short)strlen (dirp->dd_dta.name);
      strcpy (dirp->dd_dir.d_name, dirp->dd_dta.name);
      return &dirp->dd_dir;
    }

  return (struct dirent *) 0;
}

/*
 * rewinddir
 *
 * Makes the next readdir start from the beginning.
 */
int
rewinddir (DIR * dirp)
{
  errno = 0;

  /* Check for valid DIR struct. */
  if (!dirp)
    {
      errno = EFAULT;
      return -1;
    }

  dirp->dd_stat = 0;

  return 0;
}

/*
 * closedir
 *
 * Frees up resources allocated by opendir.
 */
int
closedir (DIR * dirp)
{
  int rc;

  errno = 0;
  rc = 0;

  if (!dirp)
    {
      errno = EFAULT;
      return -1;
    }

  if (dirp->dd_handle != -1)
    {
      rc = _findclose (dirp->dd_handle);
    }

  /* Delete the dir structure. */
  free (dirp);

  return rc;
}
#endif

static CV_PossibleValue_t addons_cons_t[] = {{0, "Default"},
#if 1
												{1, "HOME"}, {2, "SRB2"},
#endif
													{3, "CUSTOM"}, {0, NULL}};

consvar_t cv_addons_option = {"addons_option", "Default", CV_SAVE|CV_CALL, addons_cons_t, Addons_option_Onchange, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_addons_folder = {"addons_folder", "", CV_SAVE, NULL, NULL, 0, NULL, NULL, 0, 0, NULL};

static CV_PossibleValue_t addons_md5_cons_t[] = {{0, "Name"}, {1, "Contents"}, {0, NULL}};
consvar_t cv_addons_md5 = {"addons_md5", "Name", CV_SAVE, addons_md5_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_addons_showall = {"addons_showall", "No", CV_SAVE, CV_YesNo, NULL, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_addons_search_case = {"addons_search_case", "No", CV_SAVE, CV_YesNo, NULL, 0, NULL, NULL, 0, 0, NULL};

static CV_PossibleValue_t addons_search_type_cons_t[] = {{0, "Start"}, {1, "Anywhere"}, {0, NULL}};
consvar_t cv_addons_search_type = {"addons_search_type", "Anywhere", CV_SAVE, addons_search_type_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

char menupath[1024];
size_t menupathindex[menudepth];
size_t menudepthleft = menudepth;

char menusearch[MAXSTRINGLENGTH+1];

char **dirmenu, **coredirmenu; // core only local for this file
size_t sizedirmenu, sizecoredirmenu; // ditto
size_t dir_on[menudepth];
UINT8 refreshdirmenu = 0;
char *refreshdirname = NULL;


#if defined (_XBOX) && defined (_MSC_VER)
filestatus_t filesearch(char *filename, const char *startpath, const UINT8 *wantedmd5sum,
	boolean completepath, int maxsearchdepth)
{
//NONE?
	startpath = filename = NULL;
	wantedmd5sum = NULL;
	maxsearchdepth = 0;
	completepath = false;
	return FS_NOTFOUND;
}

void closefilemenu(boolean validsize)
{
	(void)validsize;
	return;
}

void searchfilemenu(char *tempname)
{
	(void)tempname;
	return;
}

boolean preparefilemenu(boolean samedepth, boolean replayhut)
{
	(void)samedepth;
	(void)replayhut;
	return false;
}

#elif defined (_WIN32_WCE)
filestatus_t filesearch(char *filename, const char *startpath, const UINT8 *wantedmd5sum,
	boolean completepath, int maxsearchdepth)
{
#ifdef __GNUC__
//NONE?
	startpath = filename = NULL;
	wantedmd5sum = NULL;
	maxsearchdepth = 0;
	completepath = false;
#else
	WIN32_FIND_DATA dta;
	HANDLE searchhandle = INVALID_HANDLE_VALUE;
	const wchar_t wm[4] = L"*.*";

	//if (startpath) SetCurrentDirectory(startpath);
	if (FIL_ReadFileOK(filename))
	{
		// checkfilemd5 returns an FS_* value, either FS_FOUND or FS_MD5SUMBAD
		return checkfilemd5(filename, wantedmd5sum);
	}
	ZeroMemory(&dta,sizeof (dta));
	if (maxsearchdepth)
		searchhandle = FindFirstFile(wm,&dta);
	if (searchhandle != INVALID_HANDLE_VALUE)
	{
		do
		{
			if ((dta.cFileName[0]!='.') && (dta.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			{
				//if (SetCurrentDirectory(dta.cFileName))
				{ // can fail if we haven't the right
					filestatus_t found;
					found = filesearch(filename,NULL,wantedmd5sum,completepath,maxsearchdepth-1);
					//SetCurrentDirectory("..");
					if (found == FS_FOUND || found == FS_MD5SUMBAD)
					{
						if (completepath)
							strcatbf(filename,(char *)dta.cFileName,"\\");
						FindClose(searchhandle);
						return found;
					}
				}
			}
		} while (FindNextFile(searchhandle,&dta)==0);
		FindClose(searchhandle);
	}
#endif
	return FS_NOTFOUND;
}

void closefilemenu(boolean validsize)
{
	(void)validsize;
	return;
}

void searchfilemenu(char *tempname)
{
	(void)tempname;
	return;
}

boolean preparefilemenu(boolean samedepth, boolean replayhut)
{
	(void)samedepth;
	(void)replayhut;
	return false;
}

#else

filestatus_t filesearch(char *filename, const char *startpath, const UINT8 *wantedmd5sum, boolean completepath, int maxsearchdepth)
{
	filestatus_t retval = FS_NOTFOUND;
	DIR **dirhandle;
	struct dirent *dent;
	struct stat fsstat;
	int found = 0;
	char *searchname = strdup(filename);
	int depthleft = maxsearchdepth;
	char searchpath[1024];
	size_t *searchpathindex;

	dirhandle = (DIR**) malloc(maxsearchdepth * sizeof (DIR*));
	searchpathindex = (size_t *) malloc(maxsearchdepth * sizeof (size_t));

	strcpy(searchpath,startpath);
	searchpathindex[--depthleft] = strlen(searchpath) + 1;

	dirhandle[depthleft] = opendir(searchpath);

	if (dirhandle[depthleft] == NULL)
	{
		free(searchname);
		free(dirhandle);
		free(searchpathindex);
		return FS_NOTFOUND;
	}

	if (searchpath[searchpathindex[depthleft]-2] != '/')
	{
		searchpath[searchpathindex[depthleft]-1] = '/';
		searchpath[searchpathindex[depthleft]] = 0;
	}
	else
		searchpathindex[depthleft]--;

	while ((!found) && (depthleft < maxsearchdepth))
	{
		searchpath[searchpathindex[depthleft]]=0;
		dent = readdir(dirhandle[depthleft]);

		if (!dent)
		{
			closedir(dirhandle[depthleft++]);
			continue;
		}

		if (dent->d_name[0]=='.' &&
				(dent->d_name[1]=='\0' ||
					(dent->d_name[1]=='.' &&
						dent->d_name[2]=='\0')))
		{
			// we don't want to scan uptree
			continue;
		}

		// okay, now we actually want searchpath to incorporate d_name
		strcpy(&searchpath[searchpathindex[depthleft]],dent->d_name);

		if (stat(searchpath,&fsstat) < 0) // do we want to follow symlinks? if not: change it to lstat
			; // was the file (re)moved? can't stat it
		else if (S_ISDIR(fsstat.st_mode) && depthleft)
		{
			searchpathindex[--depthleft] = strlen(searchpath) + 1;
			dirhandle[depthleft] = opendir(searchpath);
			if (!dirhandle[depthleft])
			{
					// can't open it... maybe no read-permissions
					// go back to previous dir
					depthleft++;
			}

			searchpath[searchpathindex[depthleft]-1]='/';
			searchpath[searchpathindex[depthleft]]=0;
		}
		else if (!strcasecmp(searchname, dent->d_name))
		{
			switch (checkfilemd5(searchpath, wantedmd5sum))
			{
				case FS_FOUND:
					if (completepath)
						strcpy(filename,searchpath);
					else
						strcpy(filename,dent->d_name);
					retval = FS_FOUND;
					found = 1;
					break;
				case FS_MD5SUMBAD:
					retval = FS_MD5SUMBAD;
					break;
				default: // prevent some compiler warnings
					break;
			}
		}
	}

	for (; depthleft < maxsearchdepth; closedir(dirhandle[depthleft++]));

	free(searchname);
	free(searchpathindex);
	free(dirhandle);

	return retval;
}

char exttable[NUM_EXT_TABLE][7] = { // maximum extension length (currently 4) plus 3 (null terminator, stop, and length including previous two)
	"\5.txt", "\5.cfg", // exec
	"\5.wad",
#ifdef USE_KART
	"\6.kart",
#endif
	"\5.pk3", "\5.soc", "\5.lua"}; // addfile

char filenamebuf[MAX_WADFILES][MAX_WADPATH];


static boolean filemenucmp(char *haystack, char *needle)
{
	static char localhaystack[128];
	strlcpy(localhaystack, haystack, 128);
	if (!cv_addons_search_case.value)
		strupr(localhaystack);
	if (cv_addons_search_type.value)
		return (strstr(localhaystack, needle) != 0);
	return (!strncmp(localhaystack, needle, menusearch[0]));
}

void closefilemenu(boolean validsize)
{
	// search
	if (dirmenu)
	{
		if (dirmenu != coredirmenu)
		{
			if (dirmenu[0] && ((UINT8)(dirmenu[0][DIR_TYPE]) == EXT_NORESULTS))
			{
				Z_Free(dirmenu[0]);
				dirmenu[0] = NULL;
			}
			Z_Free(dirmenu);
		}
		dirmenu = NULL;
		sizedirmenu = 0;
	}

	if (coredirmenu)
	{
		// core
		if (validsize)
		{
			for (; sizecoredirmenu > 0; sizecoredirmenu--)
			{
				Z_Free(coredirmenu[sizecoredirmenu-1]);
				coredirmenu[sizecoredirmenu-1] = NULL;
			}
		}
		else
			sizecoredirmenu = 0;

		Z_Free(coredirmenu);
		coredirmenu = NULL;
	}

	if (refreshdirname)
		Z_Free(refreshdirname);
	refreshdirname = NULL;
}

void searchfilemenu(char *tempname)
{
	size_t i, first;
	char localmenusearch[MAXSTRINGLENGTH] = "";

	if (dirmenu)
	{
		if (dirmenu != coredirmenu)
		{
			if (dirmenu[0] && ((UINT8)(dirmenu[0][DIR_TYPE]) == EXT_NORESULTS))
			{
				Z_Free(dirmenu[0]);
				dirmenu[0] = NULL;
			}
			//Z_Free(dirmenu); -- Z_Realloc later tho...
		}
		else
			dirmenu = NULL;
	}

	first = (((UINT8)(coredirmenu[0][DIR_TYPE]) == EXT_UP) ? 1 : 0); // skip UP...

	if (!menusearch[0])
	{
		if (dirmenu)
			Z_Free(dirmenu);
		dirmenu = coredirmenu;
		sizedirmenu = sizecoredirmenu;

		if (tempname)
		{
			for (i = first; i < sizedirmenu; i++)
			{
				if (!strcmp(dirmenu[i]+DIR_STRING, tempname))
				{
					dir_on[menudepthleft] = i;
					break;
				}
			}

			if (i == sizedirmenu)
				dir_on[menudepthleft] = first;

			Z_Free(tempname);
		}

		return;
	}

	strcpy(localmenusearch, menusearch+1);
	if (!cv_addons_search_case.value)
		strupr(localmenusearch);

	sizedirmenu = 0;
	for (i = first; i < sizecoredirmenu; i++)
	{
		if (filemenucmp(coredirmenu[i]+DIR_STRING, localmenusearch))
			sizedirmenu++;
	}

	if (!sizedirmenu) // no results...
	{
		if ((!(dirmenu = Z_Realloc(dirmenu, sizeof(char *), PU_STATIC, NULL)))
			|| !(dirmenu[0] = Z_StrDup(va("%c\13No results...", EXT_NORESULTS))))
				I_Error("searchfilemenu(): could not create \"No results...\".");
		sizedirmenu = 1;
		dir_on[menudepthleft] = 0;
		if (tempname)
			Z_Free(tempname);
		return;
	}

	if (!(dirmenu = Z_Realloc(dirmenu, sizedirmenu*sizeof(char *), PU_STATIC, NULL)))
		I_Error("searchfilemenu(): could not reallocate dirmenu.");

	sizedirmenu = 0;
	for (i = first; i < sizecoredirmenu; i++)
	{
		if (filemenucmp(coredirmenu[i]+DIR_STRING, localmenusearch))
		{
			if (tempname && !strcmp(coredirmenu[i]+DIR_STRING, tempname))
			{
				dir_on[menudepthleft] = sizedirmenu;
				Z_Free(tempname);
				tempname = NULL;
			}
			dirmenu[sizedirmenu++] = coredirmenu[i]; // pointer reuse
		}
	}

	if (tempname)
	{
		dir_on[menudepthleft] = 0; //first; -- can't be first, causes problems
		Z_Free(tempname);
	}
}


void string2hexString(char* input, char* output)
{
    int loop;
    int i; 
    
    i=0;
    loop=0;
    
    while(input[loop] != '\0')
    {
        sprintf((char*)(output+i),"%02X", input[loop]);
        loop+=1;
        i+=2;
    }
    //insert NULL at the end of the output string
    output[i++] = '\0';
}


void sortstrings()
{
	size_t i;
	//SORTING ALGORITHM
	//This sorting system uses bucket sort, acompanied by insertion sort once buckets are smaller than 21 files
	//there are two extra buckets in the sort
	//"invalid" bucket simply goes at the end of the sort for anything that doesn't follow the standard naming convention
	//"folder" bucket simply goes at the beginning of the sort so folders are listed first
	//
	//NOTES----
	//Ok, so strings are in coredirmenu[i] and are strings, but they all 
	//have "funny headers" (two bytes, first is filetype, second is length) 
	//
	//TODO: figure out why I'm getting munmap_chunk(): invalid pointer crash on leaving directory
	//TODO: figure out why I'm getting free(): invalid pointer crash on loading mods in certian directories
	//TODO: Get folder to sort as well as files (they are handled weirdly)
	char Folderbucket[sizecoredirmenu][255];
	char AMbucket[sizecoredirmenu][255];
	char NZbucket[sizecoredirmenu][255];
	char INVbucket[sizecoredirmenu][255];
	char BucketEntryCount[4];
	BucketEntryCount[0] = 0; //folder
	BucketEntryCount[1] = 0; //A-N
	BucketEntryCount[2] = 0; //M-Z
	BucketEntryCount[3] = 0; //INvalid

	for (i = 1; i < sizecoredirmenu; i++) 
	{
		//see if the size of file is 0 aka is a folder
		CONS_Printf("raw entry %lu is %s \n", i, coredirmenu[i]);
		int len = strlen(coredirmenu[i]);
		if (len == 0) 
		{
			CONS_Printf("%lu -- copying %s into folder bucket\n", i, coredirmenu[i]);
			strcpy (Folderbucket[BucketEntryCount[0]++], coredirmenu[i]);
			//is a folder, put at beginning of sort
		}
		// make sure the first character is a valid K, k, X, or x character
		else if (coredirmenu[i][2] == 0x4B || coredirmenu[i][2] == 0x6B || coredirmenu[i][2] == 0x58 || coredirmenu[i][2] == 0x78 ) 
		{ 
			//search for first _ in file (0x5F)
			//if greater than the 6th character (KRBCL_) warn, and put at end of sort
			int a = 2;
			char skipcheck = false;
			char underscoredepth = 3;
			while (coredirmenu[i][a++] != 0x5F || coredirmenu[i][a] == 0) 
			{
				//allowing one extra character for a new format type if it's added
				if (a > 8) 
				{
					CONS_Printf("%lu -- copying %s into invalid bucket (naming convention broken)\n", i, coredirmenu[i]);
					strcpy (INVbucket[BucketEntryCount[3]++], coredirmenu[i]);
					//put at end of sort
					skipcheck = true; // not great way to handle this...
					break;
				}
				underscoredepth++;
			}
			//probably could be handled better...
			if (skipcheck == false) {
				// make sure the first character is a valid A-Z or a-z character
				if (coredirmenu[i][underscoredepth] >= 0x41 & coredirmenu[i][underscoredepth] <= 0x7A) 
				{ 
					//if A-M or a-m put in bucket a; if N-Z or n-z put in bucket n
					if (coredirmenu[i][underscoredepth] >= 0x41 & coredirmenu[i][underscoredepth] <= 0x4D || coredirmenu[i][underscoredepth] >= 0x61 & coredirmenu[i][underscoredepth] <= 0x6D) 
					{
						CONS_Printf("%lu -- copying %s into AM bucket\n", i, coredirmenu[i]);
						strcpy (AMbucket[BucketEntryCount[1]++], coredirmenu[i]);
					}
					if (coredirmenu[i][underscoredepth] >= 0x4E & coredirmenu[i][underscoredepth] <= 0x5A || coredirmenu[i][underscoredepth] >= 0x6E & coredirmenu[i][underscoredepth] <= 0x7A) 
					{
						CONS_Printf("%lu -- copying %s into NZ bucket\n", i, coredirmenu[i]);
						strcpy (NZbucket[BucketEntryCount[2]++], coredirmenu[i]);
					}
				}
				else 
				{
					CONS_Printf("%lu -- copying %s into invalid bucket(first character not A-Z)\n", i, coredirmenu[i]);
					strcpy (INVbucket[BucketEntryCount[3]++], coredirmenu[i]);
				}
			}
		}
		else 
		{
			CONS_Printf("%lu -- copying %s into invalid bucket (does not start with K or X)\n", i, coredirmenu[i]);
			strcpy (INVbucket[BucketEntryCount[3]++], coredirmenu[i]);
		}
	}

	//DEBUG - Print bucket contents
	printf("-----BUCKETS-----\n");
	CONS_Printf("folder bucket is %d entries.\n",BucketEntryCount[0] );
	printf("--------A-N Bucket--------\n");
	for (i = 1; i < BucketEntryCount[1]; i++) 
	{
		CONS_Printf("%s\n", AMbucket[i]);
	}
	printf("--------N-Z Bucket--------\n");
	for (i = 1; i < BucketEntryCount[2]; i++) 
	{
		CONS_Printf("%s\n", NZbucket[i]);
	}
	printf("--------INVALID Bucket--------\n");
	for (i = 1; i < BucketEntryCount[3]; i++) 
	{
		CONS_Printf("%s\n", INVbucket[i]);
	}

	//sort buckets...
	// in order to sort, I have to somehow ignore the first two characters in sorting, but put whole string in slot
	
	//stuff into pointer array for loop iterations
	//void *BucketsAdr[sizeofdirmenu][255][4];
	char (*BucketsAdr[4])[sizecoredirmenu][255];
	BucketsAdr[0] = &Folderbucket;
	BucketsAdr[1] = &AMbucket;
	BucketsAdr[2] = &NZbucket;
	BucketsAdr[3] = &INVbucket;
	
	//printf("Report Data on Buckets, and begin sorting:\n");
	for (int bucketnum = 1; bucketnum < 4; bucketnum++) 
	{
		char (*currentbucket)[sizecoredirmenu][255] = BucketsAdr[bucketnum];
		//insertion sort starts with the firs string already "sorted"
		//CONS_Printf("String length is unknown for string 0 (insertion sort doesn't need to grab it) \n");
		for (int stringnum = 1; stringnum < BucketEntryCount[bucketnum]; stringnum++) 
		{
			//grab target
			char stringlen = (*currentbucket)[stringnum][1]; //NOTE stringlen does NOT include header, but does include termination char
			char targetstring[255];
			memset (targetstring, 0, 255); //empty temp pointer
			for (int stringchar = 0; stringchar <= stringlen+1; stringchar++) 
			{
				//put data into temp string
				targetstring[stringchar] = (*currentbucket)[stringnum][stringchar];
			}
			char hex_str[(stringlen*2)+4];
			string2hexString(targetstring, hex_str); 
			//CONS_Printf("String length is %d for string %d (%s | %s) in bucket %d \n", stringlen, stringnum, targetstring, hex_str, bucketnum);

			int targetstringoffset = 0;
			//check for where _ is in file for naimg convention (unless invalid bucket
			if (bucketnum != 3) {
				int a=2;
				while (true) {
					if ( targetstring[a] != '_') 
					{
						//allowing one extra character for a new format type if it's added
						if (a > 8) 
						{
							targetstringoffset = 0;
							CONS_Printf("CRITICAL ERROR! TARGET STRING \"%s\"DOES NOT HAVE _ DESPITE BEING SORTED INTO VALID BUCKET!!\n", targetstring);
							break;
						}
						//CONS_Printf("string %s does not have a _  on character %d\n", targetstring, a);
					}
					else {
						//is valid!
						targetstringoffset++;
						break;

					}
					targetstringoffset++;
					a++;
				}
			}
			//grab loc string

			int StringToSearch = stringnum;
			int pasteloc = stringnum;
			int offsetloc = 0;
			//CONS_Printf("String we are inserting is %s\n", targetstring);

			while (StringToSearch-- >= 0 ) {
				/*
				printf("BUCKET SORT STATUS SO FAR ---\n");
				for (i = 0; i < BucketEntryCount[bucketnum]; i++) 
				{
					CONS_Printf("slot %lu - %s\n", i, (*currentbucket)[i]);
				}
				*/
				char currentstring[255];
				memset (currentstring, 0, 255); //empty temp pointer

				//grab string to compare to
				char currentstringlen = (*currentbucket)[StringToSearch][1]; 
				for (int stringchar = 0; stringchar <= currentstringlen+1; stringchar++) 
				{
					//put data into temp string
					currentstring[stringchar] = (*currentbucket)[StringToSearch][stringchar];
				}
				//CONS_Printf("String we are comparing %s against is %s (slot %d)\n", targetstring, currentstring, StringToSearch);

				int stringtosearchoffset = 0;
				//check for where _ is in file for naimg convention (unless invalid bucket
				if (bucketnum != 3) {
					int a=2;
					while (true) {
						if ( currentstring[a] != '_') 
						{
							//allowing one extra character for a new format type if it's added
							if (a > 8) 
							{
								targetstringoffset = 0;
								CONS_Printf("CRITICAL ERROR! STRING TO SEARCH \"%s\"DOES NOT HAVE _ DESPITE BEING SORTED INTO VALID BUCKET!!\n", currentstring);
								break;
							}

							//CONS_Printf("string %s does not have a _  on character %d\n", currentstring, a);
						}
						else {
							//is valid!
							stringtosearchoffset++;
							break;

						}
						stringtosearchoffset++;
						a++;
					}
				}

				//begin insertion sort...

				//the whole reason we have to do this is these damn headers
				int stringcmpnum;

				int currentstringchar;
				int targetstringchar;

				//for (stringcmpnum = 2; currentstring[stringcmpnum+stringtosearchoffset] == targetstring[stringcmpnum+targetstringoffset]; stringcmpnum++) {
				stringcmpnum = 2;
				while (true) {
					currentstringchar = currentstring[stringcmpnum+stringtosearchoffset];
					targetstringchar = targetstring[stringcmpnum+targetstringoffset];

					//convert to upper case
					if (currentstringchar >= 0x61 && currentstringchar <= 0x7A) {
						currentstringchar -= 0x20;
					}
					if (targetstringchar >= 0x61 && targetstringchar <= 0x7A) {
						targetstringchar -= 0x20;
					}

				        if (currentstringchar != targetstringchar) {
						break;
					}

					if (stringcmpnum == 255) {
						printf("CRITICAL ERROR STRINGS %d and %d are the same up to the 255th character!!!", stringnum, stringcmpnum);
						break;
					}
					if (stringcmpnum == 2) {
						//printf("first char in string is %d\n", currentstring[stringcmpnum+stringtosearchoffset]);
					}
					stringcmpnum++;
				}
				//ok it's different now, WHY
				if (currentstringchar > targetstringchar) 
				{
					//shift current string forward in array and repeat
					strcpy ( (*currentbucket)[stringnum-offsetloc], currentstring); 
					
					//CONS_Printf("SHIFTED STRING at slot %d forward one! (%s is higher than %s)\n", StringToSearch, currentstring, targetstring);
					pasteloc--;
					offsetloc++;
					
					//printf("pasteloc is now %d\n", pasteloc);
					if (pasteloc == 0) 
					{
						//CONS_Printf("AT BEGINNING OF LIST, pasting %s into slot 0\n", targetstring);
						strcpy ( (*currentbucket)[pasteloc], targetstring);
						break;
					}
				}
				else if (currentstringchar < targetstringchar) 
				{
					//string belongs in this slot
					strcpy ( (*currentbucket)[pasteloc], targetstring);
					//CONS_Printf("STRING %s IS NOT LOWER; SORTED at slot %d!\n", targetstring, pasteloc);
					break;
				}
			}

		}
		printf("bucket %d is sorted ======\n", bucketnum);
		for (i = 0; i < BucketEntryCount[bucketnum]; i++) 
		{
			CONS_Printf("slot %lu - %s\n", i, (*currentbucket)[i]);
		}

	}
	//Buckets sorted! merge back into coredirmenu
	int n = BucketEntryCount[0];
	printf("%d folders have been detected for re-merge...\n", n);
	n++; //we start at slot one because, ummm, uhhh, ummmmmm, it stopped crashing when I put this here??
	for (int bucketnum = 1; bucketnum < 4; bucketnum++) 
	{
		char (*currentbucket)[sizecoredirmenu][255] = BucketsAdr[bucketnum];
		for (int a=0; a < BucketEntryCount[bucketnum]; a++) {
			CONS_Printf("storing entry %d (%s) back into coredirmenu\n", n, (*currentbucket)[a]);
			strcpy (coredirmenu[n], (*currentbucket)[a]);
			n++;
		}
	}
	printf("all values stored!\n");
}


boolean preparefilemenu(boolean samedepth, boolean replayhut)
{
	DIR *dirhandle;
	struct dirent *dent;
	struct stat fsstat;
	size_t pos = 0, folderpos = 0, numfolders = 0;
	char *tempname = NULL;

	if (samedepth)
	{
		if (dirmenu && dirmenu[dir_on[menudepthleft]])
			tempname = Z_StrDup(dirmenu[dir_on[menudepthleft]]+DIR_STRING); // don't need to I_Error if can't make - not important, just QoL
	}
	else
		menusearch[0] = menusearch[1] = 0; // clear search

	if (!(dirhandle = opendir(menupath))) // get directory
	{
		closefilemenu(true);
		return false;
	}

	for (; sizecoredirmenu > 0; sizecoredirmenu--) // clear out existing items
	{
		Z_Free(coredirmenu[sizecoredirmenu-1]);
		coredirmenu[sizecoredirmenu-1] = NULL;
	}

	while (true)
	{
		menupath[menupathindex[menudepthleft]] = 0;
		dent = readdir(dirhandle);

		if (!dent)
			break;
		else if (dent->d_name[0]=='.' &&
				(dent->d_name[1]=='\0' ||
					(dent->d_name[1]=='.' &&
						dent->d_name[2]=='\0')))
			continue; // we don't want to scan uptree

		strcpy(&menupath[menupathindex[menudepthleft]],dent->d_name);

		if (stat(menupath,&fsstat) < 0) // do we want to follow symlinks? if not: change it to lstat
			; // was the file (re)moved? can't stat it
		else // is a file or directory
		{
			if (!S_ISDIR(fsstat.st_mode)) // file
			{
				size_t len = strlen(dent->d_name)+1;
				if (replayhut)
				{
					if (strcasecmp(".lmp", dent->d_name+len-5)) continue; // Not a replay
				}
				else if (!cv_addons_showall.value)
				{
					UINT8 ext;
					for (ext = 0; ext < NUM_EXT_TABLE; ext++)
						if (!strcasecmp(exttable[ext]+1, dent->d_name+len-(exttable[ext][0]))) break; // extension comparison
					if (ext == NUM_EXT_TABLE) continue; // not an addfile-able (or exec-able) file
				}
			}
			else // directory
				numfolders++;

			sizecoredirmenu++;
		}
	}

	if (!sizecoredirmenu)
	{
		closedir(dirhandle);
		closefilemenu(false);
		if (tempname)
			Z_Free(tempname);
		return false;
	}

	if (menudepthleft != menudepth-1) // Make room for UP...
	{
		sizecoredirmenu++;
		numfolders++;
		folderpos++;
	}

	if (dirmenu && dirmenu == coredirmenu)
		dirmenu = NULL;

	if (!(coredirmenu = Z_Realloc(coredirmenu, sizecoredirmenu*sizeof(char *), PU_STATIC, NULL)))
	{
		closedir(dirhandle); // just in case
		I_Error("preparefilemenu(): could not reallocate coredirmenu.");
	}

	rewinddir(dirhandle);

	while ((pos+folderpos) < sizecoredirmenu)
	{
		menupath[menupathindex[menudepthleft]] = 0;
		dent = readdir(dirhandle);

		if (!dent)
			break;
		else if (dent->d_name[0]=='.' &&
				(dent->d_name[1]=='\0' ||
					(dent->d_name[1]=='.' &&
						dent->d_name[2]=='\0')))
			continue; // we don't want to scan uptree

		strcpy(&menupath[menupathindex[menudepthleft]],dent->d_name);

		if (stat(menupath,&fsstat) < 0) // do we want to follow symlinks? if not: change it to lstat
			; // was the file (re)moved? can't stat it
		else // is a file or directory
		{
			char *temp;
			size_t len = strlen(dent->d_name)+1;
			UINT8 ext = EXT_FOLDER;
			UINT8 folder;

			if (!S_ISDIR(fsstat.st_mode)) // file
			{
				if (!((numfolders+pos) < sizecoredirmenu)) continue; // crash prevention

				if (replayhut)
				{
					if (strcasecmp(".lmp", dent->d_name+len-5)) continue; // Not a replay
					ext = EXT_TXT; // This isn't used anywhere but better safe than sorry for messing with this...
				}
				else
				{
					for (; ext < NUM_EXT_TABLE; ext++)
						if (!strcasecmp(exttable[ext]+1, dent->d_name+len-(exttable[ext][0]))) break; // extension comparison
					if (ext == NUM_EXT_TABLE && !cv_addons_showall.value) continue; // not an addfile-able (or exec-able) file
					ext += EXT_START; // moving to be appropriate position

					if (ext >= EXT_LOADSTART)
					{
						size_t i;
						for (i = 0; i < numwadfiles; i++)
						{
							if (!filenamebuf[i][0])
							{
								strncpy(filenamebuf[i], wadfiles[i]->filename, MAX_WADPATH);
								filenamebuf[i][MAX_WADPATH - 1] = '\0';
								nameonly(filenamebuf[i]);
							}

							if (strcmp(dent->d_name, filenamebuf[i]))
								continue;
							if (cv_addons_md5.value && !checkfilemd5(menupath, wadfiles[i]->md5sum))
								continue;

							ext |= EXT_LOADED;
						}
						
					}
					else if (ext == EXT_TXT)
					{
						if (!strcmp(dent->d_name, "log.txt") || !strcmp(dent->d_name, "errorlog.txt"))
							ext |= EXT_LOADED;
					}

					if (!strcmp(dent->d_name, configfile))
						ext |= EXT_LOADED;
				}

				folder = 0;
			}
			else // directory
				len += (folder = 1);

			if (len > 255)
				len = 255;

			if (!(temp = Z_Malloc((len+DIR_STRING+folder) * sizeof (char), PU_STATIC, NULL)))
				I_Error("preparefilemenu(): could not create file entry.");
			temp[DIR_TYPE] = ext;
			temp[DIR_LEN] = (UINT8)(len);
			strlcpy(temp+DIR_STRING, dent->d_name, len);
			if (folder)
			{
				strcpy(temp+len, PATHSEP);
				coredirmenu[folderpos++] = temp;
			}
			else if (replayhut) // Reverse-alphabetical on just the files; acts as a fake "most recent first" with the current filename format
				coredirmenu[sizecoredirmenu - 1 - pos++] = temp;
			else
				coredirmenu[numfolders + pos++] = temp;
		}
	}
	if (!(replayhut)) {
		size_t i = 0;
		printf("there are %lu entries in this directory \n", sizecoredirmenu );
		printf("natural order of files:\n");
		for (i = 1; i < sizecoredirmenu; i++) 
		{
		    int len = strlen(coredirmenu[i]);
		    char hex_str[(len*2)+1];
		    string2hexString(coredirmenu[i], hex_str); //unknown why, but can't get hex from folders (probably unimportant?)
			printf("buf is %s [size = %d] (%s)\n", coredirmenu[i]+2, len, hex_str);
		}
		//sort mod entries to be alphanumeric
		sortstrings();
		printf("--------------\n");
	}

	closedir(dirhandle);

	if ((menudepthleft != menudepth-1) // now for UP... entry
		&& !(coredirmenu[0] = Z_StrDup(va("%c\5UP...", EXT_UP))))
			I_Error("preparefilemenu(): could not create \"UP...\".");

	menupath[menupathindex[menudepthleft]] = 0;
	sizecoredirmenu = (numfolders+pos); // just in case things shrink between opening and rewind

	if (!sizecoredirmenu)
	{
		dir_on[menudepthleft] = 0;
		closefilemenu(false);
		return false;
	}

	searchfilemenu(tempname);

	return true;
}

#endif
