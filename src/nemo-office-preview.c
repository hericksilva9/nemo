/* nemo-office-preview.c - Renders office documents for the preview pane.
 *
 * Copyright (C) 2026 Linux Mint
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/* LibreOffice runs here rather than inside nemo, so a document that crashes
 * it only takes this process down.
 *
 * Requests arrive on fd 3 and replies leave on the same fd, one at a time.
 * A request is a native uint32 length followed by that many bytes of text:
 * a command and its arguments, separated by tabs.  A reply is a native uint32
 * status (0 for success), a uint32 length and that many bytes: text lines,
 * raw pixels for a tile, or an error message.
 *
 *   export <path> <pdf>        (empty), the document saved as pdf
 *   info   <path>              type, parts, current part, part names
 *   size   <path> <part>       width, height of that part
 *   tile   <path> <part> <pw> <ph> <x> <y> <w> <h>
 *                              pw*ph BGRA pixels of that twip area
 *
 * Every request names its document, so callers never share state here.  The
 * last one info, size or tile loaded stays open, as drawing a sheet takes
 * many tiles of the same one; export loads its own and leaves that alone.
 *
 * Sizes are in twips.  End of file on fd 3 means nemo is gone. */

#include <config.h>

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>

#define LOK_USE_UNSTABLE_API
#include <LibreOfficeKit/LibreOfficeKitInit.h>
#include <LibreOfficeKit/LibreOfficeKit.h>

#define LO_PROGRAM_DIR "/usr/lib/libreoffice/program"
#define CHANNEL_FD 3
#define MAX_REQUEST 4096
#define MAX_TILE_SIZE 1024

/* LibreOfficeKitEnums.h is C++ once the unstable API is on. */
#define DOCTYPE_SPREADSHEET 1

/* Exit status nemo reads as "LibreOffice isn't installed". */
#define EXIT_NO_OFFICE 2

static LibreOfficeKit *office = NULL;
static LibreOfficeKitDocument *document = NULL;
static gchar *document_path = NULL;

static gboolean
read_all (void  *buffer,
          gsize  size)
{
    guint8 *p = buffer;

    while (size > 0) {
        gssize n = read (CHANNEL_FD, p, size);

        if (n < 0 && errno == EINTR) {
            continue;
        }

        if (n <= 0) {
            return FALSE;
        }

        p += n;
        size -= n;
    }

    return TRUE;
}

static gboolean
write_all (const void *buffer,
           gsize       size)
{
    const guint8 *p = buffer;

    while (size > 0) {
        gssize n = write (CHANNEL_FD, p, size);

        if (n < 0 && errno == EINTR) {
            continue;
        }

        if (n <= 0) {
            return FALSE;
        }

        p += n;
        size -= n;
    }

    return TRUE;
}

static gboolean
reply (guint32      status,
       const void  *data,
       guint32      size)
{
    return write_all (&status, sizeof (status)) &&
           write_all (&size, sizeof (size)) &&
           write_all (data, size);
}

static gboolean
reply_text (const gchar *text)
{
    return reply (0, text, strlen (text));
}

static gboolean
reply_error (const gchar *message)
{
    if (message == NULL || *message == '\0') {
        message = "LibreOffice failed";
    }

    return reply (1, message, strlen (message));
}

static void
close_document (void)
{
    if (document != NULL) {
        document->pClass->destroy (document);
        document = NULL;
    }

    g_clear_pointer (&document_path, g_free);
}

static gchar *
take_error (void)
{
    gchar *error = office->pClass->getError (office);
    gchar *copy = g_strdup (error != NULL ? error : "");

    free (error);

    return copy;
}

/* Makes the document at @path the open one, showing @part (-1 for as it
 * was saved). */
static gboolean
use_document (const gchar  *path,
              gint          part,
              gchar       **error)
{
    if (g_strcmp0 (path, document_path) != 0) {
        close_document ();

        /* LibreOfficeKit never runs macros unless asked to at load time. */
        document = office->pClass->documentLoad (office, path);

        if (document == NULL) {
            *error = take_error ();
            return FALSE;
        }

        document_path = g_strdup (path);

        /* Only spreadsheets are drawn in tiles; the rest become pdfs. */
        if (document->pClass->getDocumentType (document) == DOCTYPE_SPREADSHEET) {
            document->pClass->initializeForRendering (document, NULL);
        }
    }

    if (part >= 0 && part != document->pClass->getPart (document)) {
        if (part >= document->pClass->getParts (document)) {
            *error = g_strdup ("No such part");
            return FALSE;
        }

        document->pClass->setPart (document, part);
    }

    return TRUE;
}

static gboolean
reply_use_error (gchar *error)
{
    gboolean ret = reply_error (error);

    g_free (error);

    return ret;
}

static gboolean
do_info (gchar **args)
{
    GString *out;
    gchar *error = NULL;
    gboolean ret;
    gint parts, i;

    if (!use_document (args[0], -1, &error)) {
        return reply_use_error (error);
    }

    parts = document->pClass->getParts (document);

    out = g_string_new (NULL);
    g_string_append_printf (out, "%d\n%d\n%d\n",
                            document->pClass->getDocumentType (document), parts,
                            document->pClass->getPart (document));

    for (i = 0; i < parts; i++) {
        gchar *name = document->pClass->getPartName (document, i);

        g_string_append_printf (out, "%s\n", name != NULL ? name : "");
        free (name);
    }

    ret = reply_text (out->str);
    g_string_free (out, TRUE);

    return ret;
}

static gboolean
do_size (gchar **args)
{
    gchar *error = NULL, *text;
    long width = 0, height = 0;
    gboolean ret;

    if (!use_document (args[0], atoi (args[1]), &error)) {
        return reply_use_error (error);
    }

    document->pClass->getDocumentSize (document, &width, &height);

    text = g_strdup_printf ("%ld\n%ld\n", width, height);
    ret = reply_text (text);
    g_free (text);

    return ret;
}

static gboolean
do_tile (gchar **args)
{
    guint8 *pixels;
    gchar *error = NULL;
    gboolean ret;
    gint pw, ph;

    pw = atoi (args[2]);
    ph = atoi (args[3]);

    if (pw <= 0 || ph <= 0 || pw > MAX_TILE_SIZE || ph > MAX_TILE_SIZE) {
        return reply_error ("Bad tile size");
    }

    if (!use_document (args[0], atoi (args[1]), &error)) {
        return reply_use_error (error);
    }

    pixels = g_malloc0 ((gsize) pw * ph * 4);
    document->pClass->paintTile (document, pixels, pw, ph,
                                 atoi (args[4]), atoi (args[5]),
                                 atoi (args[6]), atoi (args[7]));
    ret = reply (0, pixels, (guint32) pw * ph * 4);
    g_free (pixels);

    return ret;
}

static gboolean
do_export (gchar **args)
{
    LibreOfficeKitDocument *doc;
    gchar *tmp;
    gboolean saved;

    doc = office->pClass->documentLoad (office, args[0]);

    if (doc == NULL) {
        return reply_use_error (take_error ());
    }

    /* Written aside and moved in place, so nemo never finds half a pdf. */
    tmp = g_strconcat (args[1], ".tmp", NULL);
    saved = doc->pClass->saveAs (doc, tmp, "pdf", NULL) &&
            rename (tmp, args[1]) == 0;

    if (!saved) {
        unlink (tmp);
    }

    doc->pClass->destroy (doc);
    g_free (tmp);

    return saved ? reply_text ("") : reply_error ("Unable to export the document");
}

static gboolean
handle (gchar *request)
{
    gchar **args;
    guint n_args;
    gboolean ret;

    args = g_strsplit (request, "\t", -1);
    n_args = g_strv_length (args);

    if (n_args == 3 && g_str_equal (args[0], "export")) {
        ret = do_export (args + 1);
    } else if (n_args == 2 && g_str_equal (args[0], "info")) {
        ret = do_info (args + 1);
    } else if (n_args == 3 && g_str_equal (args[0], "size")) {
        ret = do_size (args + 1);
    } else if (n_args == 9 && g_str_equal (args[0], "tile")) {
        ret = do_tile (args + 1);
    } else {
        ret = reply_error ("Bad request");
    }

    g_strfreev (args);

    return ret;
}

int
main (int argc, char **argv)
{
    gchar *profile_dir, *profile;

    signal (SIGPIPE, SIG_IGN);

    /* LibreOffice prints to stdout now and then; keep it apart from nemo's. */
    dup2 (STDERR_FILENO, STDOUT_FILENO);

    profile_dir = g_build_filename (g_get_user_cache_dir (), "nemo", "lo-profile", NULL);
    profile = g_filename_to_uri (profile_dir, NULL, NULL);

    /* A profile of our own, or we would fight over the lock with the user's
     * LibreOffice. */
    office = lok_init_2 (LO_PROGRAM_DIR, profile);

    g_free (profile);
    g_free (profile_dir);

    if (office == NULL) {
        _exit (EXIT_NO_OFFICE);
    }

    for (;;) {
        guint32 size;
        gchar *request;
        gboolean ok;

        if (!read_all (&size, sizeof (size)) || size > MAX_REQUEST) {
            break;
        }

        request = g_malloc (size + 1);

        if (!read_all (request, size)) {
            g_free (request);
            break;
        }

        request[size] = '\0';
        ok = handle (request);
        g_free (request);

        if (!ok) {
            break;
        }
    }

    close_document ();
    office->pClass->destroy (office);

    /* LibreOffice crashes in its static destructors; skip them. */
    _exit (0);
}
