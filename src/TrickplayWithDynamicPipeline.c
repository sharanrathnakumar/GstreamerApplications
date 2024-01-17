#include <string.h>
#include <stdio.h>
#include <gst/gst.h>

typedef struct _CustomData
{
  GstElement *pipeline;
  GstElement *source;
  GstElement *decoder;
  GstElement *convert;
  GstElement *resample;
  GstElement *sink;
  GMainLoop *loop;

  gboolean playing;             /* Playing or Paused */
  gdouble rate;                 /* Current playback rate (can be negative) */
} CustomData;


static void pad_added_handler(GstElement *src, GstPad *new_pad, CustomData *data);

/* Send seek event to change rate */
static void
send_seek_event (CustomData * data)
{
  gint64 position;
  GstEvent *seek_event;

  GstState state;
  gst_element_get_state(data->pipeline, &state, NULL, GST_CLOCK_TIME_NONE);

  if (state != GST_STATE_PLAYING) {
	  // If the pipeline is not in the PLAYING state, transition to it.
	  GstStateChangeReturn ret = gst_element_set_state(data->pipeline, GST_STATE_PLAYING);
	  if (ret == GST_STATE_CHANGE_FAILURE) {
		  g_printerr("Unable to set the pipeline to the playing state.\n");
		  return;
	  }
  }

  /* Obtain the current position, needed for the seek event */
  if (!gst_element_query_position (data->pipeline, GST_FORMAT_TIME, &position)) {
	  g_printerr ("Unable to retrieve current position.\n");
	  return;
  }

  g_print("Current position: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS(position));
  /* Create the seek event */
  if (data->rate > 0) {
	  seek_event =
		  gst_event_new_seek (data->rate, GST_FORMAT_TIME,
				  GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE, GST_SEEK_TYPE_SET,
				  position, GST_SEEK_TYPE_END, 0);
  } else {
	  seek_event =
		  gst_event_new_seek (data->rate, GST_FORMAT_TIME,
				  GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE, GST_SEEK_TYPE_SET, 0,
				  GST_SEEK_TYPE_SET, position);
  }

  if (data->sink == NULL) {
	  /* If we have not done so, obtain the sink through which we will send the seek events */
	  g_object_get (data->pipeline, "sink", &data->sink, NULL);
  }

  /* Send the event */
  gst_element_send_event (data->sink, seek_event);

  g_print ("Current rate: %g\n", data->rate);
}

/* Process keyboard input */
	static gboolean
handle_keyboard (GIOChannel * source, GIOCondition cond, CustomData * data)
{
	gchar *str = NULL;

	if (g_io_channel_read_line (source, &str, NULL, NULL,
				NULL) != G_IO_STATUS_NORMAL) {
		return TRUE;
	}

	switch (g_ascii_tolower (str[0])) {
		case 'p':
			data->playing = !data->playing;
			gst_element_set_state (data->pipeline,
					data->playing ? GST_STATE_PLAYING : GST_STATE_PAUSED);
			g_print ("Setting state to %s\n", data->playing ? "PLAYING" : "PAUSE");
			break;
		case 's':
			if (g_ascii_isupper (str[0])) {
				data->rate = 4.0;
			} else {
				data->rate = 20.0;
			}
			send_seek_event (data);
			break;
		case 'd':
			data->rate *= -1.0;
			send_seek_event (data);
			break;
		case 'n':
			if (data->sink == NULL) {
				/* If we have not done so, obtain the sink through which we will send the step events */
				g_object_get (data->pipeline, "sink", &data->sink, NULL);
			}

			gst_element_send_event (data->sink,
					gst_event_new_step (GST_FORMAT_BUFFERS, 1, ABS (data->rate), TRUE,
						FALSE));
			g_print ("Stepping one frame\n");
			break;
		case 'q':
			g_main_loop_quit (data->loop);
			break;
		default:
			break;
	}

	g_free (str);

	return TRUE;
}

	int
tutorial_main (int argc, char *argv[])
{
	CustomData data;
  GstStateChangeReturn ret;
  GIOChannel *io_stdin;

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  if (argc < 2) {
	  g_printerr("Error: No argument passed.\nUsage : application <mediafile.mp4>\n");
	  return -1;
  }
  /* Initialize our data structure */
  memset (&data, 0, sizeof (data));

  /* Print usage map */
  g_print ("USAGE: Choose one of the following options, then press enter:\n"
      " 'P' to toggle between PAUSE and PLAY\n"
      " 'S' to increase playback speed, 's' to decrease playback speed\n"
      " 'D' to toggle playback direction\n"
      " 'N' to move to next frame (in the current direction, better in PAUSE)\n"
      " 'Q' to quit\n");

  /* Build the pipeline */
 /* data.pipeline =
      gst_parse_launch
      ("playbin uri=https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm",
      NULL);
*/

  data.source = gst_element_factory_make("filesrc", "source");
  data.decoder = gst_element_factory_make("decodebin", "decoder");
  data.convert = gst_element_factory_make("audioconvert", "convert");
  data.resample = gst_element_factory_make("audioresample", "resample");
  data.sink = gst_element_factory_make("autoaudiosink", "sink");

  /*Create empty pipeline*/
  data.pipeline = gst_pipeline_new("new-pipeline");

  if(!data.pipeline || !data.source || !data.decoder || !data.convert || !data.resample || !data.sink) {
	  g_print("NOt all elements could be created\n");
	  return -1;
  }
 
  /* Build the pipeline, Note we are NOT linking the source at this
   * point. We will do it later */
  gst_bin_add_many(GST_BIN (data.pipeline), data.source, data.decoder, data.convert, data.resample, data.sink, NULL);

  if( !gst_element_link_many(data.convert, data.resample, data.sink, NULL)) {
	  g_printerr("Elements could not be linked.\n");
	  gst_object_unref(data.pipeline);
	  return -1;
  }
 
  /* Set the URI to play */
  g_object_set(data.source, "location", argv[1], NULL);

  /* Connect to the pad added signal */
  g_signal_connect(data.source, "pad-added", G_CALLBACK ( pad_added_handler), &data);

  /* Add a keyboard watch so we get notified of keystrokes */
  io_stdin = g_io_channel_unix_new (fileno (stdin));
  g_io_add_watch (io_stdin, G_IO_IN, (GIOFunc) handle_keyboard, &data);

  /* Start playing */
  ret = gst_element_set_state (data.pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Unable to set the pipeline to the playing state.\n");
    gst_object_unref (data.pipeline);
    return -1;
  }

  data.playing = TRUE;
  data.rate = 4.0;

  /* Create a GLib Main Loop and set it to run */
  data.loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (data.loop);

  /* Free resources */
  g_main_loop_unref (data.loop);
  g_io_channel_unref (io_stdin);
  gst_element_set_state (data.pipeline, GST_STATE_NULL);
  if (data.sink != NULL)
    gst_object_unref (data.sink);
  gst_object_unref (data.pipeline);
  return 0;
}

/* This function will be called by the pad-added signal */
static void pad_added_handler(GstElement *src, GstPad *new_pad, CustomData *data ) {
	GstPad * sink_pad = gst_element_get_static_pad ( data->convert, "sink");
	GstPadLinkReturn ret;
	GstCaps *new_pad_caps = NULL;
	GstStructure *new_pad_struct = NULL;
	const gchar *new_pad_type = NULL;

	g_print ("Received new pad '%s' from '%s' : \n", GST_PAD_NAME (new_pad), GST_ELEMENT_NAME (src));

	/* If our converter is already linked, we have nothing to do here */
	if (gst_pad_is_linked (sink_pad)) {
		g_print ("We are already linked. Ignoring.\n");
		goto exit;
	}

	/* Attempt the link */
	new_pad_caps = gst_pad_get_current_caps (new_pad);
	new_pad_struct = gst_caps_get_structure (new_pad_caps, 0);
	new_pad_type = gst_structure_get_name (new_pad_struct);
	if(!g_str_has_prefix (new_pad_type, "audio/x-raw")) {
		g_print ("It has type '%s' which is not raw audio. Ignoring.\n", new_pad_type);
		goto exit;
	}

	/* Attempt the link */
	ret = gst_pad_link (new_pad, sink_pad);
	if( GST_PAD_LINK_FAILED (ret)) {
		g_print ("Type is '%s' but link failed.\n", new_pad_type);
	} else {
		g_print ("Link succeeded (type '%s').\n", new_pad_type);
	}

exit:
	/* Unreference the new pad's caps, if we got them */
	if (new_pad_caps != NULL)
		gst_caps_unref (new_pad_caps);

	/* Unreference the sink pad */
	gst_object_unref (sink_pad);
}

int
main (int argc, char *argv[])
{
  return tutorial_main (argc, argv);
}

