// point d'entrer graphiquement
#include <gtk/gtk.h>

static void on_insert(GtkWidget *widget, gpointer data)
{
    g_print("Insert clique\n");
}

int main(int argc, char *argv[])
{
    gtk_init(&argc, &argv);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "LSM-Tree Database");
    gtk_window_set_default_size(GTK_WINDOW(window), 400, 300);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *button = gtk_button_new_with_label("Insert data");
    g_signal_connect(button, "clicked", G_CALLBACK(on_insert), NULL);
    gtk_container_add(GTK_CONTAINER(window), button);

    gtk_widget_show_all(window);
    gtk_main();
    return 0;
}