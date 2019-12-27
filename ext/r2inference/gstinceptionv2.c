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
 * SECTION:element-gstinceptionv2
 *
 * The inceptionv2 element allows the user to infer/execute a pretrained model
 * based on the GoogLeNet (Inception v1 or Inception v2) architectures on 
 * incoming image frames.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 -v videotestsrc ! inceptionv2 ! xvimagesink
 * ]|
 * Process video frames from the camera using a GoogLeNet (Inception v1 or 
 * Inception v2) model.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstinceptionv2.h"
#include "gst/r2inference/gstinferencemeta.h"
#include <string.h>
#include "gst/r2inference/gstinferencepreprocess.h"
#include "gst/r2inference/gstinferencepostprocess.h"
#include "gst/r2inference/gstinferencedebug.h"

GST_DEBUG_CATEGORY_STATIC (gst_inceptionv2_debug_category);
#define GST_CAT_DEFAULT gst_inceptionv2_debug_category

#define MEAN 128.0
#define STD 1/128.0
#define MODEL_CHANNELS 3

/* prototypes */
static void gst_inceptionv2_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_inceptionv2_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_inceptionv2_dispose (GObject * object);
static void gst_inceptionv2_finalize (GObject * object);

static gboolean gst_inceptionv2_preprocess (GstVideoInference * vi,
    GstVideoFrame * inframe, GstVideoFrame * outframe);
static gboolean gst_inceptionv2_postprocess (GstVideoInference * vi,
    const gpointer prediction, gsize predsize, GstMeta * meta_model[2],
    GstVideoInfo * info_model, gboolean * valid_prediction);
static gboolean gst_inceptionv2_postprocess_old (GstVideoInference * vi,
    const gpointer prediction, gsize predsize, GstMeta * meta_model,
    GstVideoInfo * info_model, gboolean * valid_prediction);
static gboolean gst_inceptionv2_postprocess_new (GstVideoInference * vi,
    const gpointer prediction, gsize predsize, GstMeta * meta_model,
    GstVideoInfo * info_model, gboolean * valid_prediction);
static gboolean gst_inceptionv2_start (GstVideoInference * vi);
static gboolean gst_inceptionv2_stop (GstVideoInference * vi);

enum
{
  PROP_0
};

/* pad templates */
#define CAPS								\
  "video/x-raw, "							\
  "width=224, "							\
  "height=224, "							\
  "format={RGB, RGBx, RGBA, BGR, BGRx, BGRA, xRGB, ARGB, xBGR, ABGR}"

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

struct _GstInceptionv2
{
  GstVideoInference parent;
};

struct _GstInceptionv2Class
{
  GstVideoInferenceClass parent;
};

/* class initialization */

G_DEFINE_TYPE_WITH_CODE (GstInceptionv2, gst_inceptionv2,
    GST_TYPE_VIDEO_INFERENCE,
    GST_DEBUG_CATEGORY_INIT (gst_inceptionv2_debug_category, "inceptionv2", 0,
        "debug category for inceptionv2 element"));

static void
gst_inceptionv2_class_init (GstInceptionv2Class * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoInferenceClass *vi_class = GST_VIDEO_INFERENCE_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &sink_model_factory);
  gst_element_class_add_static_pad_template (element_class, &src_model_factory);

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "inceptionv2", "Filter",
      "Infers incoming image frames using a pretrained GoogLeNet (Inception v1 or Inception v2) model",
      "Carlos Rodriguez <carlos.rodriguez@ridgerun.com> \n\t\t\t"
      "   Jose Jimenez <jose.jimenez@ridgerun.com> \n\t\t\t"
      "   Michael Gruner <michael.gruner@ridgerun.com>");

  gobject_class->set_property = gst_inceptionv2_set_property;
  gobject_class->get_property = gst_inceptionv2_get_property;
  gobject_class->dispose = gst_inceptionv2_dispose;
  gobject_class->finalize = gst_inceptionv2_finalize;

  vi_class->start = GST_DEBUG_FUNCPTR (gst_inceptionv2_start);
  vi_class->stop = GST_DEBUG_FUNCPTR (gst_inceptionv2_stop);
  vi_class->preprocess = GST_DEBUG_FUNCPTR (gst_inceptionv2_preprocess);
  vi_class->postprocess = GST_DEBUG_FUNCPTR (gst_inceptionv2_postprocess);
  vi_class->inference_meta_info = gst_classification_meta_get_info ();
}

static void
gst_inceptionv2_init (GstInceptionv2 * inceptionv2)
{
}

void
gst_inceptionv2_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstInceptionv2 *inceptionv2 = GST_INCEPTIONV2 (object);

  GST_DEBUG_OBJECT (inceptionv2, "set_property");

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_inceptionv2_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstInceptionv2 *inceptionv2 = GST_INCEPTIONV2 (object);

  GST_DEBUG_OBJECT (inceptionv2, "get_property");

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_inceptionv2_dispose (GObject * object)
{
  GstInceptionv2 *inceptionv2 = GST_INCEPTIONV2 (object);

  GST_DEBUG_OBJECT (inceptionv2, "dispose");

  /* clean up as possible.  may be called multiple times */

  G_OBJECT_CLASS (gst_inceptionv2_parent_class)->dispose (object);
}

void
gst_inceptionv2_finalize (GObject * object)
{
  GstInceptionv2 *inceptionv2 = GST_INCEPTIONV2 (object);

  GST_DEBUG_OBJECT (inceptionv2, "finalize");

  /* clean up object here */

  G_OBJECT_CLASS (gst_inceptionv2_parent_class)->finalize (object);
}

static gboolean
gst_inceptionv2_preprocess (GstVideoInference * vi,
    GstVideoFrame * inframe, GstVideoFrame * outframe)
{
  GST_LOG_OBJECT (vi, "Preprocess");
  return gst_normalize (inframe, outframe, MEAN, STD, MODEL_CHANNELS);
}

static gboolean
gst_inceptionv2_postprocess_old (GstVideoInference * vi,
    const gpointer prediction, gsize predsize, GstMeta * meta_model,
    GstVideoInfo * info_model, gboolean * valid_prediction)
{
  GstClassificationMeta *class_meta = (GstClassificationMeta *) meta_model;
  GstDebugLevel gst_debug_level = GST_LEVEL_LOG;

  GST_LOG_OBJECT (vi, "Postprocess");

  gst_fill_classification_meta (class_meta, prediction, predsize);

  gst_inference_print_highest_probability (vi, gst_inceptionv2_debug_category,
      class_meta, prediction, gst_debug_level);

  *valid_prediction = TRUE;
  return TRUE;
}

static gboolean
gst_inceptionv2_postprocess_new (GstVideoInference * vi,
    const gpointer prediction, gsize predsize, GstMeta * meta_model,
    GstVideoInfo * info_model, gboolean * valid_prediction)
{
  GstInferenceMeta *imeta = NULL;
  GstInferenceClassification *c = NULL;
  GstInferencePrediction *root = NULL;

  g_return_val_if_fail (vi != NULL, FALSE);
  g_return_val_if_fail (meta_model != NULL, FALSE);
  g_return_val_if_fail (info_model != NULL, FALSE);

  GST_LOG_OBJECT (vi, "Postprocess Meta");

  imeta = (GstInferenceMeta *) meta_model;

  root = imeta->prediction;
  if (!root) {
    GST_ERROR_OBJECT (vi, "Prediction is not part of the Inference Meta");
    return FALSE;
  }

  c = gst_create_class_from_prediction (vi, prediction, predsize);
  gst_inference_prediction_append_classification (root, c);
  gst_inference_print_predictions (vi, gst_inceptionv2_debug_category, imeta);

  *valid_prediction = TRUE;
  return TRUE;
}

static gboolean
gst_inceptionv2_postprocess (GstVideoInference * vi, const gpointer prediction,
    gsize predsize, GstMeta * meta_model[2], GstVideoInfo * info_model,
    gboolean * valid_prediction)
{
  gboolean ret = TRUE;

  ret &=
      gst_inceptionv2_postprocess_old (vi, prediction, predsize, meta_model[0],
      info_model, valid_prediction);
  ret &=
      gst_inceptionv2_postprocess_new (vi, prediction, predsize, meta_model[1],
      info_model, valid_prediction);

  return ret;
}

static gboolean
gst_inceptionv2_start (GstVideoInference * vi)
{
  GST_INFO_OBJECT (vi, "Starting Inception v2");

  return TRUE;
}

static gboolean
gst_inceptionv2_stop (GstVideoInference * vi)
{
  GST_INFO_OBJECT (vi, "Stopping Inception v2");

  return TRUE;
}
