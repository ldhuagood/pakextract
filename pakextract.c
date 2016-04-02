/*-
 * Copyright (c) 2012-2016 Yamagi Burmeister
 *               2015-2016 Daniel Gibson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#include <assert.h>

enum {
	HDR_DIR_LEN_Q2 = 64,
	HDR_DIR_LEN_DK = 72
};

/* Holds the pak header */
struct
{
	char signature[4];
	int dir_offset;
	int dir_length;
} header;

int dk_pak_mode = 0;

/* A directory entry */
typedef struct
{
	char file_name[56];
	int file_pos;
	int file_length; // in case of is_compressed: size after decompression

	// the following two are only used by daikatana
	int compressed_length; // size of compressed data in the pak
	int is_compressed; // 0: uncompressed, else compressed

} directory;
 
/*
 * Creates a directory tree.
 *
 *  *s -> The path to create. The last
 *        part (the file itself) is
 *        ommitted
 */
static void
mktree(char *s)
{
	char *dir;
	char *elements[28];
	char *path;
	char *token;
	int i, j;

	path = calloc(56, sizeof(char));
	dir = malloc(sizeof(char) * 56);

	strncpy(dir, s, sizeof(char) * 56);

	for (i = 0; (token = strsep(&dir, "/")) != NULL; i++)
	{
		elements[i] = token;
	}

	for (j = 0; j < i - 1; j++)
	{
		strcat(path, elements[j]);
		strcat(path, "/");

		mkdir(path, 0700);
	}

	free(path);
	free(dir);
}

/*
 * Reads the pak file header and
 * stores it into the global struct
 * "header".
 *
 *  *fd -> A file descriptor holding
 *         the pack to be read.
 */
static int
read_header(FILE *fd)
{
	if (fread(header.signature, 4, 1, fd) != 1)
	{
		perror("Could not read the pak file header");
		return 0;
	}

	if (fread(&header.dir_offset, 4, 1, fd) != 1)
	{
		perror("Could not read the pak file header");
		return 0;
	}

	if (fread(&header.dir_length, 4, 1, fd) != 1)
	{
		perror("Could not read the pak file header");
		return 0;
	}

	// TODO: we could convert the ints to platform endianess now

	if (strncmp(header.signature, "PACK", 4) != 0)
	{
		fprintf(stderr, "Not a pak file\n");
		return 0;
	}

	int direntry_len = dk_pak_mode ? HDR_DIR_LEN_DK : HDR_DIR_LEN_Q2;

	// Note that this check is not reliable, it could pass and it could still be the wrong kind of pak!
	if ((header.dir_length % direntry_len) != 0)
	{
		const char* curmode = dk_pak_mode ? "Daikatana" : "Quake(2)";
		const char* othermode = (!dk_pak_mode) ? "Daikatana" : "Quake(2)";
		fprintf(stderr, "Corrupt pak file - maybe it's not %s format but %s format?\n", curmode, othermode);
		if(!dk_pak_mode)
			fprintf(stderr, "If this is a Daikatana .pak file, try adding '-dk' to command-line!\n");
		else
			fprintf(stderr, "Are you sure this is a Daikatana .pak file? Try removing '-dk' from command-line!\n");
		
		return 0;
	}

	return 1;
}

static int
read_dir_entry(directory* entry, FILE* fd)
{
	if(fread(entry->file_name, 56, 1, fd) != 1) return 0;
	if(fread(&(entry->file_pos), 4, 1, fd) != 1) return 0;
	if(fread(&(entry->file_length), 4, 1, fd) != 1) return 0;

	if(dk_pak_mode)
	{
		if(fread(&(entry->compressed_length), 4, 1, fd) != 1) return 0;
		if(fread(&(entry->is_compressed), 4, 1, fd) != 1) return 0;
	}
	else
	{
		entry->compressed_length = 0;
		entry->is_compressed = 0;
	}

	// TODO: we could convert the ints to platform endianess now

	return 1;
}

/*
 * Reads the directory of a pak file
 * into a linked list and returns 
 * a pointer to the first element.
 *
 *  *fd -> a file descriptor holding
 *         holding the pak to be read
 */ 
static directory *
read_directory(FILE *fd, int listOnly, int* num_entries)
{
	int i;
	int direntry_len = dk_pak_mode ? HDR_DIR_LEN_DK : HDR_DIR_LEN_Q2;
	int num_dir_entries = header.dir_length / direntry_len;
	directory* dir = calloc(num_dir_entries, sizeof(directory));

	if(dir == NULL)
	{
		perror("Couldn't allocate memory");
		return NULL;
	}

	/* Navigate to the directory */
	fseek(fd, header.dir_offset, SEEK_SET);

	for (i = 0; i < num_dir_entries; ++i)
	{
		directory* cur = &dir[i];
		
		if (!read_dir_entry(cur, fd))
		{
			perror("Could not read directory entry");
			*num_entries = 0;
			free(dir);
			return NULL;
		}

		if(listOnly)
		{
			printf("%s (%d bytes", cur->file_name, cur->file_length);
			
			if(dk_pak_mode && cur->is_compressed)
				printf(", %d compressed", cur->compressed_length);
			
			printf(")\n");
		}
	}

	*num_entries = num_dir_entries;
	return dir;
}

static void
extract_compressed(FILE* in, directory *d)
{
	FILE *out;
	int offset;
	int read;
	int written;
	int x;
	int num;
	unsigned char *in_buf;
	unsigned char *out_buf;

	if ((out = fopen(d->file_name, "w")) == NULL)
	{
		perror("Couldn't open outputfile");
		return;
	}

	if ((in_buf = malloc(d->compressed_length)) == NULL)
	{
		perror("Couldn't allocate memory");
		return;
	}

	if ((out_buf = calloc(1, d->file_length)) == NULL)
	{
		perror("Couldn't allocate memory");
		return;
	}

	fseek(in, d->file_pos, SEEK_SET);
	fread(in_buf, d->compressed_length, 1, in);

	read = 0;
	written = 0;

	while (read < d->compressed_length)
	{
		x = in_buf[read];
		++read;

		// x + 1 bytes of uncompressed data
		if (x < 64)
		{
			num = x + 1;
			memmove(out_buf + written, in_buf + read, num);

			read += num;
			written += num;

			continue;
		}
		// x - 62 zeros
		else if (x < 128)
		{
			num = x - 62;
			memset(out_buf + written, 0, num);

			written += num;

			continue;
		}
		// x - 126 times the next byte
		else if (x < 192)
		{
			num = x - 126;
			memset(out_buf + written, in_buf[read], num);

			++read;
			written += num;

			continue;
		}
		// Reference previously uncompressed data
		else if (x < 254)
		{
			num = x - 190;

			offset = (int)in_buf[read] + 2;
			++read;

			memmove(out_buf + written, (out_buf + written) - offset, num);
			written += num;
		}
		// Terminate
		else if (x == 255)
		{
			break;
		}
	}

	fwrite(out_buf, d->file_length, 1, out);
	fclose(out);

	free(in_buf);
	free(out_buf);
}

static void
extract_raw(FILE* in, directory *d)
{
	FILE* out = fopen(d->file_name, "w");
	if (out == NULL)
	{
		perror("Could open the outputfile");
		return;
	}
	
	// just copy the data from the .pak to the output file (in chunks for speed)
	int bytes_left = d->file_length;
	char buf[2048];
	
	fseek(in, d->file_pos, SEEK_SET);
	
	while(bytes_left >= sizeof(buf))
	{
		fread(buf, sizeof(buf), 1, in);
		fwrite(buf, sizeof(buf), 1, out);
		bytes_left -= sizeof(buf);
	}
	if(bytes_left > 0)
	{
		fread(buf, bytes_left, 1, in);
		fwrite(buf, bytes_left, 1, out);
	}

	fclose(out);
}

/* 
 * Extract the files from a pak.
 *
 *  *d -> a pointer to the first element
 *        of the pak directory
 *
 *  *fd -> a file descriptor holding
 *         the pak to be extracted
 */
static void
extract_files(FILE *fd, directory *dirs, int num_entries)
{
	int i;

	for(i=0; i<num_entries; ++i)
	{
		directory* d = &dirs[i];
	    mktree(d->file_name);

		if(d->is_compressed)
		{
			assert(dk_pak_mode != 0 && "Only Daikatana paks contain compressed files!");
			extract_compressed(fd, d);
		}
		else
		{
			extract_raw(fd, d);
		}
	}
}

static void
printUsage(const char* argv0)
{

	fprintf(stderr, "Extractor for Quake/Quake2 (and compatible) and Daikatana .pak files\n");

	fprintf(stderr, "Usage: %s [-l] [-dk] pakfile\n", argv0);
	fprintf(stderr, "       -l     don't extract, just list contents\n");
	fprintf(stderr, "       -dk    Daikatana pak format (Quake is default)\n");
}

/*
 * A small programm to extract a Quake II pak file.
 * The pak file is given as the first an only 
 * argument.
 */
int
main(int argc, char *argv[])
{
	directory *d = NULL;
	FILE *fd = NULL;
	const char* filename = NULL;
	int listOnly = 0;
	int i = 0;
	int num_entries = 0;

	/* Correct usage? */
	if (argc < 2)
	{
		printUsage(argv[0]);
		exit(-1);
	}
	
	for(i=1; i<argc; ++i)
	{
		const char* arg = argv[i];
		if(strcmp(arg, "-l") == 0) listOnly = 1;
		else if(strcmp(arg, "-dk") == 0) dk_pak_mode = 1;
		else
		{
			if(filename != NULL) // we already set a filename, wtf
			{
				fprintf(stderr, "!! Illegal argument '%s' (or too many filenames) !!\n", arg);
				printUsage(argv[0]);
				exit(-1);
			}
			filename = arg;
		}
	}

	if(filename == NULL)
	{
		fprintf(stderr, "!! No filename given !!\n");
		printUsage(argv[0]);
		exit(-1);
	}

	/* Open the pak file */
	fd = fopen(filename, "r");
	if (fd == NULL)
	{
		fprintf(stderr, "Could not open the pak file '%s': %s\n", filename, strerror(errno));
		exit(-1);
	}

	/* Read the header */
    if (!read_header(fd))
	{
		fclose(fd);
		exit(-1);
	}

	/* Read the directory */
	d = read_directory(fd, listOnly, &num_entries);
	if (d == NULL)
	{
		fclose(fd);
		exit(-1);
	}

	if(!listOnly)
	{
		/* And now extract the files */
		extract_files(fd, d, num_entries);
	}

	/* cleanup */
	fclose(fd);

	free(d);

	return 0;
}

