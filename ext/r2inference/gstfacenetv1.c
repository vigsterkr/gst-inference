/*
 * GStreamer
 * Copyright (C) 2019 RidgeRun
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

/**
 * SECTION:element-gstfacenetv1
 *
 * The facenetv1 element allows the user to infer/execute a pretrained model
 * based on the FaceNetV1 architecture on incoming image frames.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 -v videotestsrc ! facenetv1 ! xvimagesink
 * ]|
 * Process video frames from the camera using a FaceNet V1 model.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstfacenetv1.h"
#include "gst/r2inference/gstinferencemeta.h"
#include <string.h>
#include <math.h>
#include "gst/r2inference/gstinferencepreprocess.h"
#include "gst/r2inference/gstinferencepostprocess.h"
#include "gst/r2inference/gstinferencedebug.h"

GST_DEBUG_CATEGORY_STATIC (gst_facenetv1_debug_category);
#define GST_CAT_DEFAULT gst_facenetv1_debug_category

#define MODEL_CHANNELS 3

/* prototypes */
static void gst_facenetv1_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_facenetv1_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_facenetv1_dispose (GObject * object);
static void gst_facenetv1_finalize (GObject * object);

static gboolean gst_facenetv1_preprocess (GstVideoInference * vi,
    GstVideoFrame * inframe, GstVideoFrame * outframe);
static gboolean gst_facenetv1_postprocess (GstVideoInference * vi,
    const gpointer prediction, gsize predsize, GstMeta * meta_model[2],
    GstVideoInfo * info_model, gboolean * valid_prediction);
static gboolean gst_facenetv1_start (GstVideoInference * vi);
static gboolean gst_facenetv1_stop (GstVideoInference * vi);

enum
{
  PROP_0
};

/* pad templates */

#define CAPS "video/x-raw,format=RGB,width=160,height=160"

static GstStaticPadTemplate sink_model_factory =
GST_STATIC_PAD_TEMPLATE ("sink_model",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS (CAPS)
    );

static GstStaticPadTemplate src_model_factory =
GST_STATIC_PAD_TEMPLATE ("src_model",
    GST_PAD_SRC,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS (CAPS)
    );

struct _GstFacenetv1
{
  GstVideoInference parent;
};

struct _GstFacenetv1Class
{
  GstVideoInferenceClass parent;
};

/* class initialization */

G_DEFINE_TYPE_WITH_CODE (GstFacenetv1, gst_facenetv1,
    GST_TYPE_VIDEO_INFERENCE,
    GST_DEBUG_CATEGORY_INIT (gst_facenetv1_debug_category, "facenetv1", 0,
        "debug category for facenetv1 element"));

static void
gst_facenetv1_class_init (GstFacenetv1Class * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoInferenceClass *vi_class = GST_VIDEO_INFERENCE_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &sink_model_factory);
  gst_element_class_add_static_pad_template (element_class, &src_model_factory);

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "facenetv1", "Filter",
      "Infers incoming image frames using a pretrained FaceNet model",
      "Carlos Rodriguez <carlos.rodriguez@ridgerun.com> \n\t\t\t"
      "   Jose Jimenez <jose.jimenez@ridgerun.com> \n\t\t\t"
      "   Michael Gruner <michael.gruner@ridgerun.com>");

  gobject_class->set_property = gst_facenetv1_set_property;
  gobject_class->get_property = gst_facenetv1_get_property;
  gobject_class->dispose = gst_facenetv1_dispose;
  gobject_class->finalize = gst_facenetv1_finalize;

  vi_class->start = GST_DEBUG_FUNCPTR (gst_facenetv1_start);
  vi_class->stop = GST_DEBUG_FUNCPTR (gst_facenetv1_stop);
  vi_class->preprocess = GST_DEBUG_FUNCPTR (gst_facenetv1_preprocess);
  vi_class->postprocess = GST_DEBUG_FUNCPTR (gst_facenetv1_postprocess);
  vi_class->inference_meta_info = gst_classification_meta_get_info ();
}

static void
gst_facenetv1_init (GstFacenetv1 * facenetv1)
{
}

void
gst_facenetv1_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstFacenetv1 *facenetv1 = GST_FACENETV1 (object);

  GST_DEBUG_OBJECT (facenetv1, "set_property");

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_facenetv1_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstFacenetv1 *facenetv1 = GST_FACENETV1 (object);

  GST_DEBUG_OBJECT (facenetv1, "get_property");

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_facenetv1_dispose (GObject * object)
{
  GstFacenetv1 *facenetv1 = GST_FACENETV1 (object);

  GST_DEBUG_OBJECT (facenetv1, "dispose");

  /* clean up as possible.  may be called multiple times */

  G_OBJECT_CLASS (gst_facenetv1_parent_class)->dispose (object);
}

void
gst_facenetv1_finalize (GObject * object)
{
  GstFacenetv1 *facenetv1 = GST_FACENETV1 (object);

  GST_DEBUG_OBJECT (facenetv1, "finalize");

  /* clean up object here */

  G_OBJECT_CLASS (gst_facenetv1_parent_class)->finalize (object);
}

static gboolean
gst_facenetv1_preprocess (GstVideoInference * vi,
    GstVideoFrame * inframe, GstVideoFrame * outframe)
{
  GST_LOG_OBJECT (vi, "Preprocess");
  return gst_normalize_face (inframe, outframe, MODEL_CHANNELS);
}

static gboolean
gst_facenetv1_postprocess (GstVideoInference * vi, const gpointer prediction,
    gsize predsize, GstMeta * meta_model[2], GstVideoInfo * info_model,
    gboolean * valid_prediction)
{
  GstClassificationMeta *class_meta = (GstClassificationMeta *) meta_model[0];
  GstDebugLevel gst_debug_level = GST_LEVEL_LOG;

  GST_LOG_OBJECT (vi, "Postprocess");

  gst_fill_classification_meta (class_meta, prediction, predsize);

  gst_inference_print_embedding (vi, gst_facenetv1_debug_category, class_meta,
      prediction, gst_debug_level);
  *valid_prediction = TRUE;
  return TRUE;
}

static gboolean
gst_facenetv1_start (GstVideoInference * vi)
{
  GST_INFO_OBJECT (vi, "Starting Facenet v1");

  return TRUE;
}

static gboolean
gst_facenetv1_stop (GstVideoInference * vi)
{
  GST_INFO_OBJECT (vi, "Stopping Facenet v1");

  return TRUE;
}
