/*

mtf - a Microsoft Tape Format reader (and future writer?)
Copyright (C) 1999  D. Alan Stewart, Layton Graphics, Inc.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

Contact the author at:

D. Alan Stewart
Layton Graphics, Inc.
155 Woolco Dr.
Marietta, GA 30062, USA
astewart@layton-graphics.com


Contributors:

Andrew Barnett <mtf@precisionlinux.com>

	Andrew contributed some early patches to 0.1 for tape drives that use
	physical block sizes different from that used by my tape drive. The code
	itself has disappeared as I've reworked the source, but the ideas are still
	there.

Alex Krowitx <alexkrowitz@my-Deja.com>

	Alex provided a bug fix to 0.2 (which was never put in general release) for
	tapes having a physical block size smaller than the MTF logical block size.

Version history:

	0.1		DAS 3/22/1999 - initial release
	0.2.1	DAS 9/13/2000 - automatic determination of the tape drive's physical
			block size; added -b switch to manually specify tape physical block
			size; when using the pattern-matching features 0.5 creates only
			those directories required for files that are actually read from
			tape; renamed -f switch to -F (it seems best to reserve lower case
			for frequently used switches); -F switch tests for free space at
			file creation time rather than directory creation time; added
			MTF_OPTS envinronment variable support


**
**	mtf.c
**
**	This is the main source code file for the progam mtf. It is a bare-bones
**	read for Microsoft Tape Format tapes. Many things unsupported!
**
*/

#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mtio.h>
#include <regex.h>
#include <grp.h>
#include <pwd.h>
#include "mtf.h"


typedef char* CHARPTR;


char outPath[MAXPATHLEN + 1];
char curPath[MAXPATHLEN + 1];
char device[MAXPATHLEN + 1];
int mtfd = -1;
UINT8 verbose, debug, list, forceCase;
UINT8 tBuffer[MAX_TAPE_BLOCK_SIZE];
UINT16 setNum, matchCnt;
UINT32 minFree;
size_t tapeBlockSize;
regex_t match[MAX_PATTERN];
gid_t group;
uid_t owner;


int main(int, char*[]);
static CHARPTR* parseEnv(char*, int*);
static INT16 parseArgs(int, char*[]);
static INT16 whichSet(char*);
static INT16 whichDevice(char*);
static INT16 setBlockSize(char*);
static INT16 setPath(char*);
static INT16 setOwner(char*);
static INT16 setGroup(char*);
static INT16 setCase(char*);
static INT16 setMinFree(char*);
static INT16 getPatterns(int, char*[], int);
static void usage(void);


int main(int argc, char *argv[])
{
	char *ptr, **args, path[MAXPATHLEN + 1];
	int i, count;
	INT16 result;
	struct stat sbuf;
	struct mtop op;
	struct mtget get;
	struct passwd *pbuf;
	struct group *gbuf;

	verbose = 0;
	debug = 0;
	list = 0;
	setNum = 0;
	strcpy(outPath, "");
	matchCnt = 0;
	tapeBlockSize = 0;
	minFree = 0;
	owner = -1;
	group = -1;
	forceCase = CASE_SENSITIVE;

	ptr = getenv("TAPE");
	if (ptr != NULL)
		strcpy(device, ptr);
	else
		strcpy(device, "/dev/tape");

	ptr = getenv("MTF_OPTS");
	if ((ptr != NULL) && (strlen(ptr) > 0))
	{
		args = parseEnv(ptr, &count);
		if (args == NULL) return(-1);

		if (parseArgs(count, args) != 0) return(-1);

		for (i = 0; i < count; i += 1) free(args[i]);

		free(args);
	}

	if (parseArgs(argc, argv) != 0)
	{
		return(-1);
	}

	if (outPath[0] != '/')
	{
		getcwd(curPath, MAXPATHLEN);
		strcpy(path, outPath);
		sprintf(outPath, "%s/%s", curPath, path);

		if (outPath[strlen(outPath) - 1] == '/')
			outPath[strlen(outPath) - 1] = '\0';
	}

	if (debug > 0)
	{
		verbose = 2;
		fprintf(stdout, "Debug mode selected.\n");
	}

	if (verbose == 1)
	{
		fprintf(stdout, "Verbose mode selected.\n");
	}
	else if (verbose > 1)
	{
		fprintf(stdout, "Very verbose mode selected.\n");
		
		if (list > 0) fprintf(stdout, "List mode selected.\n");

		if (forceCase == CASE_UPPER)
			fprintf(stdout, "Case forced to upper.\n");
		else if (forceCase == CASE_LOWER)
			fprintf(stdout, "Case forced to lower.\n");

		if (tapeBlockSize != (size_t) 0)
			fprintf(stdout, "Tape block size set to %u bytes.\n",
			        tapeBlockSize);

		if (setNum != 0) fprintf(stdout, "Set %u will be read.\n", setNum);

		if (owner != (uid_t) -1)
		{
			pbuf = getpwuid(owner);
			if (pbuf == NULL)
				fprintf(stderr, "Unable to retrieve name of uid %u!\n", owner);
			else
				fprintf(stdout, "Files will be owned by %s.\n",
				        pbuf->pw_name);
		}

		if (group != (gid_t) -1)
		{
			gbuf = getgrgid(group);
			if (gbuf == NULL)
				fprintf(stderr, "Unable to retrieve name of gid %u!\n", group);
			else
				fprintf(stdout, "Files will belong to group %s.\n",
				        gbuf->gr_name);
		}

		fprintf(stdout, "Files will be written to %s.\n", outPath);
		fprintf(stdout, "Tape device will be %s.\n", device);

		if (minFree != 0)
			fprintf(stdout, "Free space of %lu bytes will be maintained.\n",
			        minFree);
		
		if (matchCnt > 0)
			fprintf(stdout, "%u patterns were found.\n", matchCnt);
	}

	if (stat(outPath, &sbuf) != 0)
	{
		fprintf(stderr, "Error testing for status of %s!\n", outPath);
		return(-1);
	}

	if ((sbuf.st_mode & S_IFDIR) == 0)
	{
		fprintf(stderr, "%s is not a directory!\n", outPath);
		return(-1);
	}

	strcpy(curPath, "");

	mtfd = open(device, O_RDONLY);
	if (mtfd < 0)
	{
		fprintf(stderr, "Error opening %s for reading!\n", device);
		return(-1);
	}

	if (openMedia() != 0)
	{
		fprintf(stderr, "Error opening tape!\n");
		goto error;
	}

	if (setNum > 1)
	{
		op.mt_op = MTFSF;
		op.mt_count = (setNum - 1) * 2;

		if (verbose > 0)
			fprintf(stdout, "Forwarding tape to data set #%u...\n", setNum);

		if (ioctl(mtfd, MTIOCTOP, &op) != 0)
		{
			fprintf(stderr, "Error forwarding tape!\n");
			goto error;
		}

		if (debug > 0)
		{
			op.mt_op = MTNOP;
			op.mt_count = 0;

			if (ioctl(mtfd, MTIOCTOP, &op) != 0)
			{
				fprintf(stderr, "Error returned by MTIOCTOP!\n");
				goto error;
			}

			if (ioctl(mtfd, MTIOCGET, &get) != 0)
			{
				fprintf(stderr, "Error returned by MTIOCGET!\n");
				goto error;
			}

			printf("tape file no. %u\n", get.mt_fileno);
			printf("tape block no. %u\n", get.mt_blkno);
		}

		if (readNextBlock(0) != 0)
		{
			fprintf(stderr, "Error reading first block of data set!\n");
			goto error;
		}
	}

next:

	if (readDataSet() != 0)
	{
		fprintf(stderr, "Error reading data set!\n");
		goto error;
	}

	result = readEndOfDataSet();
	if (result < 0)
	{
		fprintf(stderr, "Error reading to end of data set!\n");
		goto error;
	}

	if ((result == 0) && (setNum == 0))
		goto next;

	if (verbose > 0) fprintf(stdout, "Successful read of archive!\n");
	
	close(mtfd);

	return(0);

error:

	for (i = 0; i < matchCnt; i += 1)
	{
		regfree(&match[i]);
	}

	dump("errorblock.dmp");
	if (mtfd != -1) close(mtfd);

	return(-1);
}


CHARPTR* parseEnv(char *env, int *total)
{
	int i, count;
	char *ptr;
	CHARPTR *args;
	size_t span;

	count = 0;
	ptr = env;
	while (*ptr != '\0')
	{
		ptr += strspn(ptr, " \t");
		if (*ptr == '\"')
		{
			ptr += 1;
			ptr += strcspn(ptr, "\"");
			ptr += 1;
			count += 1;
		}
		else if (*ptr == '\'')
		{
			ptr += 1;
			ptr += strcspn(ptr, "\'");
			ptr += 1;
			count += 1;
		}
		else if (*ptr != '\0')
		{
			ptr += strcspn(ptr, " \t");
			count += 1;
		}
	}

	count += 1;

	args = (CHARPTR*) malloc(sizeof(CHARPTR) * (size_t) count);
	if (args == NULL)
	{
		fprintf(stderr, "Memory error while parsing MTF_OPTS!\n");
		return(NULL);
	}

	args[0] = (char*) malloc(sizeof(char) * 1);
	if (args[0] == NULL)
	{
		fprintf(stderr, "Memory error while parsing MTF_OPTS!\n");
		return(NULL);
	}

	strcpy(args[0], "");

	i = 1;
	ptr = env;
	while (*ptr != '\0')
	{
		ptr += strspn(ptr, " \t");
		if (*ptr == '\"')
		{
			ptr += 1;
			span = strcspn(ptr, "\"");
			args[i] = (char*) malloc(sizeof(char) * (span + 1));
			if (args[i] == NULL)
			{
				fprintf(stderr, "Memory error while parsing MTF_OPTS!\n");
				return(NULL);
			}
			memcpy(args[i], ptr, span);
			(args[i])[span] = '\0';
			ptr += span + 1;
			i += 1;
		}
		else if (*ptr == '\'')
		{
			ptr += 1;
			span = strcspn(ptr, "\'");
			args[i] = (char*) malloc(sizeof(char) * (span + 1));
			if (args[i] == NULL)
			{
				fprintf(stderr, "Memory error while parsing MTF_OPTS!\n");
				return(NULL);
			}
			memcpy(args[i], ptr, span);
			(args[i])[span] = '\0';
			ptr += span + 1;
			i += 1;
		}
		else if (*ptr != '\0')
		{
			span = strcspn(ptr, " \t");
			args[i] = (char*) malloc(sizeof(char) * (span + 1));
			if (args[i] == NULL)
			{
				fprintf(stderr, "Memory error while parsing MTF_OPTS!\n");
				return(NULL);
			}
			memcpy(args[i], ptr, span);
			(args[i])[span] = '\0';
			ptr += span;
			i += 1;
		}
	}

	*total = count;

	return args;
}


INT16 parseArgs(int argc, char *argv[])
{
	int i;
	char *ptr;

	i = 1;

	while ((i < argc) && (argv[i][0] == '-'))
	{
		ptr = &argv[i][1];

		if (strlen(ptr) > 1)
		{
			while (*ptr != '\0')
			{
				if (*ptr == 'v')
				{
					verbose = 1;
				}
				else if (*ptr == 'V')
				{
					verbose = 2;
				}
				else if (*ptr == 'D')
				{
					debug = 1;
				}
				else if (*ptr == 'l')
				{
					list = 1;
				}
				else
				{
					fprintf(stderr, "Unrecognized switch (-%c)!\n", *ptr);
					usage();
					return(-1);
				}

				ptr += 1;
			}
		}
		else
		{
			if (*ptr == 'v')
			{
				verbose = 1;
			}
			else if (*ptr == 'V')
			{
				verbose = 2;
			}
			else if (*ptr == 'D')
			{
				debug = 1;
			}
			else if (*ptr == 'l')
			{
				list = 1;
			}
			else if (strcmp(ptr, "s") == 0)
			{
				i += 1;

				if (i == argc)
				{
					fprintf(stderr, "Argument required for -s switch!\n");
					usage();
					return(-1);
				}

				if (whichSet(argv[i]) != 0)
					return(-1);
			}
			else if (strcmp(ptr, "d") == 0)
			{
				i += 1;

				if (i == argc)
				{
					fprintf(stderr, "Argument required for -d switch!\n");
					usage();
					return(-1);
				}

				if (whichDevice(argv[i]) != 0)
					return(-1);
			}
			else if (strcmp(ptr, "b") == 0)
			{
				i += 1;

				if (i == argc)
				{
					fprintf(stderr, "Argument required for -b switch!\n");
					usage();
					return(-1);
				}

				if (setBlockSize(argv[i]) != 0)
					return(-1);
			}
			else if (strcmp(ptr, "o") == 0)
			{
				i += 1;

				if (i == argc)
				{
					fprintf(stderr, "Argument required for -o switch!\n");
					usage();
					return(-1);
				}

				if (setPath(argv[i]) != 0)
					return(-1);
			}
			else if (strcmp(ptr, "u") == 0)
			{
				i += 1;

				if (i == argc)
				{
					fprintf(stderr, "Argument required for -u switch!\n");
					usage();
					return(-1);
				}

				if (setOwner(argv[i]) != 0)
					return(-1);
			}
			else if (strcmp(ptr, "g") == 0)
			{
				i += 1;

				if (i == argc)
				{
					fprintf(stderr, "Argument required for -g switch!\n");
					usage();
					return(-1);
				}

				if (setGroup(argv[i]) != 0)
					return(-1);
			}
			else if (strcmp(ptr, "c") == 0)
			{
				i += 1;

				if (i == argc)
				{
					fprintf(stderr, "Argument required for -c switch!\n");
					usage();
					return(-1);
				}

				if (setCase(argv[i]) != 0)
					return(-1);
			}
			else if (strcmp(ptr, "F") == 0)
			{
				i += 1;

				if (i == argc)
				{
					fprintf(stderr, "Argument required for -F switch!\n");
					usage();
					return(-1);
				}

				if (setMinFree(argv[i]) != 0)
					return(-1);
			}
			else
			{
				fprintf(stderr, "Unrecognized switch (-%c)!\n", *ptr);
				usage();
				return(-1);
			}
		}

		i += 1;
	}

	if (i < argc)
	{
		if (getPatterns(argc, argv, i) != 0)
			return(-1);
		else
			return(0);
	}

	return(0);
}


INT16 whichSet(char *argv)
{
	UINT16 test;

	if (strcmp(argv, "*") == 0)
	{
		setNum = 0;
	}
	else
	{
		if (strspn(argv, "0123456789") != strlen(argv))
		{
			fprintf(stderr, "Invalid value given for set number (-s)!\n");
			usage();
			return(-1);
		}

		if (sscanf(argv, "%hu", &test) != 1)
		{
			fprintf(stderr,
			        "Unable to parse value given for set number (-s)!\n");
			usage();
			return(-1);
		}

		if ((test < 1) || (test > 65535))
		{
			fprintf(stderr,
			        "Value given for set number (-s) is out of range!\n");
			usage();
			return(-1);
		}

		setNum = test;
	}

	return(0);
}


INT16 whichDevice(char *argv)
{
	strcpy(device, argv);

	return(0);
}


INT16 setBlockSize(char *argv)
{
	UINT32 test;

	if (strspn(argv, "0123456789") != strlen(argv))
	{
		fprintf(stderr, "Invalid value given for tape block size (-b)!\n");
		usage();
		return(-1);
	}

	if (sscanf(argv, "%lu", &test) != 1)
	{
		fprintf(stderr, "Unable to parse value given for block size (-b)!\n");
		usage();
		return(-1);
	}

	if ((test < (UINT32) MIN_TAPE_BLOCK_SIZE) ||
	    (test > (UINT32) MAX_TAPE_BLOCK_SIZE))
	{
		fprintf(stderr,
				"Value given for block size (-B) is out of range (%u-%u)!\n",
				MIN_TAPE_BLOCK_SIZE, MAX_TAPE_BLOCK_SIZE);
		usage();
		return(-1);
	}

	tapeBlockSize = (size_t) test;

	return(0);
}


INT16 setPath(char *argv)
{
	strcpy(outPath, argv);

	return(0);
} 


INT16 setOwner(char *argv)
{
	struct passwd *pbuf;
	UINT32 test;

	if (strspn(argv, "0123456789") == strlen(argv))
	{
		if (sscanf(argv, "%lu", &test) != 1)
		{
			fprintf(stderr, "Unable to parse value given for owner (-u)!\n");
			usage();
			return(-1);
		}

		owner = test;
	}
	else
	{
		pbuf = getpwnam(argv);
		if (pbuf == NULL)
		{
			fprintf(stderr, "Unable to retrieve user id (-u) of %s!\n", argv);
			usage();
			return(-1);
		}

		owner = pbuf->pw_uid;
	}

	return(0);
} 


INT16 setGroup(char *argv)
{
	struct group *gbuf;
	UINT32 test;

	if (strspn(argv, "0123456789") == strlen(argv))
	{
		if (sscanf(argv, "%lu", &test) != 1)
		{
			fprintf(stderr, "Unable to parse value given for owner (-u)!\n");
			usage();
			return(-1);
		}

		group = test;
	}
	else
	{
		gbuf = getgrnam(argv);
		if (gbuf == NULL)
		{
			fprintf(stderr, "Unable to retrieve group id (-g) of %s!\n", argv);
			usage();
			return(-1);
		}

		group = gbuf->gr_gid;
	}

	return(0);
} 


INT16 setCase(char *argv)
{
	if (strlen(argv) == 0)
	{
		fprintf(stderr, "No value given for forcing case (-c)!\n");
		usage();
		return(-1);
	}

	strlwr(argv);

	if (strcmp(argv, "lower") == 0)
		forceCase = CASE_LOWER;
	else if (strcmp(argv, "upper") == 0)
		forceCase = CASE_UPPER;
	else
	{
		fprintf(stderr, "Invalid value given for forcing case (-c) - \"%s\"!\n",		        argv);
		usage();
		return(-1);
	}

	return(0);
} 


INT16 setMinFree(char *argv)
{
	char *ptr;
	UINT32 multiplier;

	if (strlen(argv) == 0)
	{
		fprintf(stderr, "No value given for minimum free space (-f)!\n");
		usage();
		return(-1);
	}

	strlwr(argv);

	ptr = argv;
	while ((*ptr >= '0') && (*ptr <= '9'))
		ptr += 1;

	if (strlen(ptr) == 0)
		multiplier = 0;
	if (strcmp(ptr, "k") == 0)
		multiplier = 1024;
	else if (strcmp(ptr, "m") == 0)
		multiplier = 1048576;
	else
	{
		fprintf(stderr,
		        "Invalid multiplier given for minimum free space (-f)!\n");
		usage();
		return(-1);
	}

	if (sscanf(argv, "%lu", &minFree) != 1)
	{
		fprintf(stderr,
		        "Unable to parse value given for minimum free space (-f)!\n");
		usage();
		return(-1);
	}

	minFree *= multiplier;

	return(0);
} 


INT16 getPatterns(int argc, char *argv[], int start)
{
	int i;

	i = start;
	while (i < argc)
	{
		if (argv[i][0] == '-')
		{
			fprintf(stderr, "Error parsing pattern!\n");
			usage();
			return(-1);
		}

		if (strlen(argv[i]) >= MAX_PATTERN_LEN)
		{
			fprintf(stderr, "Pattern exceeds maximum length!\n");
			usage();
			return(-1);
		}

		if (matchCnt == MAX_PATTERN)
		{
			fprintf(stderr, "Maximum number of patterns exceeded!\n");
			usage();
			return(-1);
		}

		if (regcomp(&match[matchCnt], argv[i],
		            REG_ICASE | REG_NOSUB | REG_NEWLINE) != 0)
		{
			fprintf(stderr, "Invalid pattern - \"%s\"!\n", argv[i]);
			usage();
			return(-1);
		}

		matchCnt += 1;

		i += 1;
	}

	return(0);
}


void usage(void)
{
	fprintf(stderr, "Usage: mtf [options] [pattern(s)]\n");
	fprintf(stderr, "    -v               verbose\n");
	fprintf(stderr, "    -V               very verbose\n");
	fprintf(stderr, "    -D               debug\n");
	fprintf(stderr, "    -l               list contents\n");
	fprintf(stderr, "    -b bytes         tape block size\n");
	fprintf(stderr, "    -d device        device to read from\n");
	fprintf(stderr, "    -s set           number of data set to read\n");
	fprintf(stderr, "    -u user          assign owner to all files/directories written\n");
	fprintf(stderr, "    -g group         assign group to all files/directories written\n");
	fprintf(stderr, "    -c [lower|upper] force the case of paths\n");
	fprintf(stderr, "    -o path          root path to write files to\n");
	fprintf(stderr, "    -F bytes[K|M]    maintain minimum free space of bytes;\n");
	fprintf(stderr, "                     a K or M suffix signifies kilobytes or megabytes\n");
	fprintf(stderr, "    pattern(s)       only read file paths that match regex pattern(s)\n");

	return;
}

