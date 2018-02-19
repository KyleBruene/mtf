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

See mtf.c for version history, contibutors, etc.


**
**	mtf.h
**
**	defines, types for the Microsoft Tape Format
**	prototypes for functions in mtfread.c and mtfutil.c
**
*/


#pragma pack(1)


#define MIN_TAPE_BLOCK_SIZE 512
#define MAX_TAPE_BLOCK_SIZE 65536
#define MAX_PRINT_STRING 100
#define MAX_PATTERN_LEN 100
#define MAX_PATTERN 20

#define CASE_SENSITIVE 0
#define CASE_LOWER 1
#define CASE_UPPER 2

#define min(a,b) ((a) < (b) ? (a) : (b))
#define max(a,b) ((a) < (b) ? (b) : (a))


typedef unsigned short BOOLEAN;
typedef char INT8;
typedef unsigned char UINT8;
typedef short INT16;
typedef unsigned short UINT16;
typedef long INT32;
typedef unsigned long UINT32;

typedef struct
{
	UINT32	least;	/* least significant 32 bits */
	UINT32	most;	/* most significant 32 bits */
} UINT64;

/* pointer to non-fixed length information */
typedef struct
{
	UINT16	size;	/* size of referenced field */
	UINT16	offset; /* offset to start of field from start of structure */
} MTF_TAPE_ADDRESS;

/* storage of date and time */
typedef UINT8 MTF_DATE_TIME[5];	/* 14 bit year, 4 bit month, 5 bit day, */
                                /* 5 bit hour, 6 bit minute, 6 bit second */

/* macros for reading the MTF_DATE_TIME type */
#define MTF_YEAR(X) (UINT16) ((X[0] << 6) | (X[1] >> 2))
#define MTF_MONTH(X) (UINT8) (((X[1] & 0x03) << 2) | ((X[2] & 0xC0) >> 6))
#define MTF_DAY(X) (UINT8) ((X[2] & 0x3E) >> 1)
#define MTF_HOUR(X) (UINT8) (((X[2] & 0x01) << 4) | ((X[3] & 0xF0) >> 4))
#define MTF_MINUTE(X) (UINT8) (((X[3] & 0x0F) << 2) | ((X[4] & 0xC0) >> 6))
#define MTF_SECOND(X) (UINT8) (X[4] & 0x3F)

/* common descriptor block header */
typedef struct
{
	UINT32				type;		/* DBLK type */
	UINT32				attr;		/* block attributes */
	UINT16				off;		/* offset to first event */
	UINT8				osId;		/* OS ID */
	UINT8				osVer;		/* OS version */
	UINT64				size;		/* displayable size */
	UINT64				fla;		/* format logical address */
	UINT16 				mbc;		/* reserved for MBC */
	UINT8			rsv1[6];		/* reserved for future use */
	UINT32				cbId;		/* control block ID */
	UINT8			rsv2[4];		/* reserved for future use */
	MTF_TAPE_ADDRESS	osData;		/* OS-specific data */
	UINT8				strType;	/* string type */
	UINT8				rsv3;		/* reserved for future use */
	UINT16				check;		/* header checksum */
} MTF_DB_HDR;

/* values for MTF_DB_HDR.type field */
#define MTF_TAPE 0x45504154
#define MTF_SSET 0x54455353
#define MTF_VOLB 0x424C4F56
#define MTF_DIRB 0x42524944
#define MTF_FILE 0x454C4946
#define MTF_CFIL 0x4C494643
#define MTF_ESPB 0x42505345
#define MTF_ESET 0x54455345
#define MTF_EOTM 0x4D544F45
#define MTF_SFMB 0x424D4653

/* bit masks for MTF_DB_HDR.attr field for all values of MTF_DB_HDR.type */
#define MTF_CONTINUATION 0x0001
#define MTF_COMPRESSION 0x0002
#define MTF_EOS_AT_EOM 0x0004

/* bit masks for MTF_DB_HDR.attr field for MTF_DB_HDR.type = MTF_TAPE */
#define MTF_SET_MAP_EXISTS 0x0100
#define MTF_FDD_ALLOWED 0x0200

/* bit masks for MTF_DB_HDR.attr field for MTF_DB_HDR.type = MTF_SSET */
#define MTF_FDD_EXISTS 0x0100
#define MTF_ENCRYPTION 0x0200

/* bit masks for MTF_DB_HDR.attr field for MTF_DB_HDR.type = MTF_ESET */
#define MTF_FDD_ABORTED 0x0100
#define MTF_END_OF_FAMILY 0x0200
#define MTF_ABORTED_SET 0x0400

/* bit masks for MTF_DB_HDR.attr field for MTF_DB_HDR.type = MTF_EOTM */
#define MTF_NO_ESET_PBA 0x0100
#define MTF_INVALID_ESET_PBA 0x0200

/* values for MTF_DB_HDR.osId field */
#define MTF_OS_NETWARE 1
#define MTF_OS_NETWARE_SMS 13
#define MTF_OS_WINDOWS_NT 14
#define MTF_OS_DOS 24
#define MTF_OS_OS2 25
#define MTF_OS_WINDOWS_95 26
#define MTF_OS_MACINTOSH 27
#define MTF_OS_UNIX 28

/* values for MTF_DB_HDR.strType field */
#define MTF_NO_STRINGS 0
#define MTF_ANSI_STR 1
#define MTF_UNICODE_STR 2

/* structure pointed to by the MTF_DB_HDR.osData field when MTF_DB_HDR.osId = */
/* MTF_OS_WINDOWS_NT and MTF_DB_HDR.osVer = 0 */
typedef struct
{
	UINT32	attr;	/* file attributes */
	UINT16	off;	/* short name offset */
	UINT16	size;	/* short name size */
	BOOLEAN	link;	/* if non-zero the file is a link to a previous file */
	UINT16	rsv;	/* reserved for future use */
} MTF_OS_DATA_WINDOWS_NT;

/* descriptor block for MTF_DB_HDR.type = MTF_TAPE (tape header) */
typedef struct
{
	MTF_DB_HDR			common;		/* common block header */
	UINT32				famId;		/* media family ID */
	UINT32				attr;		/* TAPE attributes */
	UINT16				seq;		/* media sequence number */
	UINT16				encrypt;	/* password encryption */
	UINT16				sfmSize;	/* soft filemark block size */
	UINT16				catType;	/* media-based catalog type */
	MTF_TAPE_ADDRESS	name;		/* media name */
	MTF_TAPE_ADDRESS	desc;		/* media desc./label */
	MTF_TAPE_ADDRESS	passwd;		/* media password */
	MTF_TAPE_ADDRESS	software;	/* software name */
	UINT16				flbSize;	/* format logical block size */
	UINT16				vendorId;	/* software vendor ID */
	MTF_DATE_TIME		date;		/* media date */
	UINT8				ver;		/* MTF major version */
} MTF_TAPE_BLK;

/* bitmasks for MTF_TAPE_BLK.attr */
#define MTF_TAPE_SOFT_FILEMARK_BIT 0x00000001
#define MTF_TAPE_MEDIA_LABEL_BIT 0x00000002

/* values for MTF_TAPE_BLK.catType */
#define MTF_NO_MBC 0
#define MTF_MBC_TYPE_1 1
#define MTF_MBC_TYPE_2 2

/* descriptor block for MTF_DB_HDR.type = MTF_SSET (start of data set) */
typedef struct
{
	MTF_DB_HDR			common;			/* common block header */
	UINT32				attr;			/* SSET attributes */
	UINT16				passEncrypt;	/* password encryption */
	UINT16				softCompress;	/* software compression */
	UINT16				vendor;			/* software vendor ID */
	UINT16				num;			/* data set number */
	MTF_TAPE_ADDRESS	name;			/* data set name */
	MTF_TAPE_ADDRESS	desc;			/* data set description */
	MTF_TAPE_ADDRESS	passwd;			/* data set password */
	MTF_TAPE_ADDRESS	user;			/* user name */
	UINT64				pba;			/* physical block address */
	MTF_DATE_TIME		date;			/* media write date */
	UINT8				major;			/* software major version */
	UINT8				minor;			/* software minor version */
	INT8				tz;				/* time zone */
	UINT8				ver;			/* MTF minor version */
	UINT8				catVer;			/* media catalog version 8/ */
} MTF_SSET_BLK;

/* bitmasks for MTF_SSET_BLK.attr */
#define MTF_SSET_TRANSFER_BIT 0x00000001
#define MTF_SSET_COPY_BIT 0x00000002
#define MTF_SSET_NORMAL_BIT 0x00000004
#define MTF_SSET_DIFFERENTIAL_BIT 0x00000008
#define MTF_SSET_INCREMENTAL_BIT 0x00000010
#define MTF_SSET_DAILY_BIT 0x00000020

/* value for MTF_SSET_BLK.tz when local time is not coordinated with UTC */
#define MTF_LOCAL_TZ 127

/* descriptor block for MTF_DB_HDR.type = MTF_VOLB (volume) */
typedef struct
{
	MTF_DB_HDR			common;		/* common block header */
	UINT32				attr;		/* VOLB attributes */
	MTF_TAPE_ADDRESS	device;		/* device name */
	MTF_TAPE_ADDRESS	volume;		/* volume name */
	MTF_TAPE_ADDRESS	machine;	/* machine name */
	MTF_DATE_TIME		date;		/* media write date */
} MTF_VOLB_BLK;

/* bitmasks for MTF_VOLB_BLK.attr */
#define MFT_VOLB_NO_REDIRECT_RESTORE_BIT 0x00000001
#define MFT_VOLB_NON_VOLUME_BIT 0x00000002
#define MFT_VOLB_DEV_DRIVE_BIT 0x00000004
#define MFT_VOLB_DEV_UNC_BIT 0x00000008
#define MFT_VOLB_DEV_OS_SPEC_BIT 0x00000010
#define MFT_VOLB_DEV_VEND_SPEC_BIT 0x00000020

/* descriptor block for MTF_DB_HDR.type = MTF_DIRB (directory) */
typedef struct
{
	MTF_DB_HDR			common;		/* common block header */
	UINT32				attr;		/* DIRB attributes */
	MTF_DATE_TIME		mod;		/* last modification date */
	MTF_DATE_TIME		create;		/* creation date */
	MTF_DATE_TIME		backup;		/* backup date */
	MTF_DATE_TIME		access;		/* last access date */
	UINT32				id;			/* directory ID */
	MTF_TAPE_ADDRESS	name;		/* directory name */
} MTF_DIRB_BLK;

/* bitmasks for MTF_DIRB_BLK.attr */
#define MTF_DIRB_READ_ONLY_BIT 0x00000100
#define MTF_DIRB_HIDDEN_BIT 0x00000200
#define MTF_DIRB_SYSTEM_BIT 0x00000400
#define MTF_DIRB_MODIFIED_BIT 0x00000800
#define MTF_DIRB_EMPTY_BIT 0x00010000
#define MTF_DIR_PATH_IN_STREAM_BIT 0x00020000
#define MTF_DIRB_CORRUPT_BIT 0x00040000

/* descriptor block for MTF_DB_HDR.type = MTF_FILE (file) */
typedef struct
{
	MTF_DB_HDR			common;		/* common block header */
	UINT32				attr;		/* FILE attributes */
	MTF_DATE_TIME		mod;		/* last modification date */
	MTF_DATE_TIME		create;		/* creation date */
	MTF_DATE_TIME		backup;		/* backup date */
	MTF_DATE_TIME		access;		/* last access date */
	UINT32				dirId;		/* directory ID */
	UINT32				id;			/* file ID */
	MTF_TAPE_ADDRESS	name;		/* file name */
} MTF_FILE_BLK;

/* bitmasks for MTF_FILE_BLK.attr */
#define MTF_FILE_READ_ONLY_BIT 0x00000100
#define MTF_FILE_HIDDEN_BIT 0x00000200
#define MTF_FILE_SYSTEM_BIT 0x00000400
#define MTF_FILE_MODIFIED_BIT 0x00000800
#define MTF_FILE_IN_USE_BIT 0x00010000
#define MTF_FILE_NAME_IN_STREAM_BIT 0x00020000
#define MTF_FILE_CORRUPT_BIT 0x00040000

/* descriptor block for MTF_DB_HDR.type = MTF_CFIL (corrupt object) */
typedef struct
{
	MTF_DB_HDR	common;		/* common block header */
	UINT32		attr;		/* CFIL attributes */
	UINT8		rsv[8];		/* reserved for future use */
	UINT64		off;		/* stream offset */
	UINT64		num;		/* corrupt stream number */
} MTF_CFIL_BLK;

/* bitmasks for MTF_CFIL_BLK.attr */
#define MTF_CFIL_LENGTH_CHANGE_BIT 0x00010000
#define MTF_CFIL_UNREADABLE_BLK_BIT 0x00020000
#define MTF_CFIL_DEADLOCK_BIT 0x00080000
#define MTF_FILE_READ_ONLY_BIT 0x00000100

/* descriptor block for MTF_DB_HDR.type = MTF_ESPB (end of set pad) */
typedef MTF_DB_HDR MTF_ESPB_BLK;

/* descriptor block for MTF_DB_HDR.type = MTF_ESET (end of data set) */
typedef struct
{
	MTF_DB_HDR		common;		/* common block header */
	UINT32			attr;		/* ESET attributes */
	UINT32			corrupt;	/* number of corrupt files */
	UINT64			mbc1;		/* reserved for MBC */
	UINT64			mbc2;		/* reserved for MBC */
	UINT16			seq;		/* FDD media sequence number */
	UINT16			set;		/* data set number */
	MTF_DATE_TIME	date;		/* media write date */
} MTF_ESET_BLK;

/* bitmasks for MTF_ESET_BLK.attr */
#define MTF_ESET_TRANSFER_BIT 0x00000001
#define MTF_ESET_COPY_BIT 0x00000002
#define MTF_ESET_NORMAL_BIT 0x00000004
#define MTF_ESET_DIFFERENTIAL_BIT 0x00000008
#define MTF_ESET_INCREMENTAL_BIT 0x00000010
#define MTF_ESET_DAILY_BIT 0x00000020

/* descriptor block for MTF_DB_HDR.type = MTF_EOTM (end of tape) */
typedef struct
{
	MTF_DB_HDR		common;		/* common block header */
	UINT64			lastEset;	/* last ESET PBA */
} MTF_EOTM_BLK;

/* descriptor block for MTF_DB_HDR.type = MTF_SFMB (soft filemark) */
typedef struct
{
	MTF_DB_HDR	common;		/* common block header */
	UINT32		marks;		/* number of filemark entries */
	UINT32		used;		/* filemark entries used */
} MTF_SFMB_BLK;

/* stream header */
typedef struct
{
	UINT32	id;			/* stream ID */
	UINT16	sysAttr;	/* stream file system attributes */
	UINT16	mediaAttr;	/* stream media format attributes */
	UINT64	length;		/* stream length */
	UINT16	encrypt;	/* data encryption algorithm */
	UINT16	compress;	/* data compression algorithm */
	UINT16	check;		/* checksum */
} MTF_STREAM_HDR;

/* bitmasks for MTF_STREAM_HDR.sysAttr */
#define MTF_STREAM_MODIFIED_FOR_READ 0x00000001
#define MTF_STREAM_CONTAINS_SECURITY 0x00000002
#define MTF_STREAM_IS_NON_PORTABLE 0x00000004
#define MTF_STREAM_IS_SPARSE 0x00000008

/* bitmasks for MTF_STREAM_HDR.mediaAttr */
#define MTF_STREAM_CONTINUE 0x00000001
#define MTF_STREAM_VARIABLE 0x00000002
#define MTF_STREAM_VAR_END 0x00000004
#define MTF_STREAM_ENCRYPTED 0x00000008
#define MTF_STREAM_COMPRESSED 0x00000010
#define MTF_STREAM_CHECKSUMED 0x00000020
#define MTF_STREAM_EMBEDDED_LENGTH 0x00000040

/* platform-independant stream data types */
#define MTF_STAN 0x4E415453 /* standard */
#define MTF_PNAM 0x4D414E50 /* path */
#define MTF_FNAM 0x4D414E46 /* file name */
#define MTF_CSUM 0x4D555343 /* checksum */
#define MTF_CRPT 0x54505243 /* corrupt */
#define MTF_SPAD 0x44415053 /* pad */
#define MTF_SPAR 0x52415053 /* sparse */
#define MTF_TSMP 0x504D5354 /* set map, media based catalog, type 1 */
#define MTF_TFDD 0x44444654 /* fdd, media based catalog, type 1 */
#define MTF_MAP2 0x3250414D /* set map, media based catalog, type 2 */
#define MTF_FDD2 0x32444446 /* fdd, media based catalog, type 2 */

/* Windows NT stream data types */
#define MTF_ADAT 0x54414441
#define MTF_NTEA 0x4145544E
#define MTF_NACL 0x4C43414E
#define MTF_NTED 0x4445544E
#define MTF_NTQU 0x5551544E
#define MTF_NTPR 0x5250544E
#define MTF_NTOI 0x494F544E

/* Windows 95 stream data types */
#define MTF_GERC 0x43524547

/* Netware stream data types */
#define MTF_N386 0x3638334E
#define MTF_NBND 0x444E424E
#define MTF_SMSD 0x44534D53

/* OS/2 stream data types */
#define MTF_OACL 0x4C43414F

/* Macintosh stream data types */
#define MTF_MRSC 0x4353524D
#define MTF_MPRV 0x5652504D
#define MTF_MINF 0x464E494D

/* stream compression frame header */
typedef struct
{
	UINT16		id;				/* compression header id  - see define below */
	UINT16		attr;			/* stream media format attributes */
	UINT64		remain;			/* remaining stream size */
	UINT32		uncompress;		/* uncompressed size */
	UINT32		compress;		/* compressed size */
	UINT8		seq;			/* sequence number */
	UINT8		rsv;			/* reserved */
	UINT16		check;			/* checksum */
} MTF_CMP_HDR;

#define MTF_CMP_HDR_ID 0x4846


/* prototypes for mtfread.c */
INT32 openMedia(void);
INT32 readDataSet(void);
INT32 readEndOfDataSet(void);
INT32 readTapeBlock(void);
INT32 readStartOfSetBlock(void);
INT32 readVolumeBlock(void);
INT32 readDirectoryBlock(void);
INT32 readFileBlock(void);
INT32 readFile(UINT16);
INT32 readCorruptObjectBlock(void);
INT32 readEndOfSetPadBlock(void);
INT32 readEndOfSetBlock(void);
INT32 readEndOfTapeMarkerBlock(void);
INT32 readSoftFileMarkBlock(void);
INT32 readNextBlock(UINT16);
INT32 skipToNextBlock(void);
INT32 skipOverStream(void);
INT32 writeData(int);
char *getString(UINT8, UINT16, UINT8*);

/* prototypes for mtfutil.c */
void strlwr(char*);
void strupr(char*);
void increment64(UINT64*, UINT32);
void decrement64(UINT64*, UINT32);
void dump(char*);
