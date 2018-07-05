/*
 *  mIDA - MIDL Analyzer plugin for IDA
 *  (c) 2005 - Nicolas Pouvesle / Tenable Network Security
 *
 */

#include <windows.h>

#include "midl.h"
#include "midl_scan.h"
#include "midl_decompile.h"
#include "buffer.h"
#include "window.h"

#include <ida.hpp>
#include <idp.hpp>
#include <expr.hpp>
#include <bytes.hpp>
#include <loader.hpp>
#include <kernwin.hpp>


char * ofile;
DWORD mFlags;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	mIDAhInst = hinstDLL;
	return TRUE;
}


static ulong idaapi sizer_fct(void *obj)
{
	midl_interface * mi = (midl_interface *) obj;
	midl_fct_list * fct_list = mi->list;

	return fct_list->fct_num;
}

static void idaapi close_fct(void *obj)
{
	midl_interface * mi = (midl_interface *) obj;

	qfree (mi->list);
	qfree (mi);
}

static void idaapi desc_fct(void *obj,ulong n,char * const *arrptr)
{
	midl_interface * mi = (midl_interface *) obj;
	midl_fct_list * fct_list = mi->list;
	midl_function * mfcts = fct_list->list;

	/* header */
	if (n == 0)
	{
		for (int i = 0; i < qnumber (header_fct); i++)
			qsnprintf(arrptr[i], MAXSTR, "%s", header_fct[i]);
	}
	else
	{
		qsnprintf(arrptr[0], MAXSTR, "0x%.2X", mfcts[n-1].opcode);
		qsnprintf(arrptr[1], MAXSTR, "0x%.8X", mfcts[n-1].offset);
		qsnprintf(arrptr[2], MAXSTR, "%s", mfcts[n-1].name);
	}
}

static void idaapi enter_cb_fct(void *obj,ulong n)
{
	midl_interface * mi = (midl_interface *) obj;
	midl_fct_list * fct_list = mi->list;
	midl_function * mfcts = fct_list->list;

	jumpto (mfcts[n-1].offset);
}


static void idaapi edit_fct(void *obj,ulong n)
{
	midl_interface * mi = (midl_interface *) obj;
	midl_fct_list * fct_list = mi->list;
	midl_function * mfcts = fct_list->list;

	 // build the format string that creates the dialog.
    const char format[] =
	"STARTITEM 0\n"

	"Function preferences\n" // Title

	"<Function name:A:100:50::>\n" 
	"<Function arguments address:N:20:20::>\n"
	"<Function structures address:N:20:20::>\n\n"

    ; // End Dialog Format String

    // do the magic
	AskUsingForm_c(format, (char *)mfcts[n-1].name, &mfcts[n-1].arg_offset, &mi->type_raw);
}

static ulong idaapi decompile_fct(void *obj,ulong n)
{
	midl_interface * mi = (midl_interface *) obj;
	midl_fct_list * fct_list = mi->list;
	midl_function * mfcts = fct_list->list;

	if (IS_START_SEL(n))
	{
		mi->midl_struct = (midl_structure_list *) qalloc (sizeof(*mi->midl_struct));
		if (!mi->midl_struct)
		{
			msg ("Error while allocating midl structure list\n");
			return -1;
		}

		mi->fct_buffer = init_buffer();
		if (!mi->fct_buffer)
		{
			qfree (mi->midl_struct);
			mi->midl_struct = NULL;
			return -1;
		}

		mi->struct_buffer = init_buffer();
		if (!mi->struct_buffer)
		{
			free_buffer (mi->fct_buffer);
			qfree (mi->midl_struct);
			mi->midl_struct = NULL;
			return -1;
		}
		
		mi->midl_struct->num = 0;
		mi->midl_struct->mstruct = NULL;

		return 1;
	}
	else if (IS_END_SEL(n))
	{
		buffer * buf;
		char tmp[2000];
		HWND hWindow = NULL;
		FILE * fp;

		if (!mi->midl_struct)
			return -1;

		buf = init_buffer();
		if (!buf)
		{
			msg ("Error while allocating final buffer !\n");
			return -1;
		}

		decompile_struct_list (mi->midl_struct, mi->struct_buffer, mi->callback_table, mi->expr_table);
		free_midl_structure_list (mi->midl_struct);
		
		qsnprintf ((char *)tmp, sizeof(tmp), 
			"/*\r\n"
			" * IDL code generated by mIDA v%s\r\n"
			" * Copyright (C) 2006, Tenable Network Security\r\n"
			" * http://cgi.tenablesecurity.com/tenable/mida.php\r\n"
			" * \r\n"
			" * \r\n"
			" * Decompilation information:\r\n"
			" * RPC stub type: %s\r\n"
			" */\r\n\r\n",
			MIDA_VERSION,
			mi->is_inline ? "inline" : "interpreted / fully interpreted");
		buffer_add_message (buf, (char*) tmp);

		// create interface description
		qsnprintf ((char *)tmp, sizeof(tmp), "[\r\n uuid(%.8x-%.4x-%.4x-%.2x%.2x-%.2x%.2x%.2x%.2x%.2x%.2x),\r\n version(%d.%d)\r\n]\r\n\r\ninterface %s\r\n{\r\n\r\n",
		mi->uuid.x1, mi->uuid.x2, mi->uuid.x3,
		mi->uuid.x4[0],mi->uuid.x4[1],
		mi->uuid.x4[2],mi->uuid.x4[3],mi->uuid.x4[4],mi->uuid.x4[5],mi->uuid.x4[6],mi->uuid.x4[7],
		mi->uuid.major, mi->uuid.minor,
		"mIDA_interface");

		// final buffer = description + structures + functions
		buffer_add_message (buf, (char*)tmp);
		buffer_add_message (buf, (char*)mi->struct_buffer->buffer);
		buffer_add_message (buf, (char*)mi->fct_buffer->buffer);
		buffer_add_message (buf, (char*)"}\r\n");

		if (ofile == NULL)
		{
			hWindow = AddMDIChild ();

			if (!hWindow)
			{
				msg ("Decompile Output :\n\n%s", buf->buffer);
			}
			else
			{
				SetMDIWindowText (hWindow, buf->buffer);
			}
		}
		else
		{
			fp = qfopen ((char *)ofile, (char *)"a");
			if (fp)
			{
				qfwrite (fp, (void *)buf->buffer, strlen (buf->buffer));
				qfclose (fp);
			}

		}

		free_buffer (buf);
		free_buffer (mi->fct_buffer);
		free_buffer (mi->struct_buffer);
		qfree (mi->midl_struct);
		mi->midl_struct = NULL;
		return 1;
	}
	else if (IS_SEL(n))
	{
		if (!mi->midl_struct)
			return -1;

		__try
		{
			decompile_function (&mfcts[n-1], mi->type_raw, mi->fct_buffer, mi->midl_struct, mi->ndr_version, mi->callback_table, mi->expr_table);
		}
		__except (GetExceptionCode() == EXCEPTION_MIDA_LOOPLIMIT ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
		{
			free_buffer (mi->fct_buffer);
			free_buffer (mi->struct_buffer);
			qfree (mi->midl_struct);
			mi->midl_struct = NULL;
			return 0;
		}
		return 1;
	}

	return 1;
}


static void idaapi decompile_all_fct(void *obj)
{
	midl_interface * mi = (midl_interface *) obj;
	midl_fct_list * fct_list = mi->list;
	unsigned int i, ret;

	__try
	{
		decompile_fct (obj, START_SEL);
	
		for (i=1; i<=fct_list->fct_num; i++)
		{
			ret = decompile_fct (obj, i);
			if (ret == 0)
				RaiseException (EXCEPTION_MIDA_LOOPLIMIT,0,0,NULL);
		}

		decompile_fct (obj, END_SEL);
	}
	__except (GetExceptionCode() == EXCEPTION_MIDA_LOOPLIMIT ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
	{
		return;
	}
}


int idaapi IDAP_init(void)
{
	if (strncmp (inf.procName, "metapc", 8) != 0 || inf.filetype != f_PE)
	{
		msg ("mIDA: Can't be loaded (not a PE file or debugger mode)\n");
		return PLUGIN_SKIP;
	}
	
	return PLUGIN_OK;
}


void idaapi IDAP_term(void)
{
	CleanupMDIWindow ();
}


void idaapi IDAP_run(int arg)
{
	const char * options = NULL;
	midl_interface_list * midl_list = NULL, * tmp_list = NULL;
	midl_interface * mi;
	unsigned char uuid_name[100];
	long ret;
	HKEY hKey;
	DWORD len;
	DWORD type;

	InitializeMDIWindow (mIDAhInst);

	mFlags = 0;
	ret = RegOpenKeyEx(HKEY_LOCAL_MACHINE, "SOFTWARE\\Tenable\\mIDA", 0, MAXIMUM_ALLOWED, &hKey);
	if (ret == ERROR_SUCCESS)
	{
		len = sizeof(mFlags);
		ret = RegQueryValueEx(hKey, "Debug", NULL, &type, (LPBYTE)&mFlags, &len);
	}

	msg ("\n---------------------------------------------------\n"
		"mIDA Plugin v%s\n"
		"Copyright (C) 2006, Tenable Network Security\n"
		"---------------------------------------------------\n\n",  MIDA_VERSION);

	midl_list = midl_scan();
	tmp_list = midl_list;

	options = get_plugin_options("ofile");

	if (options == NULL)
	{
		ofile = NULL;

		while (tmp_list && tmp_list->mi)
		{
			mi = tmp_list->mi;

			qsnprintf ((char *)uuid_name, sizeof(uuid_name), "%.8x-%.4x-%.4x-%.2x%.2x-%.2x%.2x%.2x%.2x%.2x%.2x v%d.%d",
				mi->uuid.x1, mi->uuid.x2, mi->uuid.x3,
				mi->uuid.x4[0],mi->uuid.x4[1],
				mi->uuid.x4[2],mi->uuid.x4[3],mi->uuid.x4[4],mi->uuid.x4[5],mi->uuid.x4[6],mi->uuid.x4[7],
				mi->uuid.major, mi->uuid.minor);

			if (mi->list != NULL)
			{
			choose2(
				CH_MULTI,				// flags
				-1,-1,-1,-1,			// autoposition
				(void*)mi,				// pass the created function list to the window
				qnumber(header_fct),	// number of columns
				widths_fct,				// widths of columns
				sizer_fct,				// function that returns number of lines
				desc_fct,				// function that generates a line
				(const char*)uuid_name,	// window title
				-1,						// use the default icon for the window
				0,						// position the cursor on the first line
				decompile_fct,			// "kill" callback
				decompile_all_fct,		// "new" callback
				NULL,					// "update" callback
				edit_fct,				// "edit" callback
				enter_cb_fct,			// function to call when the user pressed Enter
				close_fct,				// function to call when the window is closed
				popup_fct,				// use default popup menu items
				NULL);					// use the same icon for all lines
			}
			tmp_list = tmp_list->next;
		}
	}
	else
	{
		ofile = (char *) options;

		while (tmp_list && tmp_list->mi)
		{
			decompile_all_fct ((void *)tmp_list->mi);
			tmp_list = tmp_list->next;
		}
	}

	free_interface_list (midl_list);
}


char IDAP_comment[] = "This is a MIDL decompiler plugin.";
char IDAP_help[] =
        "A MIDL decompiler plugin for IDA\n"
        "\n"
        "This module extract MIDL structures from exe or dll.\n";
char IDAP_name[] = "mIDA (MIDL Analyzer plugin for IDA)";
char IDAP_hotkey[] = "Ctrl-7";


// Exported Plugin Object
plugin_t PLUGIN =
{
  IDP_INTERFACE_VERSION,
  PLUGIN_PROC,
  IDAP_init,
  IDAP_term,
  IDAP_run,
  IDAP_comment,
  IDAP_help,
  IDAP_name,
  IDAP_hotkey
};