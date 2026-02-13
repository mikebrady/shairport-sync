/*
 * This file is part of Shairport Sync.
 * Copyright (c) Mike Brady 2019--2025
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#include "dbus-interface.h"
#include <popt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

GMainLoop *loop;

void on_properties_changed(__attribute__((unused)) GDBusProxy *proxy, GVariant *changed_properties,
                           const gchar *const *invalidated_properties, gpointer user_data) {
  /* Note that we are guaranteed that changed_properties and
   * invalidated_properties are never NULL
   */

  if (g_variant_n_children(changed_properties) > 0) {
    GVariantIter *iter;
    const gchar *key;
    GVariant *value;
    g_print(" *** Properties Changed:\n");
    g_variant_get(changed_properties, "a{sv}", &iter);
    while (g_variant_iter_loop(iter, "{&sv}", &key, &value)) {
      gchar *value_str;
      value_str = g_variant_print(value, TRUE);
      if (user_data)
        g_print("      %s.%s -> %s\n", (char *)user_data, key, value_str);
      else
        g_print("      %s -> %s\n", key, value_str);
      g_free(value_str);
    }
    g_variant_iter_free(iter);
  }

  if (g_strv_length((GStrv)invalidated_properties) > 0) {
    guint n;
    g_print(" *** Properties Invalidated:\n");
    for (n = 0; invalidated_properties[n] != NULL; n++) {
      const gchar *key = invalidated_properties[n];
      g_print("      %s\n", key);
    }
  }
}

void notify_loudness_callback(ShairportSync *proxy, __attribute__((unused)) gpointer user_data) {
  //  printf("\"notify_loudness_callback\" called with a gpointer of
  //  %lx.\n",(int64_t)user_data);
  gboolean ebl = shairport_sync_get_loudness(proxy);
  if (ebl == TRUE)
    printf("Client reports loudness is enabled.\n");
  else
    printf("Client reports loudness is disabled.\n");
}

void notify_loudness_threshold_callback(ShairportSync *proxy,
                                        __attribute__((unused)) gpointer user_data) {
  gdouble th = shairport_sync_get_loudness_threshold(proxy);
  printf("Client reports loudness threshold set to %.2f dB.\n", th);
}

void notify_volume_callback(ShairportSyncAdvancedRemoteControl *proxy,
                            __attribute__((unused)) gpointer user_data) {
  gdouble th = shairport_sync_advanced_remote_control_get_volume(proxy);
  printf("Client reports volume set to %.2f.\n", th);
}

pthread_t dbus_thread;
void *dbus_thread_func(__attribute__((unused)) void *arg) {

  loop = g_main_loop_new(NULL, FALSE);

  g_main_loop_run(loop);

  //  dbus_service_main(); // let it run inside a thread
  return NULL; // this is just to quieten a compiler warning.
}

int main(int argc, char *argv[]) {

  GBusType gbus_type_selected = G_BUS_TYPE_SYSTEM; // set default
  // get the options --system or --session for system bus or session bus
  signed char c;      /* used for argument parsing */
  poptContext optCon; /* context for parsing command-line options */

  struct poptOption optionsTable[] = {
      {"system", '\0', POPT_ARG_VAL, &gbus_type_selected, G_BUS_TYPE_SYSTEM,
       "Listen on the D-Bus system bus -- pick this option or the \'--session\' option, but not "
       "both. This is the default if no option is chosen.",
       NULL},
      {"session", '\0', POPT_ARG_VAL, &gbus_type_selected, G_BUS_TYPE_SESSION,
       "Listen on the D-Bus session bus -- pick this option or the \'--system\' option, but not "
       "both.",
       NULL},
      POPT_AUTOHELP{NULL, 0, 0, NULL, 0, NULL, NULL}};

  optCon = poptGetContext(NULL, argc, (const char **)argv, optionsTable, 0);
  poptSetOtherOptionHelp(optCon, "[--system | --session]");

  if (argc > 2) {
    poptPrintHelp(optCon, stderr, 0);
    exit(EXIT_FAILURE);
  }

  /* Now do options processing */
  while ((c = poptGetNextOpt(optCon)) >= 0) {
  }

  if (c < -1) {
    /* an error occurred during option processing */
    fprintf(stderr, "%s: %s\n", poptBadOption(optCon, POPT_BADOPTION_NOALIAS), poptStrerror(c));
    return 1;
  }

  poptFreeContext(optCon);

  printf("Listening on the D-Bus %s bus.\n",
         (gbus_type_selected == G_BUS_TYPE_SYSTEM) ? "system" : "session");

  pthread_create(&dbus_thread, NULL, &dbus_thread_func, NULL);

  ShairportSync *proxy;
  GError *error = NULL;

  proxy = shairport_sync_proxy_new_for_bus_sync(gbus_type_selected, G_DBUS_PROXY_FLAGS_NONE,
                                                "org.gnome.ShairportSync",
                                                "/org/gnome/ShairportSync", NULL, &error);

  // g_signal_connect(proxy, "notify::loudness-filter-active",
  // G_CALLBACK(notify_loudness_filter_active_callback), NULL);

  g_signal_connect(proxy, "g-properties-changed", G_CALLBACK(on_properties_changed),
                   "ShairportSync");
  g_signal_connect(proxy, "notify::loudness-threshold",
                   G_CALLBACK(notify_loudness_threshold_callback), "ShairportSync");

  // Now, add notification of changes in diagnostics

  ShairportSyncDiagnostics *proxy2;
  GError *error2 = NULL;
  proxy2 = shairport_sync_diagnostics_proxy_new_for_bus_sync(
      gbus_type_selected, G_DBUS_PROXY_FLAGS_NONE, "org.gnome.ShairportSync",
      "/org/gnome/ShairportSync", NULL, &error2);
  g_signal_connect(proxy2, "g-properties-changed", G_CALLBACK(on_properties_changed),
                   "ShairportSync.Diagnostics");

  // Now, add notification of changes in remote control

  ShairportSyncRemoteControl *proxy3;
  GError *error3 = NULL;
  proxy3 = shairport_sync_remote_control_proxy_new_for_bus_sync(
      gbus_type_selected, G_DBUS_PROXY_FLAGS_NONE, "org.gnome.ShairportSync",
      "/org/gnome/ShairportSync", NULL, &error3);
  g_signal_connect(proxy3, "g-properties-changed", G_CALLBACK(on_properties_changed),
                   "ShairportSync.RemoteControl");

  ShairportSyncAdvancedRemoteControl *proxy4;
  GError *error4 = NULL;
  proxy4 = shairport_sync_advanced_remote_control_proxy_new_for_bus_sync(
      gbus_type_selected, G_DBUS_PROXY_FLAGS_NONE, "org.gnome.ShairportSync",
      "/org/gnome/ShairportSync", NULL, &error4);
  g_signal_connect(proxy4, "g-properties-changed", G_CALLBACK(on_properties_changed),
                   "ShairportSync.AdvancedRemoteControl");
  g_signal_connect(proxy4, "notify::volume", G_CALLBACK(notify_volume_callback),
                   "ShairportSync.AdvancedRemoteControl");

  g_print("Starting test...\n");

  g_print("Using the RemoteControl interface, play for five seconds, pause for five seconds and "
          "then resume play...\n");
  g_print("Play...\n");
  shairport_sync_remote_control_call_play(SHAIRPORT_SYNC_REMOTE_CONTROL(proxy3), NULL, NULL, 0);
  sleep(5);
  g_print("Pause...\n");
  shairport_sync_remote_control_call_pause(SHAIRPORT_SYNC_REMOTE_CONTROL(proxy3), NULL, NULL, 0);
  sleep(5);
  g_print("Play...\n");
  shairport_sync_remote_control_call_play(SHAIRPORT_SYNC_REMOTE_CONTROL(proxy3), NULL, NULL, 0);
  sleep(5);
  g_print("Using the RemoteControl interface, set AirPlay Volume (range -30 to 0) to -30, -20, "
          "-10, 0 and -15 for five seconds each...\n");
  g_print("Set AirPlay Volume (range -30 to 0) to -30\n");
  shairport_sync_remote_control_call_set_airplay_volume(SHAIRPORT_SYNC_REMOTE_CONTROL(proxy3), -30,
                                                        NULL, NULL, 0);
  sleep(5);
  g_print("Set AirPlay Volume (range -30 to 0) to -20\n");
  shairport_sync_remote_control_call_set_airplay_volume(SHAIRPORT_SYNC_REMOTE_CONTROL(proxy3), -20,
                                                        NULL, NULL, 0);
  sleep(5);
  g_print("Set AirPlay Volume (range -30 to 0) to -10\n");
  shairport_sync_remote_control_call_set_airplay_volume(SHAIRPORT_SYNC_REMOTE_CONTROL(proxy3), -10,
                                                        NULL, NULL, 0);
  sleep(5);
  g_print("Set AirPlay Volume (range -30 to 0) to -0\n");
  shairport_sync_remote_control_call_set_airplay_volume(SHAIRPORT_SYNC_REMOTE_CONTROL(proxy3), 0,
                                                        NULL, NULL, 0);
  sleep(5);
  g_print("Set AirPlay Volume (range -30 to 0) to -15\n");
  shairport_sync_remote_control_call_set_airplay_volume(SHAIRPORT_SYNC_REMOTE_CONTROL(proxy3), -15,
                                                        NULL, NULL, 0);
  sleep(5);

  g_print("Using the AdvancedRemoteControl interface, set Volume to 20%%, 100%%, 40%% and 60%% for "
          "five seconds each...\n");
  g_print("Set Volume to 20%%\n");
  shairport_sync_advanced_remote_control_call_set_volume(
      SHAIRPORT_SYNC_ADVANCED_REMOTE_CONTROL(proxy4), 20, NULL, NULL, 0);
  sleep(5);
  g_print("Set Volume to 100%%\n");
  shairport_sync_advanced_remote_control_call_set_volume(
      SHAIRPORT_SYNC_ADVANCED_REMOTE_CONTROL(proxy4), 100, NULL, NULL, 0);
  sleep(5);
  g_print("Set Volume to 40%%\n");
  shairport_sync_advanced_remote_control_call_set_volume(
      SHAIRPORT_SYNC_ADVANCED_REMOTE_CONTROL(proxy4), 40, NULL, NULL, 0);
  sleep(5);
  g_print("Set Volume to 50%%\n");
  shairport_sync_advanced_remote_control_call_set_volume(
      SHAIRPORT_SYNC_ADVANCED_REMOTE_CONTROL(proxy4), 50, NULL, NULL, 0);

  sleep(5);
  g_print("Using the RemoteControl interface, increase volume for five seconds...\n");
  shairport_sync_remote_control_call_volume_up(SHAIRPORT_SYNC_REMOTE_CONTROL(proxy3), NULL, NULL,
                                               NULL);
  sleep(5);
  g_print("Using the RemoteControl interface, decrease volume...\n");
  shairport_sync_remote_control_call_volume_down(SHAIRPORT_SYNC_REMOTE_CONTROL(proxy3), NULL, NULL,
                                                 NULL);

  /*
  // sleep(1);
    shairport_sync_set_loudness_filter_active(SHAIRPORT_SYNC(proxy), TRUE);
    sleep(10);
    shairport_sync_set_loudness_threshold(SHAIRPORT_SYNC(proxy), -20.0);
    sleep(5);
    shairport_sync_set_loudness_filter_active(SHAIRPORT_SYNC(proxy), FALSE);
    sleep(5);
    shairport_sync_set_loudness_filter_active(SHAIRPORT_SYNC(proxy), TRUE);
    sleep(5);
    shairport_sync_set_loudness_threshold(SHAIRPORT_SYNC(proxy), -10.0);
    sleep(10);
    shairport_sync_set_loudness_filter_active(SHAIRPORT_SYNC(proxy), FALSE);
    sleep(1);

    shairport_sync_call_remote_command(SHAIRPORT_SYNC(proxy), "string",NULL,NULL,NULL);
    */
  sleep(1);
  g_print("Finished test. Listening for property changes...\n");
  // g_main_loop_quit(loop);
  pthread_join(dbus_thread, NULL);
  printf("exiting program.\n");

  g_object_unref(proxy);

  return 0;
}
