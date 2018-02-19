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

See mtf.c for version history, contributors, etc.

**
**	mtfread.c
**
**	functions for reading an MTF tape
**
*/


#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/param.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <sys/vfs.h>
#include <utime.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mtio.h>
#include <ctype.h>
#include <regex.h>
#include <grp.h>
#include <pwd.h>
#include "mtf.h"


extern char outPath[MAXPATHLEN + 1], curPath[MAXPATHLEN + 1];
extern int mtfd, errno;
extern UINT8 verbose, debug, list, forceCase;
extern UINT8 tBuffer[MAX_TAPE_BLOCK_SIZE];
extern UINT16 matchCnt;
extern UINT16 tapeBlockSize;
extern UINT32 minFree;
extern regex_t match[MAX_PATTERN];
extern uid_t owner;
extern gid_t group;


UINT8 compressPossible;
int filemark;
UINT16 flbSize = 0;
UINT16 remaining;
UINT16 setCompress;
UINT32 blockCnt;
struct mtop mt_cmd;

MTF_DB_HDR *dbHdr;
MTF_TAPE_BLK *tape;
MTF_SSET_BLK *sset;
MTF_VOLB_BLK *volb;
MTF_DIRB_BLK *dirb;
MTF_FILE_BLK *file;
MTF_CFIL_BLK *cfil;
MTF_ESPB_BLK *espb;
MTF_ESET_BLK *eset;
MTF_EOTM_BLK *eotm;
MTF_SFMB_BLK *sfmb;
MTF_STREAM_HDR *stream;

/* openMedia() reads the MTF tape header and prepares for reading the first   */
/* data set.                                                                  */

INT32 openMedia(void)
{
	INT32 result;

	if (verbose > 0) fprintf(stdout, "\nReading TAPE block...\n");

	blockCnt = 0;
	filemark = 0;

	result = readNextBlock(0); 
	if (result != 0)
	{
		fprintf(stderr, "Error reading first block!\n");
		return(-1);
	}

	dbHdr = (MTF_DB_HDR*) tBuffer;

	if (dbHdr->type != MTF_TAPE)
	{
		fprintf(stderr, "Error reading first block of tape!\n");
		return(-1);
	}

	tape = (MTF_TAPE_BLK*) dbHdr;

	if (readTapeBlock() != 0)
	{
		fprintf(stderr, "Error reading TAPE block!\n");
		return(-1);
	}

	return(0);
}


/* readDataSet() reads a MTF data set.                                        */

INT32 readDataSet(void)
{
	INT32 result;
	char *ptr;

	if (verbose > 0) fprintf(stdout, "\nReading SSET block...\n");

	filemark = 0;

	dbHdr = (MTF_DB_HDR*) tBuffer;

	if (dbHdr->type != MTF_SSET)
	{
		ptr = (char*) &sset->common.type;
		fprintf(stderr, "Unexpected descriptor block type \'%c%c%c%c\'!\n",
				*ptr, *(ptr + 1), *(ptr + 2), *(ptr + 3));
		return(-1);
	}

	sset = (MTF_SSET_BLK*) dbHdr;

	if (readStartOfSetBlock() != 0)
	{
		fprintf(stderr, "Error reading SSET block!\n");
		return(-1);
	}

	result = 0;
	while ((result == 0) && (filemark == 0))
	{
		dbHdr = (MTF_DB_HDR*) tBuffer;

		switch (dbHdr->type)
		{
			case MTF_VOLB:
				if (verbose > 0) fprintf(stdout, "\nReading VOLB block...\n");
				volb = (MTF_VOLB_BLK*) dbHdr;
				result = readVolumeBlock();
				break;

			case MTF_DIRB:
				if (verbose > 0) fprintf(stdout, "\nReading DIRB block...\n");
				dirb = (MTF_DIRB_BLK*) dbHdr;
				result = readDirectoryBlock();
				break;

			case MTF_FILE:
				if (verbose > 0) fprintf(stdout, "\nReading FILE block...\n");
				file = (MTF_FILE_BLK*) dbHdr;
				result = readFileBlock();
				break;

			case MTF_CFIL:
				if (verbose > 0) fprintf(stdout, "\nReading CFIL block...\n");
				cfil = (MTF_CFIL_BLK*) dbHdr;
				result = readCorruptObjectBlock();
				break;

			case MTF_ESPB:
				if (verbose > 0) fprintf(stdout, "\nReading ESPB block...\n");
				espb = (MTF_ESPB_BLK*) dbHdr;
				result = readEndOfSetPadBlock();
				break;

			case MTF_EOTM:
				if (verbose > 0) fprintf(stdout, "\nReading EOTM block...\n");
				eotm = (MTF_EOTM_BLK*) dbHdr;
				result = readEndOfTapeMarkerBlock();
				return(-1);
				break;

			case MTF_SFMB:
				if (verbose > 0) fprintf(stdout, "\nReading SFMB block...\n");
				sfmb = (MTF_SFMB_BLK*) dbHdr;
				result = readSoftFileMarkBlock();
				return(-1);
				break;

			default:
				if (verbose > 1)
				{
					ptr = (char*) &dbHdr->type;
					if ((isalnum(*ptr)) && (isalnum(*(ptr + 1))) &&
					    (isalnum(*(ptr + 2))) && (isalnum(*(ptr + 3))))
					{
						fprintf(stdout,
								"Skipping descriptor block type \'%c%c%c%c\'!\n",
								*ptr, *(ptr + 1), *(ptr + 2), *(ptr + 3));
					}
					else
					{
						fprintf(stdout,
						        "Looking for next  descriptor block...\n");
					}
				}

				result = readNextBlock(flbSize); 
				break;
		}
	}

	if (result < 0)
	{
		fprintf(stderr, "Error reading tape block!\n");
		return(-1);
	}
	else if (filemark == 0)
	{
		fprintf(stderr, "Error reading filemark!\n");
		return(-1);
	}
	else
	{
		result = readNextBlock(0); 
		if (result < 0)
		{
			fprintf(stderr, "Error reading tape block!\n");
			return(-1);
		}
	}

	return(0);
}


/* readEndOfDataSet() reads the MTF end of set block and prepares for reading */
/* the next data set. It returns 1 if there is none, 0 if there is.           */

INT32 readEndOfDataSet(void)
{
	char *ptr;
	INT32 result;

	if (verbose > 0) fprintf(stdout, "\nReading ESET block...\n");

	filemark = 0;

	dbHdr = (MTF_DB_HDR*) tBuffer;

	if (dbHdr->type != MTF_ESET)
	{
		ptr = (char*) &eset->common.type;
		fprintf(stderr, "Unexpected descriptor block type \'%c%c%c%c\'!\n",
				*ptr, *(ptr + 1), *(ptr + 2), *(ptr + 3));
		return(-1);
	}

	eset = (MTF_ESET_BLK*) dbHdr;

	result = readNextBlock(0); 
	while (result == 0)
	{
		result = readNextBlock(0); 
	}

	if (result < 0)
	{
		fprintf(stderr, "Error reading tape block!\n");
		return(-1);
	}

	if (filemark == 0)
	{
		fprintf(stderr, "Error reading filemark!\n");
		return(-1);
	}

	filemark = 0;

	result = readNextBlock(0); 
	if (result < 0)
	{
		fprintf(stderr, "Error reading tape block!\n");
		return(-1);
	}

	return(result);
}


/* readTapeBlock() reads an MTF TAPE descriptor block, advances past the      */
/* following filemark and reads the first tape block of the first data set.   */

INT32 readTapeBlock(void)
{
	INT32 result;
	char *ptr;

	if (tape->ver != 1)
	{
		fprintf(stderr, "Unexpected MTF major version!\n");
		return(-1);
	}

	if (verbose > 1)
	{
		fprintf(stdout, "Descriptor Block Attributes: %08lX\n", dbHdr->attr);
		fprintf(stdout, "TAPE Block Attributes: %08lX\n", tape->attr);
		fprintf(stdout, "Format Logical Address: %lu:%lu\n",
				dbHdr->fla.most, dbHdr->fla.least);
		fprintf(stdout, "Offset To First Event: %u\n", dbHdr->off);
		fprintf(stdout, "String Type: %u\n", dbHdr->strType);
		fprintf(stdout, "OS: %u\n", dbHdr->osId);

		fprintf(stdout, "MTF Major Version: %u\n", tape->ver);
		fprintf(stdout, "Format Logical Block Size: %u\n", tape->flbSize);
		fprintf(stdout, "Media Family ID: %lu\n", tape->famId);
		fprintf(stdout, "Media Sequence Number: %u\n", tape->seq);
		fprintf(stdout, "Software Vendor ID: %u\n", tape->vendorId);
	}

	if (verbose > 0)
	{
		ptr = getString(dbHdr->strType, tape->name.size,
						(UINT8*) tape + tape->name.offset);
		fprintf(stdout, "Media Name: %s\n", ptr);

		ptr = getString(dbHdr->strType, tape->desc.size,
						(UINT8*) tape + tape->desc.offset);
		fprintf(stdout, "Media Description: %s\n", ptr);

		ptr = getString(dbHdr->strType, tape->software.size,
						(UINT8*) tape + tape->software.offset);
		fprintf(stdout, "Software: %s\n", ptr);
	}

	flbSize = tape->flbSize;

	if (dbHdr->off < flbSize)
	{
		stream = (MTF_STREAM_HDR*) ((char*) tape + dbHdr->off);
		result = skipToNextBlock();
		if (result != 1)
		{
			fprintf(stderr, "Error traversing to end of descriptor block!\n");
			return(-1);
		}
	}
	else
	{
		result = readNextBlock(dbHdr->off);
		if (result != 1)
		{
			fprintf(stderr, "Error reading tape block!\n");
			return(-1);
		}
	}

	if (tapeBlockSize < flbSize)
	{
		if (readNextBlock(0) != 1)
		{
			fprintf(stderr, "Error reading tape block!\n");
			return(-1);
		}
	}

	/* read first block past filemark */
	if (readNextBlock(0) != 0)
	{
		fprintf(stderr, "Error reading tape block!\n");
		return(-1);
	}

	return(0);
}


INT32 readStartOfSetBlock(void)
{
	INT32 result;
	char *ptr;

	if (verbose > 1)
	{
		fprintf(stdout, "Descriptor Block Attributes: %08lX\n", dbHdr->attr);
		fprintf(stdout, "SSET Block Attributes: %08lX\n", sset->attr);
		fprintf(stdout, "Format Logical Address: %lu:%lu\n",
				dbHdr->fla.most, dbHdr->fla.least);
		fprintf(stdout, "Offset To First Event: %u\n", dbHdr->off);
		fprintf(stdout, "String Type: %u\n", dbHdr->strType);
		fprintf(stdout, "OS: %u\n", dbHdr->osId);

		fprintf(stdout, "MTF Minor Version: %u\n", sset->ver);
		fprintf(stdout, "Software Vendor: %u\n", sset->vendor);
		fprintf(stdout, "Software Major Version: %u\n", sset->major);
		fprintf(stdout, "Software Minor Version: %u\n", sset->minor);
		fprintf(stdout, "Data Set Number: %u\n", sset->num);
		fprintf(stdout, "Physical Block Address: %lu:%lu\n", sset->pba.most,
				sset->pba.least);
		fprintf(stdout, "Password Encryption: %u\n", sset->passEncrypt);
		fprintf(stdout, "Software Compression: %u\n", sset->softCompress);
	}

	if (verbose > 0)
	{
		ptr = getString(dbHdr->strType, sset->name.size,
						(UINT8*) sset + sset->name.offset);
		fprintf(stdout, "Data Set Name: %s\n", ptr);

		ptr = getString(dbHdr->strType, sset->desc.size,
						(UINT8*) sset + sset->desc.offset);
		fprintf(stdout, "Data Set Description %s\n", ptr);

		ptr = getString(dbHdr->strType, sset->user.size,
						(UINT8*) sset + sset->user.offset);
		fprintf(stdout, "User Name: %s\n", ptr);
	}

	setCompress = sset->softCompress;

	if (dbHdr->off < flbSize)
	{
		stream = (MTF_STREAM_HDR*) ((char*) sset + dbHdr->off);
		result = skipToNextBlock();
		if (result < 0)
		{
			fprintf(stderr, "Error traversing to end of descriptor block!\n");
			return(-1);
		}
	}
	else
	{
		result = readNextBlock(dbHdr->off);
		if (result < 0)
		{
			fprintf(stderr, "Error reading tape block!\n");
			return(-1);
		}
	}

	return(result);
}


INT32 readVolumeBlock(void)
{
	INT32 result;
	char *ptr;

	if (verbose > 1)
	{
		fprintf(stdout, "Descriptor Block Attributes: %08lX\n", dbHdr->attr);
		fprintf(stdout, "Format Logical Address: %lu:%lu\n",
				dbHdr->fla.most, dbHdr->fla.least);
		fprintf(stdout, "Offset To First Event: %u\n", dbHdr->off);
		fprintf(stdout, "String Type: %u\n", dbHdr->strType);
		fprintf(stdout, "OS: %u\n", dbHdr->osId);
	}

	if (verbose > 0)
	{
		ptr = getString(dbHdr->strType, volb->device.size,
						(UINT8*) volb + volb->device.offset);
		fprintf(stdout, "Device Name: %s\n", ptr);

		ptr = getString(dbHdr->strType, volb->volume.size,
						(UINT8*) volb + volb->volume.offset);
		fprintf(stdout, "Volume Name: %s\n", ptr);

		ptr = getString(dbHdr->strType, volb->machine.size,
						(UINT8*) volb + volb->machine.offset);
		fprintf(stdout, "Machine Name: %s\n", ptr);
	}

	if (dbHdr->off < flbSize)
	{
		stream = (MTF_STREAM_HDR*) ((char*) volb + dbHdr->off);
		result = skipToNextBlock();
		if (result < 0)
		{
			fprintf(stderr, "Error traversing to end of descriptor block!\n");
			return(-1);
		}
	}
	else
	{
		result = readNextBlock(dbHdr->off);
		if (result < 0)
		{
			fprintf(stderr, "Error reading tape block!\n");
			return(-1);
		}
	}

	return(result);
}


INT32 readDirectoryBlock(void)
{
	INT32 result;
	char *ptr, *ptr2, fullPath[MAXPATHLEN + 1], tmpPath[MAXPATHLEN + 1];
	struct stat sbuf;

	if (verbose > 1)
	{
		fprintf(stdout, "Descriptor Block Attributes: %08lX\n", dbHdr->attr);
		fprintf(stdout, "Format Logical Address: %lu:%lu\n",
				dbHdr->fla.most, dbHdr->fla.least);
		fprintf(stdout, "Offset To First Event: %u\n", dbHdr->off);
		fprintf(stdout, "String Type: %u\n", dbHdr->strType);
		fprintf(stdout, "OS: %u\n", dbHdr->osId);

		fprintf(stdout, "Directory ID: %lu\n", dirb->id);
	}

	if ((dirb->attr & MTF_DIR_PATH_IN_STREAM_BIT) == 0)
	{
		ptr = getString(dbHdr->strType, dirb->name.size,
		                (UINT8*) dirb + dirb->name.offset);

		strcpy(curPath, ptr);

		if (verbose > 0) fprintf(stdout, "Directory Name: %s\n", ptr);

		if (forceCase == CASE_LOWER)
			strlwr(curPath);
		else if (forceCase == CASE_UPPER)
			strupr(curPath);

		sprintf(fullPath, "%s/%s", outPath, curPath);

		if ((list == 0) && (matchCnt == 0))
		{
			if (stat(fullPath, &sbuf) != 0)
			{
				if (errno == ENOENT)
				{
					strcpy(tmpPath, fullPath);
					ptr = strrchr(tmpPath, '/');
					*ptr = '\0';
					ptr2 = ptr;

					result = (INT32) stat(tmpPath, &sbuf);
					while (result != 0)
					{
						if (debug > 0) printf("%s did not exist\n", tmpPath);
						if (errno != ENOENT) break;

						ptr = strrchr(tmpPath, '/');
						*ptr = '\0';

						result = (INT32) stat(tmpPath, &sbuf);
					}

					if (result != 0)
					{
						fprintf(stderr, "Error %d testing for status of %s!\n",
								errno, tmpPath);
						return(-1);
					}

					while (ptr < ptr2)
					{
						*ptr = '/';

						if (debug > 0) printf("creating %s...\n", tmpPath);

						if (mkdir(tmpPath, 0777) != 0)
						{
							fprintf(stderr,
									"Unable to create directory %s!\n",
									tmpPath);
							return(-1);
						}

						if ((owner != -1) || (group != -1))
						{
							if (chown(tmpPath, owner, group) != 0)
							{
								fprintf(stderr,
										"Error %d setting owner/group of %s!\n",
										errno, tmpPath);
							}
						}

						while (*ptr != '\0') ptr += 1;
					}

					sprintf(fullPath, "%s/%s", outPath, curPath);
				}
				else
				{
					fprintf(stderr, "Error %d testing for status of %s!\n",
					        errno, fullPath);
					return(-1);
				}
			}
			else
			{
				if ((sbuf.st_mode & S_IFDIR) == 0)
				{
					fprintf(stderr, "%s is not a directory!\n", fullPath);
					return(-1);
				}

			} /* if (stat(fullPath, &sbuf) != 0) */

			if (verbose > 0)
				fprintf(stdout, "Current path changed to %s\n", fullPath);

		} /* if ((list == 0) && (matchCnt == 0)) */
	}
	else
	{
		/* not implemented */
		fprintf(stderr, "Reading from stream not implemented!\n");
		return(-1);
	}

	if (dbHdr->off < flbSize)
	{
		stream = (MTF_STREAM_HDR*) ((char*) dirb + dbHdr->off);
		result = skipToNextBlock();
		if (result < 0)
		{
			fprintf(stderr, "Error traversing to end of descriptor block!\n");
			return(-1);
		}
	}
	else
	{
		result = readNextBlock(dbHdr->off);
		if (result != 0)
		{
			fprintf(stderr, "Error reading tape block!\n");
			return(-1);
		}
	}

	return(result);
}


INT32 readFileBlock(void)
{
	INT32 result;
	char *ptr, *ptr2, filePath[MAXPATHLEN + 1], fullPath[MAXPATHLEN + 1];
	char tmpPath[MAXPATHLEN + 1];
	int i, output;
	struct tm tbuf;
	struct utimbuf utbuf;
	UINT32 threshold;
	struct statfs fsbuf;
	struct stat sbuf;

	if (verbose > 1)
	{
		fprintf(stdout, "Descriptor Block Attributes: %08lX\n", dbHdr->attr);
		fprintf(stdout, "Format Logical Address: %lu:%lu\n",
				dbHdr->fla.most, dbHdr->fla.least);
		fprintf(stdout, "Offset To First Event: %u\n", dbHdr->off);
		fprintf(stdout, "String Type: %u\n", dbHdr->strType);
		fprintf(stdout, "OS: %u\n", dbHdr->osId);

		fprintf(stdout, "Directory ID: %lu\n", file->dirId);
		fprintf(stdout, "File ID: %lu\n", file->id);

		fprintf(stdout, "Modification Date: %02u:%02u:%02u %02u/%02u/%04u\n",
		        MTF_HOUR(file->mod), MTF_MINUTE(file->mod),
				MTF_SECOND(file->mod), MTF_MONTH(file->mod), MTF_DAY(file->mod),
				MTF_YEAR(file->mod));
		fprintf(stdout, "Creation Date: %02u:%02u:%02u %02u/%02u/%04u\n",
		        MTF_HOUR(file->create), MTF_MINUTE(file->create),
				MTF_SECOND(file->create), MTF_MONTH(file->create),
				MTF_DAY(file->create), MTF_YEAR(file->create));
		fprintf(stdout, "Backup Date: %02u:%02u:%02u %02u/%02u/%04u\n",
		        MTF_HOUR(file->backup), MTF_MINUTE(file->backup),
				MTF_SECOND(file->backup), MTF_MONTH(file->backup),
				MTF_DAY(file->backup), MTF_YEAR(file->backup));
		fprintf(stdout, "Access Date: %02u:%02u:%02u %02u/%02u/%04u\n",
		        MTF_HOUR(file->access), MTF_MINUTE(file->access),
				MTF_SECOND(file->access), MTF_MONTH(file->access),
				MTF_DAY(file->access), MTF_YEAR(file->access));
	}

	tbuf.tm_sec = MTF_SECOND(file->mod);
	tbuf.tm_min = MTF_MINUTE(file->mod);
	tbuf.tm_hour = MTF_HOUR(file->mod);
	tbuf.tm_mday = MTF_DAY(file->mod);
	tbuf.tm_mon = MTF_MONTH(file->mod) - 1;
	tbuf.tm_year = MTF_YEAR(file->mod) - 1900;

	utbuf.modtime = mktime(&tbuf);

	tbuf.tm_sec = MTF_SECOND(file->access);
	tbuf.tm_min = MTF_MINUTE(file->access);
	tbuf.tm_hour = MTF_HOUR(file->access);
	tbuf.tm_mday = MTF_DAY(file->access);
	tbuf.tm_mon = MTF_MONTH(file->access) - 1;
	tbuf.tm_year = MTF_YEAR(file->access) - 1900;

	utbuf.actime = mktime(&tbuf);

	if (dbHdr->attr & MTF_COMPRESSION)
		compressPossible = 1;
	else
		compressPossible = 0;

	if ((file->attr & MTF_FILE_CORRUPT_BIT) != 0)
	{
		if (verbose > 0) fprintf(stdout, "File is corrupted... skipping!\n");

		stream = (MTF_STREAM_HDR*) ((char*) file + dbHdr->off);
		result = skipToNextBlock();
		if (result < 0)
		{
			fprintf(stderr,
					"Error traversing to end of %s!\n", fullPath);
			return(-1);
		}

		return(0);
	}

	if ((file->attr & MTF_FILE_NAME_IN_STREAM_BIT) == 0)
	{
		ptr = getString(dbHdr->strType, file->name.size,
						(UINT8*) file + file->name.offset);

		if (verbose > 0) fprintf(stdout, "File Name: %s\n", ptr);
	}
	else
	{
		/* not implemented */
		fprintf(stderr, "Reading from stream not implemented!\n");
		return(-1);
	}

	sprintf(filePath, "%s%s", curPath, ptr);

	if (forceCase == CASE_LOWER)
		strlwr(filePath);
	else if (forceCase == CASE_UPPER)
		strupr(filePath);

	sprintf(fullPath, "%s/%s", outPath, filePath);

	if (matchCnt > 0)
	{
		i = 0;
		while ((i >= 0) && (i < matchCnt))
		{
			if (regexec(&match[i], filePath, 0, NULL, 0) != 0)
				i += 1;
			else
				i = -1;
		}
		
		if (i >= 0)
		{
			if (verbose > 0)
				fprintf(stdout, "%s does not match any patterns... skipping!\n",
				        filePath);

			stream = (MTF_STREAM_HDR*) ((char*) file + dbHdr->off);
			result = skipToNextBlock();
			if (result < 0)
			{
				fprintf(stderr,
				        "Error traversing to end of %s!\n", fullPath);
				return(-1);
			}

			return(0);
		}
	}

	if (list == 0)
	{
		if (verbose > 0)
			fprintf(stdout, "File will be written to %s\n", fullPath);
		else
			fprintf(stdout, "%s\n", fullPath);
		
		if (matchCnt > 0)
		{
			strcpy(tmpPath, fullPath);

			ptr = strrchr(tmpPath, '/');
			*ptr = '\0';
			ptr2 = ptr;
			
			result = (INT32) stat(tmpPath, &sbuf);
			while (result != 0)
			{
				if (errno != ENOENT) break;

				ptr = strrchr(tmpPath, '/');
				*ptr = '\0';

				result = (INT32) stat(tmpPath, &sbuf);
			}

			if (result != 0)
			{
				fprintf(stderr, "Error %d testing for status of %s!\n",
						errno, tmpPath);
				return(-1);
			}

			while (ptr < ptr2)
			{
				*ptr = '/';

				if (mkdir(tmpPath, 0777) != 0)
				{
					fprintf(stderr, "Unable to create directory %s!\n",
							tmpPath);
					return(-1);
				}

				if ((owner != -1) || (group != -1))
				{
					if (chown(tmpPath, owner, group) != 0)
					{
						fprintf(stderr, "Error %d setting owner/group of %s!\n",
								errno, filePath);
					}
				}

				while (*ptr != '\0')
					ptr += 1;
			}
		}

		if (minFree != 0)
		{
			strcpy(filePath, fullPath);
			ptr = strrchr(filePath, '/');
			*ptr = '\0';

			if (statfs(filePath, &fsbuf) != 0)
			{
				if (debug > 0) printf("filePath=%s\n", filePath);
				fprintf(stderr, "Error testing for free space!\n");
				return(-1);
			}

			threshold = (minFree + (UINT32) fsbuf.f_bsize - 1) /
				(UINT32) fsbuf.f_bsize;

			if (debug > 0)
				printf("threshold=%lu avail=%ld\n", threshold, fsbuf.f_bavail);

			while ((UINT32) fsbuf.f_bavail < threshold)
			{
				fprintf(stderr, "Free space is only %ld bytes!\n",
						fsbuf.f_bavail * fsbuf.f_bsize);
				sleep(60);

				if (statfs(filePath, &fsbuf) != 0)
				{
					fprintf(stderr, "Error testing for free space!\n");
					return(-1);
				}

				if (debug > 0) printf("avail=%ld\n", fsbuf.f_bavail);
			}
		}

		output = open(fullPath, O_WRONLY | O_TRUNC | O_CREAT,
					  S_IRWXU | S_IRWXG | S_IRWXO);
		if (output == -1)
		{
			fprintf(stderr, "Error %d opening/creating %s for writing!\n",
					errno, fullPath);
			return(-1);
		}

		if ((owner != -1) || (group != -1))
		{
			if (fchown(output, owner, group) != 0)
			{
				fprintf(stderr, "Error %d setting owner/group of %s!\n",
						errno, fullPath);
			}
		}
	}
	else
	{
		fprintf(stdout, "%s\n", fullPath);
	}

	stream = (MTF_STREAM_HDR*) ((char*) file + dbHdr->off);

	if (list == 0)
	{
		while ((stream->id != MTF_STAN) && (stream->id != MTF_SPAD))
		{
			result = skipOverStream();
			if (result < 0)
			{
				fprintf(stderr, "Error traversing stream!\n");
				return(-1);
			}

			stream = (MTF_STREAM_HDR*) &tBuffer[result];
		}

		if (stream->id == MTF_STAN)
		{
			if (verbose > 1) fprintf(stdout, "Reading STAN stream...\n");

			result = writeData(output);
			if (result < 0)
			{
				fprintf(stderr, "Error writing stream to file!\n");
				return(-1);
			}

			if (debug > 0)
				printf("writeData() returned %ld\n", result);

			while ((result % flbSize) != 0)
			{
				stream = (MTF_STREAM_HDR*) &tBuffer[result];
				result = skipOverStream();
				if (result < 0)
				{
					fprintf(stderr, "Error traversing stream!\n");
					return(-1);
				}

				if (debug > 0)
					printf("skipOverStream() returned %ld\n", result);
			}
		}
		else
		{
			result = skipOverStream();
			if (result < 0)
			{
				fprintf(stderr, "Error traversing stream!\n");
				return(-1);
			}
		}
	}
	else
	{
		result = skipToNextBlock();
		if (result < 0)
		{
			fprintf(stderr, "Error traversing to end %s!\n", fullPath);
			return(-1);
		}
	}

	if (list == 0)
	{
		if (close(output) != 0)
		{
			fprintf(stderr,
					"Error %d closing %s!\n", errno, fullPath);
			return(-1);
		}

		result = utime(fullPath, &utbuf);
		if (result != 0)
		{
			fprintf(stderr,
					"Error %d setting modification/access time of %s!\n",
					errno, fullPath);

			if (debug > 0)
			{
				printf("Modification Date: %02u:%02u:%02u %02u/%02u/%04u\n",
						MTF_HOUR(file->mod), MTF_MINUTE(file->mod),
						MTF_SECOND(file->mod), MTF_MONTH(file->mod),
						MTF_DAY(file->mod),
						MTF_YEAR(file->mod));
				printf("Access Date: %02u:%02u:%02u %02u/%02u/%04u\n",
						MTF_HOUR(file->access), MTF_MINUTE(file->access),
						MTF_SECOND(file->access), MTF_MONTH(file->access),
						MTF_DAY(file->access), MTF_YEAR(file->access));
			}
		}
	}

	return(0);
}


INT32 readCorruptObjectBlock(void)
{
	INT32 result;

	if (verbose > 1)
	{
		fprintf(stdout, "Descriptor Block Attributes: %08lX\n", dbHdr->attr);
		fprintf(stdout, "Format Logical Address: %lu:%lu\n",
				dbHdr->fla.most, dbHdr->fla.least);
		fprintf(stdout, "Offset To First Event: %u\n", dbHdr->off);
		fprintf(stdout, "String Type: %u\n", dbHdr->strType);
		fprintf(stdout, "OS: %u\n", dbHdr->osId);
	}

	if (dbHdr->off < flbSize)
	{
		stream = (MTF_STREAM_HDR*) ((char*) dirb + dbHdr->off);
		result = skipToNextBlock();
		if (result < 0)
		{
			fprintf(stderr, "Error traversing to end of descriptor block!\n");
			return(-1);
		}
	}
	else
	{
		result = readNextBlock(dbHdr->off);
		if (result < 0)
		{
			fprintf(stderr, "Error reading tape block!\n");
			return(-1);
		}
	}

	return(result);
}


INT32 readEndOfSetPadBlock(void)
{
	INT32 result;

	if (verbose > 1)
	{
		fprintf(stdout, "Descriptor Block Attributes: %08lX\n", dbHdr->attr);
		fprintf(stdout, "Format Logical Address: %lu:%lu\n",
				dbHdr->fla.most, dbHdr->fla.least);
		fprintf(stdout, "Offset To First Event: %u\n", dbHdr->off);
		fprintf(stdout, "String Type: %u\n", dbHdr->strType);
		fprintf(stdout, "OS: %u\n", dbHdr->osId);
	}

	if (dbHdr->off < flbSize)
	{
		stream = (MTF_STREAM_HDR*) ((char*) dirb + dbHdr->off);
		result = skipToNextBlock();
		if (result < 0)
		{
			fprintf(stderr, "Error traversing to end of descriptor block!\n");
			return(-1);
		}
	}
	else
	{
		result = readNextBlock(dbHdr->off);
		if (result < 0)
		{
			fprintf(stderr, "Error reading tape block!\n");
			return(-1);
		}
	}

	return(result);
}


INT32 readEndOfSetBlock(void)
{
	if (verbose > 1)
	{
		fprintf(stdout, "Descriptor Block Attributes: %08lX\n", dbHdr->attr);
		fprintf(stdout, "Format Logical Address: %lu:%lu\n",
				dbHdr->fla.most, dbHdr->fla.least);
		fprintf(stdout, "Offset To First Event: %u\n", dbHdr->off);
		fprintf(stdout, "String Type: %u\n", dbHdr->strType);
		fprintf(stdout, "OS: %u\n", dbHdr->osId);
	}

	if (dbHdr->off < flbSize)
	{
		stream = (MTF_STREAM_HDR*) ((char*) dirb + dbHdr->off);
		if (skipToNextBlock() != 0)
		{
			fprintf(stderr, "Error traversing to end of descriptor block!\n");
			return(-1);
		}
	}
	else
	{
		if (readNextBlock(dbHdr->off) != 1)
		{
			fprintf(stderr, "Error reading tape block!\n");
			return(-1);
		}
	}

	return(0);
}


INT32 readEndOfTapeMarkerBlock(void)
{
	if (verbose > 1)
	{
		fprintf(stdout, "Descriptor Block Attributes: %08lX\n", dbHdr->attr);
		fprintf(stdout, "Format Logical Address: %lu:%lu\n",
				dbHdr->fla.most, dbHdr->fla.least);
		fprintf(stdout, "Offset To First Event: %u\n", dbHdr->off);
		fprintf(stdout, "String Type: %u\n", dbHdr->strType);
		fprintf(stdout, "OS: %u\n", dbHdr->osId);
	}

	if (dbHdr->off < flbSize)
	{
		stream = (MTF_STREAM_HDR*) ((char*) dirb + dbHdr->off);
		if (skipToNextBlock() != 0)
		{
			fprintf(stderr, "Error traversing to end of descriptor block!\n");
			return(-1);
		}
	}
	else
	{
		if (readNextBlock(dbHdr->off) != 1)
		{
			fprintf(stderr, "Error reading tape block!\n");
			return(-1);
		}
	}
	
	return(0);
}


INT32 readSoftFileMarkBlock(void)
{
	if (verbose > 1)
	{
		fprintf(stdout, "Descriptor Block Attributes: %08lX\n", dbHdr->attr);
		fprintf(stdout, "Format Logical Address: %lu:%lu\n",
				dbHdr->fla.most, dbHdr->fla.least);
		fprintf(stdout, "Offset To First Event: %u\n", dbHdr->off);
		fprintf(stdout, "String Type: %u\n", dbHdr->strType);
		fprintf(stdout, "OS: %u\n", dbHdr->osId);
	}

	if (dbHdr->off < flbSize)
	{
		stream = (MTF_STREAM_HDR*) ((char*) dirb + dbHdr->off);
		if (skipToNextBlock() != 0)
		{
			fprintf(stderr, "Error traversing to end of descriptor block!\n");
			return(-1);
		}
	}
	else
	{
		if (readNextBlock(dbHdr->off) != 1)
		{
			fprintf(stderr, "Error reading tape block!\n");
			return(-1);
		}
	}
	
	return(0);
}


/* readNextBlock() keeps track of how many logical blocks have been used from */
/* the last tape block read. If needed, it reads another tape block in. The   */
/* global variable, remaining, is used to track how much data remains in the  */
/* block buffer.                                                              */

INT32 readNextBlock(UINT16 advance)
{
	ssize_t result;
	struct mtop op;
	struct mtget get;

	if (debug > 0) printf("advance=%u remaining=%u\n", advance, remaining);

	if ((advance == 0) || (remaining == 0) || (advance == remaining))
	{
		if (debug > 0)
		{
			op.mt_op = MTNOP;
			op.mt_count = 0;

			if (ioctl(mtfd, MTIOCTOP, &op) != 0)
			{
				fprintf(stderr, "Error returned by MTIOCTOP!\n");
				return(-1);
			}

			if (ioctl(mtfd, MTIOCGET, &get) != 0)
			{
				fprintf(stderr, "Error returned by MTIOCGET!\n");
				return(-1);
			}

			printf("tape file no. %u\n", get.mt_fileno);
			printf("tape block no. %u\n", get.mt_blkno);
		}

		remaining = 0;

		if (flbSize == 0) /* first block of tape, don't know flbSize yet */
		{
			if (tapeBlockSize == 0)
				result = read(mtfd, tBuffer, MAX_TAPE_BLOCK_SIZE);
			else
				result = read(mtfd, tBuffer, tapeBlockSize);

			if (result < 0)
			{
				fprintf(stderr, "Error reading tape!\n");
				return(-1);
			}

			remaining = result;

			if (tapeBlockSize == 0)
			{
				tapeBlockSize = result;

				if (verbose > 0)
					fprintf(stdout, "Detected %u-byte tape block size.\n",
					        tapeBlockSize);
			}
		}
		else
		{
			result = read(mtfd, &tBuffer[remaining], tapeBlockSize);

			if (result < 0)
			{
				fprintf(stderr, "Error reading tape!\n");
				return(-1);
			}
			else if (result == 0)
			{
				if (verbose > 0) fprintf(stdout, "Read filemark.\n");

				filemark = 1;
				remaining = 0;

				return(1);
			}

			remaining = result;

			while (remaining < flbSize)
			{
				result = read(mtfd, &tBuffer[remaining], tapeBlockSize);

				if (result < 0)
				{
					fprintf(stderr, "Error reading tape!\n");
					return(-1);
				}
				else if (result == 0)
				{
					/*
					 * tapeBlockSize is less than flbSize and an EOF was
					 * found after the first tape block. ALK 2000-07-17
					 */

					if (verbose > 0) fprintf(stdout, "Read filemark.\n");

					filemark = 1;
					remaining = 0;
					
					return(1);
				}


				remaining += result;
			}
		}

		if (debug > 0)
		{
			printf("remaining=%u\n", remaining);
		dump("lastblock.dmp");
	}

		blockCnt += 1;
	}
	else if (advance > remaining)
	{
		while (advance > remaining)
		{
			if (debug > 0) printf("reading %u bytes...\n", tapeBlockSize);

			result = read(mtfd, &tBuffer[remaining], tapeBlockSize);

			if (result < 0)
			{
				fprintf(stderr, "Error reading tape!\n");
				return(-1);
			}
			else if (result == 0)
			{
				if (verbose > 0) fprintf(stdout, "Read filemark.\n");

				filemark = 1;
				remaining = 0;

				return(1);
			}

			remaining += result;
		}

		if (debug > 0) printf("remaining=%u\n", remaining);
	}
	else
	{
		if (advance % flbSize != 0)
		{
			fprintf(stderr, "Illegal read request (%u bytes)!\n", advance);
			return(-1);
		}

		if (debug > 0) printf("advancing %u bytes...\n", advance);

		remaining -= advance;
		memmove(tBuffer, tBuffer + advance, remaining);
	}

	return(0);
}


/* skipToNextBlock() skips over all streams until it finds a SPAD stream and  */
/* then skips over it. The next descriptor block will immediately follow the  */
/* SPAD stream. The tape read buffer will be advanced to this position. This  */
/* function assumes the global variable 'stream' has been set to point to a   */
/* stream header.                                                             */

INT32 skipToNextBlock(void)
{
	INT32 offset;

	while ((stream->id != MTF_STAN) && (stream->id != MTF_SPAD))
	{
		offset = skipOverStream();
		if (offset < 0)
		{
			fprintf(stderr, "Error traversing stream!\n");
			return(-1);
		}

		stream = (MTF_STREAM_HDR*) &tBuffer[offset];
	}

	if (stream->id == MTF_STAN)
	{
		offset = skipOverStream();
		if (offset < 0)
		{
			fprintf(stderr, "Error traversing stream!\n");
			return(-1);
		}

		stream = (MTF_STREAM_HDR*) &tBuffer[offset];

		if ((offset % flbSize) != 0)
		{
			while (stream->id != MTF_SPAD)
			{
				offset = skipOverStream();
				if (offset < 0)
				{
					fprintf(stderr, "Error traversing stream!\n");
					return(-1);
				}

				stream = (MTF_STREAM_HDR*) &tBuffer[offset];
			}

			offset = skipOverStream();
			if (offset < 0)
			{
				fprintf(stderr, "Error traversing stream!\n");
				return(-1);
			}
		}
	}
	else
	{
		offset = skipOverStream();
		if (offset < 0)
		{
			fprintf(stderr, "Error traversing stream!\n");
			return(-1);
		}
	}

	if (offset != 0)
	{
		fprintf(stderr, "Error skipping stream!\n");
		return(-1);
	}

	return(0);
}


/* skipOverStream() skips over the current stream. It returns the number of   *//* bytes to skip in the last tape block read.                                 */

INT32 skipOverStream(void)
{
	char *ptr;
	INT32 result;
	UINT32 offset, bytes;
	MTF_STREAM_HDR hdr;

	offset = (char*) stream - (char*) tBuffer;
	offset += sizeof(MTF_STREAM_HDR);

	if (debug > 0) printf("offset=%lu remaining=%u\n", offset, remaining);

	if (offset >= remaining)
	{
		bytes = remaining - (offset - sizeof(MTF_STREAM_HDR));

		memcpy(&hdr, stream, bytes);

		offset -= remaining;

		result = readNextBlock(remaining); 
		if (result != 0)
		{
			fprintf(stderr, "Error reading tape block!\n");
			return(-1);
		}

		if (offset > 0)
		{
			ptr = (char*) &hdr;
			ptr += bytes;

			memcpy(ptr, tBuffer, offset);
		}
	}
	else
	{
		memcpy(&hdr, stream, sizeof(MTF_STREAM_HDR));
	}

	if (verbose > 1)
	{
		ptr = (char*) &hdr.id;
		if ((isalnum(*ptr)) && (isalnum(*(ptr + 1))) &&
			(isalnum(*(ptr + 2))) && (isalnum(*(ptr + 3))))
		{
			fprintf(stdout, "Skipping %c%c%c%c stream...\n",
					*ptr, *(ptr + 1), *(ptr + 2), *(ptr + 3));
		}
		else
		{
			fprintf(stderr, "Error seeking next stream!\n");
			return(-1);
		}

		fprintf(stdout, "System Attributes: %04X\n", hdr.sysAttr);
		fprintf(stdout, "Media Attributes: %04X\n", hdr.mediaAttr);
		fprintf(stdout, "Stream Length: %lu:%lu\n", hdr.length.most,
				stream->length.least);
		fprintf(stdout, "Data Encryption: %u\n", hdr.encrypt);
		fprintf(stdout, "Data Compression: %u\n", hdr.compress);
	}

	if (debug > 0) printf("remaining=%u\n", remaining);

	if (hdr.length.most == 0)
	{
		bytes = min(hdr.length.least, remaining - offset);
	}
	else
	{
		bytes = remaining - offset;
	}

	if (debug > 0)
		printf("skipping %lu bytes from offset %lu...\n", bytes, offset);

	decrement64(&hdr.length, bytes);

	if (debug > 0)
		printf("%lu:%lu not yet skipped\n", hdr.length.most, hdr.length.least);

	if ((hdr.length.most == 0) && (hdr.length.least == 0))
	{
		offset += bytes;
	}
	else
	{
		while ((hdr.length.most > 0) || (hdr.length.least > 0))
		{
			result = readNextBlock(0); 
			if (result != 0)
			{
				fprintf(stderr, "Error reading tape block!\n");
				return(-1);
			}

			if (hdr.length.most == 0)
			{
				bytes = min(hdr.length.least, remaining);
			}
			else
			{
				bytes = remaining;
			}

			if (debug > 0)
				printf("skipping %lu bytes from offset 0...\n", bytes);

			decrement64(&hdr.length, bytes);

			if (debug > 0)
				printf("%lu:%lu not yet skipped\n", hdr.length.most,
				       hdr.length.least);
		}

		offset = bytes;
	}

	if ((offset % 4) != 0)
		offset += 4 - (offset % 4);

	if (offset >= flbSize)
	{
		bytes = offset;
		bytes -= bytes % flbSize;
		offset -= bytes;

		result = readNextBlock(bytes); 
		if (result < 0)
		{
			fprintf(stderr, "Error reading tape block!\n");
			return(-1);
		}
	}

	if (debug > 0) printf("returning %ld\n", offset);

	return(offset);
}


/* writeData() reads the contents of a the current stream (which should be a  */
/* STAN stream) and writes it to a file. It returns the number of bytes that  */
/* were written from the last tape block read.                                */

INT32 writeData(int file)
{
	char *ptr;
	INT32 result;
	UINT32 offset, bytes;
	MTF_STREAM_HDR hdr;

	offset = (char*) stream - (char*) tBuffer;
	offset += sizeof(MTF_STREAM_HDR);

	if (debug > 0) printf("offset=%lu remaining=%u\n", offset, remaining);

	if (offset >= remaining)
	{
		bytes = remaining - (offset - sizeof(MTF_STREAM_HDR));

		memcpy(&hdr, stream, bytes);

		offset -= remaining;

		result = readNextBlock(0); 
		if (result != 0)
		{
			fprintf(stderr, "Error reading tape block!\n");
			return(-1);
		}

		if (offset > 0)
		{
			ptr = (char*) &hdr;
			ptr += bytes;

			memcpy(ptr, tBuffer, offset);
		}
	}
	else
	{
		memcpy(&hdr, stream, sizeof(MTF_STREAM_HDR));
	}

	if (verbose > 1)
	{
		fprintf(stdout, "System Attributes: %04X\n", hdr.sysAttr);
		fprintf(stdout, "Media Attributes: %04X\n", hdr.mediaAttr);
		fprintf(stdout, "Stream Length: %lu:%lu\n", hdr.length.most,
				hdr.length.least);
		fprintf(stdout, "Data Encryption: %u\n", hdr.encrypt);
		fprintf(stdout, "Data Compression: %u\n", hdr.compress);
	}

	if ((compressPossible) && ((hdr.mediaAttr & MTF_STREAM_COMPRESSED) != 0))
	{
		fprintf(stderr, "Compressed streams are not supported!\n");
		return(-1);
	}

	if ((hdr.sysAttr & MTF_STREAM_IS_SPARSE) != 0)
	{
		fprintf(stderr, "Sparse streams are not supported!\n");
		return(-1);
	}

	if (debug > 0) printf("remaining=%u\n", remaining);

	if (hdr.length.most == 0)
	{
		bytes = min(hdr.length.least, remaining - offset);
	}
	else
	{
		bytes = remaining - offset;
	}

	if (debug > 0)
		printf("writing %lu bytes from offset %lu...\n", bytes, offset);

	if (write(file, &tBuffer[offset], bytes) != bytes)
	{
		fprintf(stderr, "Error writing file!\n");
		return(-1);
	}

	decrement64(&hdr.length, bytes);

	if (debug > 0)
		printf("%lu:%lu not yet written\n", hdr.length.most, hdr.length.least);

	if ((hdr.length.most == 0) && (hdr.length.least == 0))
	{
		offset += bytes;
	}
	else
	{
		while ((hdr.length.most > 0) || (hdr.length.least > 0))
		{
			result = readNextBlock(0); 
			if (result != 0)
			{
				fprintf(stderr, "Error reading tape block!\n");
				return(-1);
			}

			if (hdr.length.most == 0)
			{
				bytes = min(hdr.length.least, remaining);
			}
			else
			{
				bytes = remaining;
			}

			if (debug > 0)
				printf("writing %lu bytes from offset 0...\n", bytes);

			if (write(file, tBuffer, bytes) != bytes)
			{
				fprintf(stderr, "Error writing file!\n");
				return(-1);
			}

			decrement64(&hdr.length, bytes);

			if (debug > 0)
				printf("%lu:%lu not yet written\n", hdr.length.most,
				       hdr.length.least);
		}

		offset = bytes;
	}

	if ((offset % 4) != 0)
		offset += 4 - (offset % 4);

	if (offset >= flbSize)
	{
		bytes = offset;
		bytes -= bytes % flbSize;
		offset -= bytes;

		result = readNextBlock(bytes); 
		if (result < 0)
		{
			fprintf(stderr, "Error reading tape block!\n");
			return(-1);
		}
	}

	if (debug > 0) printf("returning %ld\n", offset);

	return(offset);
}


/* getString() fetches a string stored after a descriptor block. If the       */
/* string is type 2, it is converted from unicode to ascii. Also, any nulls   */
/* are replaced with '/' characters. A pointer to the string is returned. If  */
/* the string is longer than the maximum supported, an empty string is        */
/* returned.                                                                  */

char *getString(UINT8 type, UINT16 length, UINT8 *addr)
{
	char *ptr1, *ptr2;
	
	static char buffer[MAXPATHLEN + 1]; 

	if ((length == 0) || (length > MAXPATHLEN))
	{
		buffer[0] = '\0';
	}
	else
	{
		memcpy(buffer, addr, length);
		buffer[length] = '\0';

		if (type == 2)
		{
			length -= 1;

			ptr1 = &buffer[1];
			ptr2 = &buffer[2];

			while (ptr2 - buffer < length)
			{
				*ptr1 = *ptr2;

				if (*ptr1 == '\0')
					*ptr1 = '/';

				ptr1 += 1;
				ptr2 += 2;
			}

			*ptr1 = '\0';
		}
	}

	return(buffer);
}
