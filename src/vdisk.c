/*
 * Copyright (C) 2012 Michael Brown <mbrown@fensystems.co.uk>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/**
 * @file
 *
 * Virtual disk
 *
 */

#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "ctype.h"
#include "wimboot.h"
#include "vdisk.h"

/** Virtual files */
struct vdisk_file vdisk_files[VDISK_MAX_FILES];

/**
 * Read from virtual Master Boot Record
 *
 * @v lba		Starting LBA
 * @v count		Number of blocks to read
 * @v data		Data buffer
 */
static void vdisk_mbr ( uint64_t lba __attribute__ (( unused )),
			unsigned int count __attribute__ (( unused )),
			void *data ) {
	struct vdisk_mbr *mbr = data;

	/* Construct MBR */
	memset ( mbr, 0, sizeof ( *mbr ) );
	mbr->partitions[0].bootable = VDISK_MBR_BOOTABLE;
	mbr->partitions[0].type = VDISK_MBR_TYPE_FAT32;
	mbr->partitions[0].start = VDISK_PARTITION_LBA;
	mbr->partitions[0].length = VDISK_PARTITION_COUNT;
	mbr->signature = VDISK_MBR_SIGNATURE;
	mbr->magic = VDISK_MBR_MAGIC;
}

/**
 * Read from virtual Volume Boot Record
 *
 * @v lba		Starting LBA
 * @v count		Number of blocks to read
 * @v data		Data buffer
 */
static void vdisk_vbr ( uint64_t lba __attribute__ (( unused )),
			unsigned int count __attribute__ (( unused )),
			void *data ) {
	struct vdisk_vbr *vbr = data;

	/* Construct VBR */
	memset ( vbr, 0, sizeof ( *vbr ) );
	vbr->jump[0] = VDISK_VBR_JUMP_WTF_MS;
	memcpy ( vbr->oemid, VDISK_VBR_OEMID, sizeof ( vbr->oemid ) );
	vbr->bytes_per_sector = VDISK_SECTOR_SIZE;
	vbr->sectors_per_cluster = VDISK_CLUSTER_COUNT;
	vbr->reserved_sectors = VDISK_RESERVED_COUNT;
	vbr->fats = 1;
	vbr->media = VDISK_VBR_MEDIA;
	vbr->sectors_per_track = VDISK_SECTORS_PER_TRACK;
	vbr->heads = VDISK_HEADS;
	vbr->hidden_sectors = VDISK_VBR_LBA;
	vbr->sectors = VDISK_PARTITION_COUNT;
	vbr->sectors_per_fat = VDISK_SECTORS_PER_FAT;
	vbr->root = VDISK_ROOT_CLUSTER;
	vbr->fsinfo = VDISK_FSINFO_SECTOR;
	vbr->backup = VDISK_BACKUP_VBR_SECTOR;
	vbr->signature = VDISK_VBR_SIGNATURE;
	vbr->serial = VDISK_VBR_SERIAL;
	memcpy ( vbr->label, VDISK_VBR_LABEL, sizeof ( vbr->label ) );
	memcpy ( vbr->system, VDISK_VBR_SYSTEM, sizeof ( vbr->system ) );
	vbr->magic = VDISK_VBR_MAGIC;
}

/**
 * Read from virtual FSInfo
 *
 * @v lba		Starting LBA
 * @v count		Number of blocks to read
 * @v data		Data buffer
 */
static void vdisk_fsinfo ( uint64_t lba __attribute__ (( unused )),
			   unsigned int count __attribute__ (( unused )),
			   void *data ) {
	struct vdisk_fsinfo *fsinfo = data;

	/* Construct FSInfo */
	memset ( fsinfo, 0, sizeof ( *fsinfo ) );
	fsinfo->magic1 = VDISK_FSINFO_MAGIC1;
	fsinfo->magic2 = VDISK_FSINFO_MAGIC2;
	fsinfo->next_free = VDISK_FSINFO_NEXT_FREE;
	fsinfo->magic3 = VDISK_FSINFO_MAGIC3;
}

/**
 * Read from virtual FAT
 *
 * @v lba		Starting LBA
 * @v count		Number of blocks to read
 * @v data		Data buffer
 */
static void vdisk_fat ( uint64_t lba, unsigned int count, void *data ) {
	uint32_t *next = data;
	uint32_t start;
	uint32_t end;
	uint32_t file_end_marker;
	unsigned int i;

	/* Calculate window within FAT */
	start = ( ( lba - VDISK_FAT_LBA ) *
		  ( VDISK_SECTOR_SIZE / sizeof ( *next ) ) );
	end = ( start + ( count * ( VDISK_SECTOR_SIZE / sizeof ( *next ) ) ) );
	next -= start;

	/* Start by marking each cluster as chaining to the next */
	for ( i = start ; i < end ; i++ )
		next[i] = ( i + 1 );

	/* Add first-sector special values, if applicable */
	if ( start == 0 ) {
		next[0] = ( ( VDISK_FAT_END_MARKER & ~0xff ) |
			    VDISK_VBR_MEDIA );
		next[1] = VDISK_FAT_END_MARKER;
		next[VDISK_ROOT_CLUSTER] = VDISK_FAT_END_MARKER;
		next[VDISK_BOOT_CLUSTER] = VDISK_FAT_END_MARKER;
		next[VDISK_SOURCES_CLUSTER] = VDISK_FAT_END_MARKER;
		next[VDISK_FONTS_CLUSTER] = VDISK_FAT_END_MARKER;
	}

	/* Add end-of-file markers, if applicable */
	for ( i = 0 ; i < VDISK_MAX_FILES ; i++ ) {
		if ( vdisk_files[i].data ) {
			file_end_marker = ( VDISK_FILE_CLUSTER ( i ) +
					    ( ( vdisk_files[i].len - 1 ) /
					      VDISK_CLUSTER_SIZE ) );
			if ( ( file_end_marker >= start ) &&
			     ( file_end_marker < end ) ) {
				next[file_end_marker] = VDISK_FAT_END_MARKER;
			}
		}
	}
}

/**
 * Initialise empty directory
 *
 * @v dir		Virtual directory
 */
static void vdisk_empty_dir ( union vdisk_directory *dir ) {
	unsigned int i;

	/* Mark all entries as present and deleted */
	memset ( dir, 0, sizeof ( *dir ) );
	for ( i = 0 ; i < VDISK_DIRENT_PER_SECTOR ; i++ ) {
		dir->entry[i].filename[0] = VDISK_DIRENT_FILENAME0_DELETED;
	}
}

/**
 * Read subdirectories from virtual root directory
 *
 * @v lba		Starting LBA
 * @v count		Number of blocks to read
 * @v data		Data buffer
 */
static void vdisk_root ( uint64_t lba __attribute__ (( unused )),
			 unsigned int count __attribute__ (( unused )),
			 void *data ) {
	union vdisk_directory *dir = data;
	static const struct vdisk_directory_entry root[] = {
		{
			.filename = "BOOT    ",
			.extension = "   ",
			.attr = VDISK_DIRECTORY,
			.cluster_high = ( VDISK_BOOT_CLUSTER >> 16 ),
			.cluster_low = ( VDISK_BOOT_CLUSTER & 0xffff ),
		},
		{
			.filename = "SOURCES ",
			.extension = "   ",
			.attr = VDISK_DIRECTORY,
			.cluster_high = ( VDISK_SOURCES_CLUSTER >> 16 ),
			.cluster_low = ( VDISK_SOURCES_CLUSTER & 0xffff ),
		},
	};

	/* Construct root subdirectories */
	vdisk_empty_dir ( dir );
	memcpy ( &dir->entry[0], root, sizeof ( root ) );
}

/**
 * Read subdirectories from virtual boot directory
 *
 * @v lba		Starting LBA
 * @v count		Number of blocks to read
 * @v data		Data buffer
 */
static void vdisk_boot ( uint64_t lba __attribute__ (( unused )),
			 unsigned int count __attribute__ (( unused )),
			 void *data ) {
	union vdisk_directory *dir = data;
	static const struct vdisk_directory_entry boot[] = {
		{
			.filename = "FONTS   ",
			.extension = "   ",
			.attr = VDISK_DIRECTORY,
			.cluster_high = ( VDISK_FONTS_CLUSTER >> 16 ),
			.cluster_low = ( VDISK_FONTS_CLUSTER & 0xffff ),
		},
	};

	/* Construct boot subdirectories */
	vdisk_empty_dir ( dir );
	memcpy ( &dir->entry[0], boot, sizeof ( boot ) );
}

/**
 * Read subdirectories from virtual sources directory
 *
 * @v lba		Starting LBA
 * @v count		Number of blocks to read
 * @v data		Data buffer
 */
static void vdisk_sources ( uint64_t lba __attribute__ (( unused )),
			    unsigned int count __attribute__ (( unused )),
			    void *data ) {
	union vdisk_directory *dir = data;

	/* Construct sources subdirectories */
	vdisk_empty_dir ( dir );
}

/**
 * Read subdirectories from virtual fonts directory
 *
 * @v lba		Starting LBA
 * @v count		Number of blocks to read
 * @v data		Data buffer
 */
static void vdisk_fonts ( uint64_t lba __attribute__ (( unused )),
			  unsigned int count __attribute__ (( unused )),
			  void *data ) {
	union vdisk_directory *dir = data;

	/* Construct fonts subdirectories */
	vdisk_empty_dir ( dir );
}

/**
 * Read files from virtual directory
 *
 * @v lba		Starting LBA
 * @v count		Number of blocks to read
 * @v data		Data buffer
 */
static void vdisk_dir_files ( uint64_t lba, unsigned int count, void *data ) {
	union vdisk_directory *dir;
	struct vdisk_directory_entry *dirent;
	struct vdisk_long_filename *lfn;
	struct vdisk_file *file;
	unsigned int idx;
	uint32_t cluster;
	const char *name;
	uint8_t *checksum_data;
	uint8_t checksum;
	unsigned int sequence;
	uint16_t *lfn_char;
	char c;
	unsigned int i;

	for ( ; count ; lba++, count--, data += VDISK_SECTOR_SIZE ) {

		/* Initialise directory */
		dir = data;
		vdisk_empty_dir ( dir );
		dirent = &dir->entry[ VDISK_DIRENT_PER_SECTOR - 1 ];

		/* Identify file */
		idx = VDISK_FILE_DIRENT_IDX ( lba );
		file = &vdisk_files[idx];
		if ( ! file->data )
			continue;

		/* Populate directory entry (with invalid 8.3 filename) */
		memset ( dirent->filename, ' ', sizeof ( dirent->filename ) );
		memset ( dirent->extension, ' ', sizeof ( dirent->extension ) );
		dirent->attr = VDISK_READ_ONLY;
		dirent->size = file->len;
		cluster = VDISK_FILE_CLUSTER ( idx );
		dirent->cluster_high = ( cluster >> 16 );
		dirent->cluster_low = ( cluster & 0xffff );

		/* Calculate checksum of 8.3 filename */
		checksum = 0;
		checksum_data = ( ( uint8_t * ) dirent->filename );
		for ( i = 0 ; i < ( sizeof ( dirent->filename ) +
				    sizeof ( dirent->extension ) ) ; i++ ) {
			checksum = ( ( ( ( checksum & 1 ) << 7 ) |
				       ( checksum >> 1 ) ) +
				     *(checksum_data++) );
		}

		/* Construct long filename record */
		lfn = &dir->lfn[ VDISK_DIRENT_PER_SECTOR - 2 ];
		lfn_char = &lfn->name_1[0];
		name = file->name;
		sequence = 1;
		do {

			/* Initialise long filename, if necessary */
			if ( lfn->attr != VDISK_LFN_ATTR ) {
				lfn->sequence = sequence++;
				memset ( lfn->name_1, 0xff,
					 sizeof ( lfn->name_1 ) );
				lfn->attr = VDISK_LFN_ATTR;
				lfn->checksum = checksum;
				memset ( lfn->name_2, 0xff,
					 sizeof ( lfn->name_2 ) );
				memset ( lfn->name_3, 0xff,
					 sizeof ( lfn->name_3 ) );
			}

			/* Add character to long filename */
			c = *(name++);
			*lfn_char = c;
			if ( lfn_char == &lfn->name_1[4] ) {
				lfn_char = &lfn->name_2[0];
			} else if ( lfn_char == &lfn->name_2[5] ) {
				lfn_char = &lfn->name_3[0];
			} else if ( lfn_char == &lfn->name_3[1] ) {
				lfn--;
				lfn_char = &lfn->name_1[0];
			} else {
				lfn_char++;
			}

		} while ( c != 0 );
		lfn->sequence |= VDISK_LFN_END;
	}
}

/**
 * Read from virtual file (or empty space)
 *
 * @v lba		Starting LBA
 * @v count		Number of blocks to read
 * @v data		Data buffer
 */
static void vdisk_file ( uint64_t lba, unsigned int count, void *data ) {
	struct vdisk_file *file;
	size_t offset;
	size_t len;
	size_t copy_len;
	size_t pad_len;

	/* Construct file portion */
	file = &vdisk_files[ VDISK_FILE_IDX ( lba ) ];
	offset = VDISK_FILE_OFFSET ( lba );
	len = ( count * VDISK_SECTOR_SIZE );
	copy_len = ( ( offset < file->len ) ? ( file->len - offset ) : 0 );
	if ( copy_len > len )
		copy_len = len;
	pad_len = ( len - copy_len );
	memcpy ( data, ( file->data + offset ), copy_len );
	memset ( ( data + copy_len ), 0, pad_len );
}

/** A virtual disk region */
struct vdisk_region {
	/** Name */
	const char *name;
	/** Starting LBA */
	uint64_t lba;
	/** Number of blocks */
	unsigned int count;
	/**
	 * Build data from this region
	 *
	 * @v start		Starting LBA
	 * @v count		Number of blocks to read
	 * @v data		Data buffer
	 */
	void ( * build ) ( uint64_t lba, unsigned int count, void *data );
};

/** Define a virtual disk region */
#define VDISK_REGION( _name, _build, _lba, _count ) {		\
		.name = _name,					\
		.lba = _lba,					\
		.count = _count,				\
		.build = _build,				\
	}

/** Define a virtual disk directory region */
#define VDISK_DIRECTORY_REGION( _name, _build_subdirs, _lba ) {	\
		.name = _name " subdirs",			\
		.lba = _lba,					\
		.count = 1,					\
		.build = _build_subdirs,			\
	}, {							\
		.name = _name " files",				\
		.lba = ( _lba + 1 ),				\
		.count = ( VDISK_CLUSTER_COUNT - 1 ),		\
		.build = vdisk_dir_files,			\
	}

/** Virtual disk regions */
static struct vdisk_region vdisk_regions[] = {
	VDISK_REGION ( "MBR", vdisk_mbr,
		       VDISK_MBR_LBA, VDISK_MBR_COUNT ),
	VDISK_REGION ( "VBR", vdisk_vbr,
		       VDISK_VBR_LBA, VDISK_VBR_COUNT ),
	VDISK_REGION ( "FSInfo", vdisk_fsinfo,
		       VDISK_FSINFO_LBA, VDISK_FSINFO_COUNT ),
	VDISK_REGION ( "VBR Backup", vdisk_vbr,
		       VDISK_BACKUP_VBR_LBA, VDISK_BACKUP_VBR_COUNT ),
	VDISK_REGION ( "FAT", vdisk_fat,
		       VDISK_FAT_LBA, VDISK_FAT_COUNT ),
	VDISK_DIRECTORY_REGION ( "Root", vdisk_root, VDISK_ROOT_LBA ),
	VDISK_DIRECTORY_REGION ( "Boot", vdisk_boot, VDISK_BOOT_LBA ),
	VDISK_DIRECTORY_REGION ( "Sources", vdisk_sources, VDISK_SOURCES_LBA ),
	VDISK_DIRECTORY_REGION ( "Fonts", vdisk_fonts, VDISK_FONTS_LBA ),
};

/**
 * Read from virtual disk
 *
 * @v lba		Starting LBA
 * @v count		Number of blocks to read
 * @v data		Data buffer
 */
void vdisk_read ( uint64_t lba, unsigned int count, void *data ) {
	struct vdisk_region *region;
	void ( * build ) ( uint64_t lba, unsigned int count, void *data );
	const char *name;
	uint64_t start = lba;
	uint64_t end = ( lba + count );
	uint64_t frag_start = start;
	uint64_t frag_end;
	int file_idx;
	uint64_t file_end;
	uint64_t region_start;
	uint64_t region_end;
	unsigned int frag_count;
	unsigned int i;

	DBG2 ( "Read to %p from %#llx+%#x: ", data, lba, count );

	do {
		/* Initialise fragment to fill remaining space */
		frag_end = end;
		name = NULL;
		build = NULL;

		/* Truncate fragment and generate data */
		file_idx = VDISK_FILE_IDX ( frag_start );
		if ( file_idx >= 0 ) {

			/* Truncate fragment to end of file */
			file_end = VDISK_FILE_LBA ( file_idx + 1 );
			if ( frag_end > file_end )
				frag_end = file_end;

			/* Generate data from file */
			if ( file_idx < VDISK_MAX_FILES ) {
				name = vdisk_files[file_idx].name;
				build = vdisk_file;
			}

		} else {

			/* Truncate fragment to region boundaries */
			for ( i = 0 ; i < ( sizeof ( vdisk_regions ) /
					    sizeof ( vdisk_regions[0] ) ); i++){
				region = &vdisk_regions[i];
				region_start = region->lba;
				region_end = ( region_start + region->count );

				/* Avoid crossing start of any region */
				if ( ( frag_start < region_start ) &&
				     ( frag_end > region_start ) ){
					frag_end = region_start;
				}

				/* Ignore unless we overlap with this region */
				if ( ( frag_start >= region_end ) ||
				     ( frag_end <= region_start ) ) {
					continue;
				}

				/* Avoid crossing end of region */
				if ( frag_end > region_end )
					frag_end = region_end;

				/* Found a suitable region */
				name = region->name;
				build = region->build;
				break;
			}
		}

		/* Generate data from this region */
		frag_count = ( frag_end - frag_start );
		DBG2 ( "%s%s (%#x)", ( ( frag_start == start ) ? "" : ", " ),
		       ( name ? name : "empty" ), frag_count );
		if ( build ) {
			build ( frag_start, frag_count, data );
		} else {
			memset ( data, 0, ( frag_count * VDISK_SECTOR_SIZE ) );
		}

		/* Move to next fragment */ 
		frag_start += frag_count;
		data += ( frag_count * VDISK_SECTOR_SIZE );

	} while ( frag_start != end );

	DBG2 ( "\n" );
}