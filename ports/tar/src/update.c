/* Update a tar archive.
   Copyright (C) 1988, 1992, 1994, 1996, 1997 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 2, or (at your option) any later
   version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
   Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

/* Implement the 'r', 'u' and 'A' options for tar.  'A' means that the
   file names are tar files, and they should simply be appended to the end
   of the archive.  No attempt is made to record the reads from the args; if
   they're on raw tape or something like that, it'll probably lose...  */

#include "system.h"
#include "common.h"

/* FIXME: This module should not directly handle the following variable,
   instead, this should be done in buffer.c only.  */
extern union block *current_block;

/* We've hit the end of the old stuff, and its time to start writing new
   stuff to the tape.  This involves seeking back one record and
   re-writing the current record (which has been changed).  */
int time_to_start_writing = 0;

/* Pointer to where we started to write in the first record we write out.
   This is used if we can't backspace the output and have to null out the
   first part of the record.  */
char *output_start;

/*------------------------------------------------------------------------.
| Catenate file PATH to the archive without creating a header for it.  It |
| had better be a tar file or the archive is screwed.			  |
`------------------------------------------------------------------------*/

static void
append_file (char *path)
{
  int handle;
  struct stat stat_data;
  off_t bytes_left;

  if (stat (path, &stat_data) != 0
      || (handle = open (path, O_RDONLY | O_BINARY), handle < 0))
    {
      ERROR ((0, errno, _("Cannot open file %s"), path));
      return;
    }

  bytes_left = stat_data.st_size;

  while (bytes_left > 0)
    {
      union block *start = find_next_block ();
      size_t buffer_size = available_space_after (start);
      ssize_t status;

      if (bytes_left < buffer_size)
	{
	  buffer_size = bytes_left;
	  status = buffer_size % BLOCKSIZE;
	  if (status)
	    memset (start->buffer + bytes_left, 0,
		    (size_t) (BLOCKSIZE - status));
	}

      status = safe_read (handle, start->buffer, buffer_size);
      if (status < 0)
	{
	  char buf[UINTMAX_STRSIZE_BOUND];
	  FATAL_ERROR ((0, errno,
			_("Read error at byte %s reading %lu bytes in file %s"),
			STRINGIFY_BIGINT (stat_data.st_size - bytes_left, buf),
			(unsigned long) buffer_size, path));
	}
      bytes_left -= status;

      set_next_block_after (start + (status - 1) / BLOCKSIZE);

      if (status != buffer_size)
	{
	  char buf[UINTMAX_STRSIZE_BOUND];
	  FATAL_ERROR ((0, 0, _("%s: File shrunk by %s bytes, (yark!)"),
			path, STRINGIFY_BIGINT (bytes_left, buf)));
	}
    }

  close (handle);
}

/*-----------------------------------------------------------------------.
| Implement the 'r' (add files to end of archive), and 'u' (add files to |
| end of archive if they arent there, or are more up to date than the	 |
| version in the archive.) commands.					 |
`-----------------------------------------------------------------------*/

void
update_archive (void)
{
  enum read_header previous_status = HEADER_STILL_UNREAD;
  int found_end = 0;

  name_gather ();
  if (subcommand_option == UPDATE_SUBCOMMAND)
    name_expand ();
  open_archive (ACCESS_UPDATE);

  while (!found_end)
    {
      enum read_header status = read_header ();

      switch (status)
	{
	case HEADER_STILL_UNREAD:
	  abort ();

	case HEADER_SUCCESS:
	  {
	    struct name *name;

	    if (subcommand_option == UPDATE_SUBCOMMAND
		&& (name = name_scan (current_file_name), name))
	      {
		struct stat stat_data;
		enum archive_format unused;

		decode_header (current_header, &current_stat, &unused, 0);
		if (stat (current_file_name, &stat_data) < 0)
		  ERROR ((0, errno, _("Cannot stat %s"), current_file_name));
		else if (current_stat.st_mtime >= stat_data.st_mtime)
		  name->found = 1;
	      }
	    set_next_block_after (current_header);
	    if (current_header->oldgnu_header.isextended)
	      skip_extended_headers ();
	    skip_file (current_stat.st_size);
	    break;
	  }

	case HEADER_ZERO_BLOCK:
	  current_block = current_header;
	  found_end = 1;
	  break;

	case HEADER_END_OF_FILE:
	  found_end = 1;
	  break;

	case HEADER_FAILURE:
	  set_next_block_after (current_header);
	  switch (previous_status)
	    {
	    case HEADER_STILL_UNREAD:
	      WARN ((0, 0, _("This does not look like a tar archive")));
	      /* Fall through.  */

	    case HEADER_SUCCESS:
	    case HEADER_ZERO_BLOCK:
	      ERROR ((0, 0, _("Skipping to next header")));
	      /* Fall through.  */

	    case HEADER_FAILURE:
	      break;

	    case HEADER_END_OF_FILE:
	      abort ();
	    }
	  break;
	}

      previous_status = status;
    }

  reset_eof ();
  time_to_start_writing = 1;
  output_start = current_block->buffer;

  {
    char *path;

    while (path = name_from_list (), path)
      {
	if (interactive_option && !confirm ("add", path))
	  continue;
	if (subcommand_option == CAT_SUBCOMMAND)
	  append_file (path);
	else
	  dump_file (path, (dev_t) -1, 1);
      }
  }

  write_eot ();
  close_archive ();
  names_notfound ();
}
