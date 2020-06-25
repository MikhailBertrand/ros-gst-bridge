/* GStreamer
 * Copyright (C) 2020 FIXME <fixme@example.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */
/**
 * SECTION:element-gstrosaudiosink
 *
 * The rosaudiosink element audio data into ROS2.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 -v audiotestsrc ! rosaudiosink node_name="gst_audio" topic="/audiotopic"
 * ]|
 * Streams test tones as ROS audio messages on topic.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/audio/gstaudiosink.h>
#include "rosaudiosink.h"


GST_DEBUG_CATEGORY_STATIC (rosaudiosink_debug_category);
#define GST_CAT_DEFAULT rosaudiosink_debug_category

/* prototypes */


static void rosaudiosink_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void rosaudiosink_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void rosaudiosink_dispose (GObject * object);
static void rosaudiosink_finalize (GObject * object);

static gboolean rosaudiosink_open (GstAudioSink * sink);
static gboolean rosaudiosink_prepare (GstAudioSink * sink,
    GstAudioRingBufferSpec * spec);
static gboolean rosaudiosink_unprepare (GstAudioSink * sink);
static gboolean rosaudiosink_close (GstAudioSink * sink);
static gint rosaudiosink_write (GstAudioSink * sink, gpointer data,
    guint length);
static guint rosaudiosink_delay (GstAudioSink * sink);
static void rosaudiosink_reset (GstAudioSink * sink);

enum
{
  PROP_0,
  PROP_ROS_NAME,
  PROP_ROS_TOPIC,
  PROP_ROS_FRAME_ID,
  PROP_ROS_ENCODING
};


/* pad templates */

/* FIXME add/remove the formats that you want to support */
static GstStaticPadTemplate rosaudiosink_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw,format=S16LE,rate=[1,max],"
        "channels=[1,max],layout=interleaved")
    );


/* class initialization */

G_DEFINE_TYPE_WITH_CODE (Rosaudiosink, rosaudiosink, GST_TYPE_AUDIO_SINK,
    GST_DEBUG_CATEGORY_INIT (rosaudiosink_debug_category, "rosaudiosink", 0,
        "debug category for rosaudiosink element"))

static void
rosaudiosink_class_init (RosaudiosinkClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstAudioSinkClass *audio_sink_class = GST_AUDIO_SINK_CLASS (klass);

  /* Setting up pads and setting metadata should be moved to
     base_class_init if you intend to subclass this class. */
  gst_element_class_add_static_pad_template (element_class,
      &rosaudiosink_sink_template);


  gst_element_class_set_static_metadata (element_class,
      "rosaudiosink",
      "Sink",
      "a gstreamer sink that publishes audiodata into ROS",
      "BrettRD <brettrd@brettrd.com>");

  object_class->set_property = rosaudiosink_set_property;
  object_class->get_property = rosaudiosink_get_property;
  object_class->dispose = rosaudiosink_dispose;
  object_class->finalize = rosaudiosink_finalize;
  audio_sink_class->open = GST_DEBUG_FUNCPTR (rosaudiosink_open);
  audio_sink_class->prepare = GST_DEBUG_FUNCPTR (rosaudiosink_prepare);
  audio_sink_class->unprepare = GST_DEBUG_FUNCPTR (rosaudiosink_unprepare);
  audio_sink_class->close = GST_DEBUG_FUNCPTR (rosaudiosink_close);
  audio_sink_class->write = GST_DEBUG_FUNCPTR (rosaudiosink_write);
  audio_sink_class->delay = GST_DEBUG_FUNCPTR (rosaudiosink_delay);
  audio_sink_class->reset = GST_DEBUG_FUNCPTR (rosaudiosink_reset);

  //declaration of properties needs to happen *after* object_class->set_property
  g_object_class_install_property (object_class, PROP_ROS_NAME,
      g_param_spec_string ("ros-name", "node-name", "Name of the ROS node",
      "gst_audio_sink_node",
      (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS))
  );

  g_object_class_install_property (object_class, PROP_ROS_TOPIC,
      g_param_spec_string ("ros-topic", "pub-topic", "ROS topic to be published on",
      "gst_audio_pub",
      (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS))
  );

  g_object_class_install_property (object_class, PROP_ROS_ENCODING,
      g_param_spec_string ("ros-encoding", "encoding-string", "A hack to flexibly set the encoding string",
      "16SC1",
      (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS))
  );

}

static void
rosaudiosink_init (Rosaudiosink * rosaudiosink)
{
  // Don't register the node or the publisher just yet,
  // wait for rosaudiosink_open()
  // XXX set defaults elsewhere to keep gst-inspect consistent
  rosaudiosink->node_name = g_strdup("gst_audio_sink_node");
  rosaudiosink->pub_topic = g_strdup("gst_audio_pub");
  rosaudiosink->frame_id = g_strdup("audio_frame");
  rosaudiosink->encoding = g_strdup("16SC1");

}

void
rosaudiosink_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  Rosaudiosink *rosaudiosink = GST_ROSAUDIOSINK (object);

  GST_DEBUG_OBJECT (rosaudiosink, "set_property");

  switch (property_id) {
    case PROP_ROS_NAME:
      if(rosaudiosink->node)
      {
        RCLCPP_ERROR(rosaudiosink->logger, "can't change node name once openned");
      }
      else
      {
        g_free(rosaudiosink->node_name);
        rosaudiosink->node_name = g_value_dup_string(value);
      }
      break;

    case PROP_ROS_TOPIC:
      if(rosaudiosink->node)
      {
        RCLCPP_ERROR(rosaudiosink->logger, "can't change topic name once openned");
      }
      else
      {
        g_free(rosaudiosink->pub_topic);
        rosaudiosink->pub_topic = g_value_dup_string(value);
      }
      break;

    case PROP_ROS_FRAME_ID:
      g_free(rosaudiosink->frame_id);
      rosaudiosink->frame_id = g_value_dup_string(value);
      break;

    case PROP_ROS_ENCODING:
      g_free(rosaudiosink->encoding);
      rosaudiosink->encoding = g_value_dup_string(value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
rosaudiosink_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  Rosaudiosink *rosaudiosink = GST_ROSAUDIOSINK (object);

  GST_DEBUG_OBJECT (rosaudiosink, "get_property");
  switch (property_id) {
    case PROP_ROS_NAME:
      g_value_set_string(value, rosaudiosink->node_name);
      break;

    case PROP_ROS_TOPIC:
      g_value_set_string(value, rosaudiosink->pub_topic);
      break;

    case PROP_ROS_FRAME_ID:
      g_value_set_string(value, rosaudiosink->frame_id);
      break;

    case PROP_ROS_ENCODING:
      g_value_set_string(value, rosaudiosink->encoding);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
rosaudiosink_dispose (GObject * object)
{
  Rosaudiosink *rosaudiosink = GST_ROSAUDIOSINK (object);

  GST_DEBUG_OBJECT (rosaudiosink, "dispose");

  /* clean up as possible.  may be called multiple times */

  G_OBJECT_CLASS (rosaudiosink_parent_class)->dispose (object);
}

void
rosaudiosink_finalize (GObject * object)
{
  Rosaudiosink *rosaudiosink = GST_ROSAUDIOSINK (object);

  GST_DEBUG_OBJECT (rosaudiosink, "finalize");

  /* clean up object here */

  G_OBJECT_CLASS (rosaudiosink_parent_class)->finalize (object);
}

/* open the device with given specs */
static gboolean
rosaudiosink_open (GstAudioSink * sink)
{
  Rosaudiosink *rosaudiosink = GST_ROSAUDIOSINK (sink);

  GST_DEBUG_OBJECT (rosaudiosink, "open");

  rclcpp::init(0, NULL);
  rosaudiosink->node = std::make_shared<rclcpp::Node>(rosaudiosink->node_name);
  rosaudiosink->pub = rosaudiosink->node->create_publisher<audio_msgs::msg::Audio>(rosaudiosink->pub_topic, 1);
  rosaudiosink->logger = rosaudiosink->node->get_logger();
  rosaudiosink->clock = rosaudiosink->node->get_clock();


  return TRUE;
}

/* prepare resources and state to operate with the given specs */
static gboolean
rosaudiosink_prepare (GstAudioSink * sink, GstAudioRingBufferSpec * spec)
{
  Rosaudiosink *rosaudiosink = GST_ROSAUDIOSINK (sink);

  GST_DEBUG_OBJECT (rosaudiosink, "prepare");

  if(rosaudiosink->node)
      RCLCPP_INFO(rosaudiosink->logger, "preparing audio with caps '%s', format '%s'",
          gst_caps_to_string(spec->caps), gst_audio_format_to_string(spec->info.finfo->format));

  //collect a bunch of parameters to shoehorn into a message format
  rosaudiosink->channels = spec->info.channels;  //int number of channels
  rosaudiosink->stride = spec->info.bpf;
  rosaudiosink->endianness = spec->info.finfo->endianness;
  rosaudiosink->sample_rate = spec->info.rate;
  rosaudiosink->layout = spec->info.layout;
  return TRUE;
}

/* undo anything that was done in prepare() */
static gboolean
rosaudiosink_unprepare (GstAudioSink * sink)
{
  Rosaudiosink *rosaudiosink = GST_ROSAUDIOSINK (sink);

  GST_DEBUG_OBJECT (rosaudiosink, "unprepare");

  return TRUE;
}

/* close the device */
static gboolean
rosaudiosink_close (GstAudioSink * sink)
{
  Rosaudiosink *rosaudiosink = GST_ROSAUDIOSINK (sink);

  GST_DEBUG_OBJECT (rosaudiosink, "close");

  rosaudiosink->clock.reset();
  rosaudiosink->pub.reset();
  rosaudiosink->node.reset();
  rclcpp::shutdown();
  return TRUE;
}

/* write samples to the device */
static gint
rosaudiosink_write (GstAudioSink * sink, gpointer data, guint length)
{
  Rosaudiosink *rosaudiosink = GST_ROSAUDIOSINK (sink);

  GST_DEBUG_OBJECT (rosaudiosink, "write");

  /*
  bool all_zero = true;
  bool all_same = true;
  for(guint i=0; i<length; i++)
  {
    if(((uint8_t*)data)[i] != 0)
    {
      all_zero = false;
    }
    if(((uint8_t*)data)[i] != ((uint8_t*)data)[0])
    {
      all_same = false;
    }
  }

  if(all_zero)
  {
    RCLCPP_INFO(rosaudiosink->logger, "data is all zero");
  }
  else
  {
    RCLCPP_INFO(rosaudiosink->logger, "data is non zero");
  }
  if(all_same)
  {
    RCLCPP_INFO(rosaudiosink->logger, "data is all same");
  }
  else
  {
    RCLCPP_INFO(rosaudiosink->logger, "data differs");
  }
  */


  //create a message (this loan should be extended upstream)
  // need to use fixed data length message to benefit from zero-copy
  
  //auto msg = rosaudiosink->pub->borrow_loaned_message();
  auto msg = audio_msgs::msg::Audio();

  //fill the blanks
  msg.frames = length/rosaudiosink->stride;
  msg.channels = rosaudiosink->channels;    
  msg.sample_rate = rosaudiosink->sample_rate;
  msg.encoding = rosaudiosink->encoding;
  msg.is_bigendian = (rosaudiosink->endianness == G_BIG_ENDIAN);
  msg.layout = rosaudiosink->layout;
  msg.step = rosaudiosink->stride;
  msg.header.stamp = rosaudiosink->clock->now();
  msg.header.frame_id = rosaudiosink->frame_id;
  msg.data.resize(length);
  memcpy(msg.data.data(), data, length);


  //msg.get().data = std::vector<uint8_t>(length);
  //msg.get().data = std::vector<uint8_t>((uint8_t*)data, &((uint8_t*)data)[length]);

  //publish
  rosaudiosink->pub->publish(msg);

  return length;
}

/* get number of samples queued in the device */
static guint
rosaudiosink_delay (GstAudioSink * sink)
{
  Rosaudiosink *rosaudiosink = GST_ROSAUDIOSINK (sink);

  GST_DEBUG_OBJECT (rosaudiosink, "delay");

  return 0;
}

/* reset the audio device, unblock from a write */
static void
rosaudiosink_reset (GstAudioSink * sink)
{
  Rosaudiosink *rosaudiosink = GST_ROSAUDIOSINK (sink);

  GST_DEBUG_OBJECT (rosaudiosink, "reset");

}

