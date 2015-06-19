#ifdef USE_GTK_FILECHOOSER

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <dlfcn.h>
#include <gtk/gtk.h>

static void total_len_of_filenames(const char* file,
								   int *len)
{
	//printf("file: %s\n", file);
	*len += strlen(file) + 1; /* extra one for '\0' */
}

int os_create_file_selection (void 			*libgtk,
							  char 			*buf,
							  int 			len,
							  const char 	*title,
							  const char 	*init_dir,
							  int 			save,
							  int 			multiple)
{
	GtkWidget *dialog;

	GtkWidget* (*gtk_file_chooser_dialog_new)(const char *title,
											  GtkWindow *parent,
											  GtkFileChooserAction action,
											  const gchar *first_button_text,
											  ...)
		= dlsym(libgtk, "gtk_file_chooser_dialog_new");
	char * (*gtk_file_chooser_get_filename) (GtkFileChooser*)
		= dlsym(libgtk, "gtk_file_chooser_get_filename");
	void (*gtk_file_chooser_set_current_folder) (GtkFileChooser *chooser,
											   const gchar *name)
		= dlsym(libgtk, "gtk_file_chooser_set_current_folder");
	void (*gtk_widget_destroy) (GtkWidget *)
	   	= dlsym(libgtk, "gtk_widget_destroy");
	int (*gtk_dialog_run) (GtkDialog *)
		= dlsym(libgtk, "gtk_dialog_run");
	void (*g_print) (const char*, ...)
	   	= dlsym(libgtk, "g_print");
	guint (*gtk_dialog_get_type) (void) 
		= dlsym(libgtk, "gtk_dialog_get_type");
	GTypeInstance* (*g_type_check_instance_cast) (GTypeInstance *type_instance,
												  GType          iface_type)
		= dlsym(libgtk, "g_type_check_instance_cast");
	GType (*gtk_file_chooser_get_type)()
		= dlsym(libgtk, "gtk_file_chooser_get_type");
	void (*gtk_main_quit) (void)
		= dlsym(libgtk, "gtk_main_quit");
	void (*gtk_main) (void)
		= dlsym(libgtk, "gtk_main");

	void (*g_free)(void*)
		= dlsym(libgtk, "g_free");

	gboolean (*gtk_main_iteration_do) (gboolean blocking)
		= dlsym(libgtk, "gtk_main_iteration_do");

	void (*gtk_main_iteration) (void)
		= dlsym(libgtk, "gtk_main_iteration");
	gboolean (*gtk_events_pending) (void)
		= dlsym(libgtk, "gtk_events_pending");
	void (*gtk_file_chooser_set_select_multiple) (GtkFileChooser *chooser,
												  gboolean select_multiple)
		= dlsym(libgtk, "gtk_file_chooser_set_select_multiple");

	GSList *(*gtk_file_chooser_get_filenames) (GtkFileChooser *chooser)
		= dlsym(libgtk, "gtk_file_chooser_get_filenames");

	gchar * (*gtk_file_chooser_get_current_folder) (GtkFileChooser *chooser)
		= dlsym(libgtk, "gtk_file_chooser_get_current_folder");

	void (*g_slist_foreach) (GSList *list,
							 GFunc func,
							 gpointer user_data)
		= dlsym(libgtk, "g_slist_foreach");

	void (*g_slist_free) (GSList *list)
		= dlsym(libgtk, "g_slist_free");

	if (gtk_file_chooser_dialog_new == NULL
		|| gtk_file_chooser_get_filename == NULL
		|| gtk_file_chooser_set_select_multiple == NULL
		|| gtk_file_chooser_get_filenames == NULL
		|| gtk_file_chooser_get_current_folder == NULL
		|| gtk_file_chooser_set_current_folder == NULL
		|| g_slist_foreach == NULL
		|| g_slist_free == NULL
		|| gtk_widget_destroy == NULL
		|| gtk_dialog_run == NULL
		|| gtk_events_pending == NULL
		|| gtk_main_iteration == NULL
		|| g_print == NULL) {
		//printf("failed to find some symbols: %s\n", dlerror());
		return 0;
	}

	dialog = gtk_file_chooser_dialog_new (title == NULL? (save? "Save file" : "Open File") : title,
										  NULL,
										  save? GTK_FILE_CHOOSER_ACTION_SAVE : GTK_FILE_CHOOSER_ACTION_OPEN,
										  "_Cancel", GTK_RESPONSE_CANCEL,
										  save? "_Save" : "_Open", GTK_RESPONSE_ACCEPT,
										  NULL);
	if (multiple) {
		gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog), multiple);
	}
	if (init_dir != NULL) {
		gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), init_dir);
	}
	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
	{
		if (multiple) {
			char *dirname = getcwd(buf, len);
			int dir_len = dirname == NULL? 0 : strlen(dirname);
			GSList * list = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dialog));
			int max_len = 1 + dir_len;
			g_slist_foreach(list, (GFunc)total_len_of_filenames, &max_len);
			//g_print("total length should be %d\n", max_len);
			if (max_len > len - 1) { /* one extra for last empty string */
				max_len = len - 1;
			}
			if (dirname != NULL) {
				strncpy(buf, dirname, MIN(max_len, 1 + dir_len));
			} else {
				buf[0] = '\0';
			}
			GSList *ptr = list;
			char* next_pos = buf + MIN(max_len, dir_len + 1);
			max_len -= dir_len + 1;
			while (ptr != NULL && max_len > 0) {
#if 0
				int to_copy = MIN(max_len, strlen((char*)ptr->data) - (dir_len == 0 ? -1 : dir_len));
				strncpy (next_pos,
						 (char*)ptr->data + dir_len + (dir_len == 0? 0: 1), /* one for "/" */
						 to_copy);
#endif
				int to_copy = MIN(max_len, strlen((char*)ptr->data) + 1);
				strncpy (next_pos,
						 (char*)ptr->data,
						 to_copy);
						 
				next_pos += to_copy;
				max_len -= to_copy;
				g_free(ptr->data);
				ptr = ptr->next;
			}
			*next_pos = '\0'; /* make last string empty */
			g_slist_free(list);
			//g_free(dirname);
		} else {
			char *filename;
			filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
			strncpy(buf, filename, MIN(1 + strlen(filename), len));
			g_free (filename);
		}
	} else {
		assert(len > 1);
		buf[0] = buf[1] = '\0';
	}
	gtk_widget_destroy (dialog);
	while (gtk_events_pending ())
	  gtk_main_iteration ();
	//gtk_main_iteration_do(0);
	//gtk_main_iteration();
	return 1;
}

int os_init_gtk(void *libgtk)
{
	int (*gtk_init_check)(int *argc, char*** argv)
		= dlsym(libgtk, "gtk_init_check");
	if (gtk_init_check == NULL) {
		return -1;
	}
	int argc = 0;
	return gtk_init_check(&argc, NULL);
}

#endif //USE_GTK_FILECHOOSER
