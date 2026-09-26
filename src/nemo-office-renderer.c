/* nemo-office-renderer.c - Drives nemo-office-preview for the preview pane.
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

#include <config.h>

#include "nemo-office-renderer.h"

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <glib/gstdio.h>

#define HELPER_PATH LIBEXECDIR "/nemo-office-preview"

/* Matches EXIT_NO_OFFICE in nemo-office-preview.c. */
#define HELPER_EXIT_NO_OFFICE 2

#define REQUEST_TIMEOUT_SECONDS 30
#define IDLE_TIMEOUT_SECONDS 60
#define MAX_CRASHES 3
#define PDF_CACHE_MAX_BYTES (200 * 1024 * 1024)
#define MAX_REPLY_BYTES (16 * 1024 * 1024)

struct _NemoOfficeRenderer {
    GObject parent_instance;

    GSubprocess *process;
    GSocketConnection *connection;

    GQueue *queue;
    GTask *current;
    guint32 header[2];
    GBytes *payload;

    guint timeout_id;
    guint idle_id;
    guint crashes;
    gboolean unavailable;
};

G_DEFINE_TYPE (NemoOfficeRenderer, nemo_office_renderer, G_TYPE_OBJECT)

static void process_next (NemoOfficeRenderer *self);

static void
stop_helper (NemoOfficeRenderer *self)
{
    g_clear_handle_id (&self->timeout_id, g_source_remove);
    g_clear_handle_id (&self->idle_id, g_source_remove);

    /* Closing our end is what tells the helper to quit. */
    if (self->connection != NULL) {
        g_io_stream_close (G_IO_STREAM (self->connection), NULL, NULL);
        g_clear_object (&self->connection);
    }

    g_clear_object (&self->process);
}

static void
fail_queue (NemoOfficeRenderer *self)
{
    GTask *task;

    while ((task = g_queue_pop_head (self->queue)) != NULL) {
        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                                 "LibreOffice is not available");
        g_object_unref (task);
    }
}

static void
helper_exited_cb (GObject      *source,
                  GAsyncResult *res,
                  gpointer      user_data)
{
    NemoOfficeRenderer *self = user_data;
    GSubprocess *process = G_SUBPROCESS (source);

    if (!g_subprocess_wait_finish (process, res, NULL)) {
        return;
    }

    if (g_subprocess_get_if_exited (process) &&
        g_subprocess_get_exit_status (process) == HELPER_EXIT_NO_OFFICE) {
        self->unavailable = TRUE;
        fail_queue (self);
    }
}

static gboolean
start_helper (NemoOfficeRenderer  *self,
              GError             **error)
{
    GSubprocessLauncher *launcher;
    GSocket *socket;
    gint fds[2];

    if (socketpair (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) < 0) {
        g_set_error_literal (error, G_IO_ERROR, g_io_error_from_errno (errno),
                             g_strerror (errno));
        return FALSE;
    }

    launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_NONE);
    g_subprocess_launcher_take_fd (launcher, fds[1], 3);
    self->process = g_subprocess_launcher_spawn (launcher, error, HELPER_PATH, NULL);
    g_object_unref (launcher);

    if (self->process == NULL) {
        close (fds[0]);
        return FALSE;
    }

    /* A GSocket rather than plain fd streams: it writes with MSG_NOSIGNAL, so
     * a helper that died doesn't take nemo along through SIGPIPE. */
    socket = g_socket_new_from_fd (fds[0], error);

    if (socket == NULL) {
        close (fds[0]);
        stop_helper (self);
        return FALSE;
    }

    self->connection = g_socket_connection_factory_create_connection (socket);
    g_object_unref (socket);

    g_subprocess_wait_async (self->process, NULL, helper_exited_cb, self);

    return TRUE;
}

static void
finish_current (NemoOfficeRenderer *self,
                GBytes             *payload,
                GError             *error)
{
    GTask *task = self->current;

    self->current = NULL;
    g_clear_handle_id (&self->timeout_id, g_source_remove);

    if (error != NULL) {
        g_task_return_error (task, error);
    } else {
        g_task_return_pointer (task, payload, (GDestroyNotify) g_bytes_unref);
    }

    g_object_unref (task);
    process_next (self);
}

static void
helper_failed (NemoOfficeRenderer *self)
{
    stop_helper (self);

    /* Some documents crash LibreOffice every time; don't keep feeding it. */
    if (++self->crashes >= MAX_CRASHES) {
        self->unavailable = TRUE;
    }

    finish_current (self, NULL,
                    g_error_new_literal (G_IO_ERROR, G_IO_ERROR_BROKEN_PIPE,
                                         "The office renderer stopped"));
}

static void
payload_read_cb (GObject      *source,
                 GAsyncResult *res,
                 gpointer      user_data)
{
    NemoOfficeRenderer *self = user_data;
    GBytes *payload = g_steal_pointer (&self->payload);
    gsize read;

    if (!g_input_stream_read_all_finish (G_INPUT_STREAM (source), res, &read, NULL) ||
        read != self->header[1]) {
        g_bytes_unref (payload);
        helper_failed (self);
        return;
    }

    self->crashes = 0;

    if (self->header[0] != 0) {
        GError *error = g_error_new (G_IO_ERROR, G_IO_ERROR_FAILED, "%.*s", (gint) read,
                                     (const gchar *) g_bytes_get_data (payload, NULL));

        g_bytes_unref (payload);
        finish_current (self, NULL, error);
        return;
    }

    finish_current (self, payload, NULL);
}

static void
header_read_cb (GObject      *source,
                GAsyncResult *res,
                gpointer      user_data)
{
    NemoOfficeRenderer *self = user_data;
    gsize read;

    if (!g_input_stream_read_all_finish (G_INPUT_STREAM (source), res, &read, NULL) ||
        read != sizeof (self->header) || self->header[1] > MAX_REPLY_BYTES) {
        helper_failed (self);
        return;
    }

    self->payload = g_bytes_new_take (g_malloc (self->header[1]), self->header[1]);

    /* The helper's reply is read whole even when the caller has given up on
     * it, or the next reply would start mid-stream. */
    g_input_stream_read_all_async (G_INPUT_STREAM (source),
                                   (void *) g_bytes_get_data (self->payload, NULL),
                                   self->header[1], G_PRIORITY_DEFAULT, NULL,
                                   payload_read_cb, self);
}

static gboolean
request_timeout_cb (gpointer user_data)
{
    NemoOfficeRenderer *self = user_data;

    self->timeout_id = 0;

    /* The pending read then fails and the helper is treated as crashed. */
    g_subprocess_force_exit (self->process);

    return G_SOURCE_REMOVE;
}

static gboolean
idle_timeout_cb (gpointer user_data)
{
    NemoOfficeRenderer *self = user_data;

    self->idle_id = 0;
    stop_helper (self);

    return G_SOURCE_REMOVE;
}

static void
process_next (NemoOfficeRenderer *self)
{
    const gchar *request;
    GError *error = NULL;
    guint32 size;

    /* One request at a time: the rest wait for its reply. */
    if (self->current != NULL) {
        return;
    }

    while (self->current == NULL) {
        GTask *task = g_queue_pop_head (self->queue);

        if (task == NULL) {
            if (self->process != NULL && self->idle_id == 0) {
                self->idle_id = g_timeout_add_seconds (IDLE_TIMEOUT_SECONDS,
                                                       idle_timeout_cb, self);
            }

            return;
        }

        /* Given up on while still queued: never sent at all. */
        if (g_task_return_error_if_cancelled (task)) {
            g_object_unref (task);
            continue;
        }

        if (self->unavailable) {
            g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                                     "LibreOffice is not available");
            g_object_unref (task);
            continue;
        }

        if (self->process == NULL && !start_helper (self, &error)) {
            self->unavailable = TRUE;
            g_task_return_error (task, error);
            g_object_unref (task);
            continue;
        }

        self->current = task;
    }

    g_clear_handle_id (&self->idle_id, g_source_remove);

    request = g_task_get_task_data (self->current);
    size = strlen (request);

    if (!g_output_stream_write_all (g_io_stream_get_output_stream (G_IO_STREAM (self->connection)),
                                    &size, sizeof (size), NULL, NULL, NULL) ||
        !g_output_stream_write_all (g_io_stream_get_output_stream (G_IO_STREAM (self->connection)),
                                    request, size, NULL, NULL, NULL)) {
        helper_failed (self);
        return;
    }

    self->timeout_id = g_timeout_add_seconds (REQUEST_TIMEOUT_SECONDS, request_timeout_cb, self);
    g_input_stream_read_all_async (g_io_stream_get_input_stream (G_IO_STREAM (self->connection)),
                                   self->header, sizeof (self->header), G_PRIORITY_DEFAULT, NULL,
                                   header_read_cb, self);
}

void
nemo_office_renderer_request_async (NemoOfficeRenderer  *self,
                                    const gchar         *request,
                                    GCancellable        *cancellable,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
    GTask *task;

    task = g_task_new (self, cancellable, callback, user_data);
    g_task_set_source_tag (task, nemo_office_renderer_request_async);
    g_task_set_task_data (task, g_strdup (request), g_free);

    g_queue_push_tail (self->queue, task);
    process_next (self);
}

GBytes *
nemo_office_renderer_request_finish (NemoOfficeRenderer  *self,
                                     GAsyncResult        *result,
                                     GError             **error)
{
    g_return_val_if_fail (g_task_is_valid (result, self), NULL);

    return g_task_propagate_pointer (G_TASK (result), error);
}

static gchar *
get_cache_dir (void)
{
    return g_build_filename (g_get_user_cache_dir (), "nemo", "office-preview", NULL);
}

typedef struct {
    gchar *path;
    guint64 size;
    guint64 mtime;
} CacheEntry;

static gint
compare_cache_entries (gconstpointer a,
                       gconstpointer b)
{
    const CacheEntry *ea = a, *eb = b;

    return (ea->mtime > eb->mtime) - (ea->mtime < eb->mtime);
}

/* Drops the oldest pdfs until the cache fits its budget again. */
static void
prune_cache_thread (GTask        *task,
                    gpointer      source,
                    gpointer      task_data,
                    GCancellable *cancellable)
{
    GFileEnumerator *enumerator;
    GFileInfo *info;
    GArray *entries;
    GFile *dir;
    gchar *dir_path;
    guint64 total = 0;
    guint i;

    dir_path = get_cache_dir ();
    dir = g_file_new_for_path (dir_path);
    enumerator = g_file_enumerate_children (dir,
                                            G_FILE_ATTRIBUTE_STANDARD_NAME ","
                                            G_FILE_ATTRIBUTE_STANDARD_SIZE ","
                                            G_FILE_ATTRIBUTE_TIME_MODIFIED,
                                            G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                            NULL, NULL);
    g_object_unref (dir);

    if (enumerator == NULL) {
        g_free (dir_path);
        return;
    }

    entries = g_array_new (FALSE, FALSE, sizeof (CacheEntry));

    while ((info = g_file_enumerator_next_file (enumerator, NULL, NULL)) != NULL) {
        CacheEntry entry;

        entry.path = g_build_filename (dir_path, g_file_info_get_name (info), NULL);
        entry.size = g_file_info_get_size (info);
        entry.mtime = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
        total += entry.size;
        g_array_append_val (entries, entry);
        g_object_unref (info);
    }

    g_object_unref (enumerator);
    g_array_sort (entries, compare_cache_entries);

    for (i = 0; i < entries->len; i++) {
        CacheEntry *entry = &g_array_index (entries, CacheEntry, i);

        if (total > PDF_CACHE_MAX_BYTES && g_unlink (entry->path) == 0) {
            total -= entry->size;
        }

        g_free (entry->path);
    }

    g_array_free (entries, TRUE);
    g_free (dir_path);
}

static void
prune_cache (void)
{
    GTask *task;

    task = g_task_new (NULL, NULL, NULL, NULL);
    g_task_run_in_thread (task, prune_cache_thread);
    g_object_unref (task);
}

static void
pdf_exported_cb (GObject      *source,
                 GAsyncResult *res,
                 gpointer      user_data)
{
    GTask *task = user_data;
    GBytes *payload;
    GError *error = NULL;

    payload = nemo_office_renderer_request_finish (NEMO_OFFICE_RENDERER (source), res, &error);

    if (payload == NULL) {
        g_task_return_error (task, error);
    } else {
        g_bytes_unref (payload);
        prune_cache ();
        g_task_return_pointer (task, g_strdup (g_task_get_task_data (task)), g_free);
    }

    g_object_unref (task);
}

void
nemo_office_renderer_get_pdf_async (NemoOfficeRenderer  *self,
                                    const gchar         *path,
                                    GCancellable        *cancellable,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
    GTask *task;
    GStatBuf st;
    gchar *dir, *key, *name, *pdf, *request;

    task = g_task_new (self, cancellable, callback, user_data);
    g_task_set_source_tag (task, nemo_office_renderer_get_pdf_async);

    if (g_stat (path, &st) != 0) {
        g_task_return_new_error (task, G_IO_ERROR, g_io_error_from_errno (errno),
                                 "%s", g_strerror (errno));
        g_object_unref (task);
        return;
    }

    /* Keyed on the modification time too, so an edited document is exported
     * again rather than shown as it was. */
    key = g_compute_checksum_for_string (G_CHECKSUM_MD5, path, -1);
    name = g_strdup_printf ("%s-%" G_GINT64_FORMAT ".pdf", key, (gint64) st.st_mtime);
    dir = get_cache_dir ();

    pdf = g_build_filename (dir, name, NULL);
    g_task_set_task_data (task, pdf, g_free);

    g_free (key);
    g_free (name);

    if (g_file_test (pdf, G_FILE_TEST_EXISTS)) {
        g_task_return_pointer (task, g_strdup (pdf), g_free);
        g_object_unref (task);
        g_free (dir);
        return;
    }

    g_mkdir_with_parents (dir, 0700);
    g_free (dir);

    /* Once sent, the export finishes and lands in the cache even if the
     * caller has moved on; only a request still queued is dropped. */
    request = g_strconcat ("export\t", path, "\t", pdf, NULL);
    nemo_office_renderer_request_async (self, request, cancellable, pdf_exported_cb, task);
    g_free (request);
}

gchar *
nemo_office_renderer_get_pdf_finish (NemoOfficeRenderer  *self,
                                     GAsyncResult        *result,
                                     GError             **error)
{
    g_return_val_if_fail (g_task_is_valid (result, self), NULL);

    return g_task_propagate_pointer (G_TASK (result), error);
}

static void
nemo_office_renderer_init (NemoOfficeRenderer *self)
{
    self->queue = g_queue_new ();
}

static void
nemo_office_renderer_class_init (NemoOfficeRendererClass *klass)
{
}

NemoOfficeRenderer *
nemo_office_renderer_get_default (void)
{
    static NemoOfficeRenderer *renderer = NULL;

    /* Lives as long as nemo; the helper behind it comes and goes. */
    if (renderer == NULL) {
        renderer = g_object_new (NEMO_TYPE_OFFICE_RENDERER, NULL);
    }

    return renderer;
}
