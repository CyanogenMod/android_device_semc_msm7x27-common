/*
** Copyright 2008, Google Inc.
** Copyright (c) 2009, Code Aurora Forum. All rights reserved.
** Copyright (c) 2010, Ricardo Cerqueira
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
**
** NOTICE - (RC)
**
** All alterations done to this file to add support for the Z71 terminal
** are intended for use with CyanogenMod. This includes all the support
** for ov5642, and the reverse engineered bits like ioctls and EXIF.
** Please do not change the EXIF header without asking me first.
*/

#define LOG_NDEBUG 1
#define LOG_NIDEBUG 1
#define LOG_TAG "QualcommCameraHardware"
#include <utils/Log.h>

#include "QualcommCameraHardware.h"

#include <utils/Errors.h>
#include <utils/threads.h>
#include <binder/MemoryHeapPmem.h>
#include <utils/String16.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <cutils/properties.h>
#include <math.h>
#if HAVE_ANDROID_OS
#include <linux/android_pmem.h>
#endif
#include <linux/ioctl.h>
#include <camera/CameraParameters.h>

#include "linux/msm_mdp.h"
#include <linux/fb.h>

#define LIKELY(exp)   __builtin_expect(!!(exp), 1)
#define UNLIKELY(exp) __builtin_expect(!!(exp), 0)

extern "C" {
#include <fcntl.h>
#include <time.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <assert.h>
#include <stdlib.h>
#include <ctype.h>
#include <signal.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/system_properties.h>
#include <sys/time.h>
#include <stdlib.h>

#include <msm_camera.h>

#define DEFAULT_PICTURE_WIDTH  640
#define DEFAULT_PICTURE_HEIGHT 480
#define THUMBNAIL_BUFFER_SIZE (THUMBNAIL_WIDTH * THUMBNAIL_HEIGHT * 3/2)
#define MAX_ZOOM_LEVEL 0
#define NOT_FOUND -1
// Number of video buffers held by kernal (initially 1,2 &3)
#define ACTIVE_VIDEO_BUFFERS 3

#include <dlfcn.h>

	void* (*LINK_cam_conf)(void *data);
	void* (*LINK_cam_frame)(void *data);
	bool  (*LINK_jpeg_encoder_init)();
	void  (*LINK_jpeg_encoder_join)();
	bool  (*LINK_jpeg_encoder_encode)(const cam_ctrl_dimension_t *dimen,
                                  const uint8_t *thumbnailbuf, int thumbnailfd,
                                  const uint8_t *snapshotbuf, int snapshotfd,
                                  common_crop_t *scaling_parms, exif_tags_info_t *exif_data,
                                  int exif_table_numEntries);
	void (*LINK_camframe_terminate)(void);

	int8_t (*LINK_jpeg_encoder_setMainImageQuality)(uint32_t quality);
	int8_t (*LINK_jpeg_encoder_setThumbnailQuality)(uint32_t quality);
const struct camera_size_type *(*LINK_default_sensor_get_snapshot_sizes)(int *len);
	int (*LINK_launch_cam_conf_thread)(void);
	int (*LINK_release_cam_conf_thread)(void);

// callbacks
	void  (**LINK_mmcamera_camframe_callback)(struct msm_frame *frame);
void  (**LINK_mmcamera_jpegfragment_callback)(uint8_t *buff_ptr,
                                              uint32_t buff_size);
	void  (**LINK_mmcamera_jpeg_callback)(jpeg_event_t status);
void  (**LINK_mmcamera_shutter_callback)(common_crop_t *crop);
	void  (**LINK_camframe_timeout_callback)(void);


} // extern "C"

#ifndef HAVE_CAMERA_SIZE_TYPE
struct camera_size_type {
    int width;
    int height;
};
#endif

typedef struct crop_info_struct {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
} zoom_crop_info;

union zoomimage
{
    char d[sizeof(struct mdp_blit_req_list) + sizeof(struct mdp_blit_req) * 1];
    struct mdp_blit_req_list list;
} zoomImage;

//Default to VGA
#define DEFAULT_PREVIEW_WIDTH 480
#define DEFAULT_PREVIEW_HEIGHT 320

/*
 * Modifying preview size requires modification
 * in bitmasks for boardproperties
 */

static const camera_size_type preview_sizes[] = {
    { 640, 480 }, // VGA
    { 480, 320 }, // HVGA
    { 384, 288 },
    { 352, 288 }, // CIF
    { 320, 240 }, // QVGA
    { 176, 144 }, // QCIF
};
#define PREVIEW_SIZE_COUNT (sizeof(preview_sizes)/sizeof(camera_size_type))

static camera_size_type supportedPreviewSizes[PREVIEW_SIZE_COUNT];
static unsigned int previewSizeCount;

board_property boardProperties[] = {
        {TARGET_MSM7627, 0x000006ff},
};

static const camera_size_type picture_sizes[] = {
// disabled untill we fix them !!
//    { 2592, 1944 }, // 5MP
//    { 2048, 1536 }, // 3MP
//    { 1632, 1224 }, // 2MP 
//    { 1280, 960 }, // 1MP
    { 640, 480 }, // VGA
};
static int PICTURE_SIZE_COUNT = sizeof(picture_sizes)/sizeof(camera_size_type);
static const camera_size_type * picture_sizes_ptr;
static int supportedPictureSizesCount;

#ifdef Q12
#undef Q12
#endif

#define Q12 4096

static targetType mCurrentTarget = TARGET_MSM7627;

typedef struct {
    uint32_t aspect_ratio;
    uint32_t width;
    uint32_t height;
} thumbnail_size_type;

static thumbnail_size_type thumbnail_sizes[] = {
    { 6826, 480, 288 }, //1.666667
    { 6144, 432, 288 }, //1.5
    { 5461, 512, 384 }, //1.333333
    { 5006, 352, 288 }, //1.222222
};
#define THUMBNAIL_SIZE_COUNT (sizeof(thumbnail_sizes)/sizeof(thumbnail_size_type))
#define DEFAULT_THUMBNAIL_SETTING 2
#define THUMBNAIL_WIDTH_STR "512"
#define THUMBNAIL_HEIGHT_STR "384"
#define THUMBNAIL_SMALL_HEIGHT 144

static int attr_lookup(const str_map arr[], int len, const char *name)
{
    if (name) {
        for (int i = 0; i < len; i++) {
            if (!strcmp(arr[i].desc, name))
                return arr[i].val;
        }
    }
    return NOT_FOUND;
}

// round to the next power of two
static inline unsigned clp2(unsigned x)
{
    x = x - 1;
    x = x | (x >> 1);
    x = x | (x >> 2);
    x = x | (x >> 4);
    x = x | (x >> 8);
    x = x | (x >>16);
    return x + 1;
}

static int exif_table_numEntries = 0;
#define MAX_EXIF_TABLE_ENTRIES 7
exif_tags_info_t exif_data[MAX_EXIF_TABLE_ENTRIES];
static zoom_crop_info zoomCropInfo;
static void *mLastQueuedFrame = NULL;
#define RECORD_BUFFERS_7x30 8
static int kRecordBufferCount;


namespace android {

static const int PICTURE_FORMAT_JPEG = 1;

#define DONT_CARE 0

static const str_map focus_modes[] = {
    { CameraParameters::FOCUS_MODE_AUTO,     AF_MODE_AUTO},
    { CameraParameters::FOCUS_MODE_INFINITY, DONT_CARE },
    { CameraParameters::FOCUS_MODE_NORMAL,   AF_MODE_NORMAL },
    { CameraParameters::FOCUS_MODE_MACRO,    AF_MODE_MACRO }
};

struct SensorType {
    const char *name;
    int rawPictureWidth;
    int rawPictureHeight;
    bool hasAutoFocusSupport;
    int max_supported_snapshot_width;
    int max_supported_snapshot_height;
    int bitMask;
};

static SensorType sensorTypes[] = {
        { "dlt001_camera", 2216, 1536, true, 2592, 1944 ,0x000007ff },
        { "dlt002_camera", 2224, 1536, true, 2048, 1536 ,0x000007ff } };


static SensorType * sensorType;

static const str_map picture_formats[] = {
        {CameraParameters::PIXEL_FORMAT_JPEG, PICTURE_FORMAT_JPEG}
};

static bool parameter_string_initialized = false;
static String8 preview_size_values;
static String8 picture_size_values;
static String8 focus_mode_values;
static String8 picture_format_values;
static String8 preview_frame_rate_values;

static String8 create_sizes_str(const camera_size_type *sizes, int len) {
    String8 str;
    char buffer[32];

    if (len > 0) {
        sprintf(buffer, "%dx%d", sizes[0].width, sizes[0].height);
        str.append(buffer);
    }
    for (int i = 1; i < len; i++) {
        sprintf(buffer, ",%dx%d", sizes[i].width, sizes[i].height);
        str.append(buffer);
    }
    return str;
}

static String8 create_values_str(const str_map *values, int len) {
    String8 str;

    if (len > 0) {
        str.append(values[0].desc);
    }
    for (int i = 1; i < len; i++) {
        str.append(",");
        str.append(values[i].desc);
    }
    return str;
}

static String8 create_values_range_str(int min, int max){
    String8 str;
    char buffer[32];

    if(min <= max){
        snprintf(buffer, sizeof(buffer), "%d", min);
        str.append(buffer);

        for (int i = min + 1; i <= max; i++) {
            snprintf(buffer, sizeof(buffer), ",%d", i);
            str.append(buffer);
        }
    }
    return str;
}

//-------------------------------------------------------------------------------------
static Mutex singleton_lock;
static bool singleton_releasing;
static nsecs_t singleton_releasing_start_time;
static const nsecs_t SINGLETON_RELEASING_WAIT_TIME = seconds_to_nanoseconds(5);
static const nsecs_t SINGLETON_RELEASING_RECHECK_TIMEOUT = seconds_to_nanoseconds(1);
static Condition singleton_wait;

static void receive_camframe_callback(struct msm_frame *frame);
static void receive_jpeg_fragment_callback(uint8_t *buff_ptr, uint32_t buff_size);
static void receive_jpeg_callback(jpeg_event_t status);
static void receive_shutter_callback(common_crop_t *crop);
static void receive_camframetimeout_callback(void);
static int fb_fd = -1;
static int32_t mMaxZoom = 0;
static bool native_get_maxzoom(int camfd, void *pZm);

static int dstOffset = 0;

static int camerafd;
pthread_t w_thread;

void *opencamerafd(void *data) {
    camerafd = open(MSM_CAMERA_CONTROL, O_RDWR);
    return NULL;
}

/* When using MDP zoom, double the preview buffers. The usage of these
 * buffers is as follows:
 * 1. As all the buffers comes under a single FD, and at initial registration,
 * this FD will be passed to surface flinger, surface flinger can have access
 * to all the buffers when needed.
 * 2. Only "kPreviewBufferCount" buffers (SrcSet) will be registered with the
 * camera driver to receive preview frames. The remaining buffers (DstSet),
 * will be used at HAL and by surface flinger only when crop information
 * is present in the frame.
 * 3. When there is no crop information, there will be no call to MDP zoom,
 * and the buffers in SrcSet will be passed to surface flinger to display.
 * 4. With crop information present, MDP zoom will be called, and the final
 * data will be placed in a buffer from DstSet, and this buffer will be given
 * to surface flinger to display.
 */
#define NUM_MORE_BUFS 2

QualcommCameraHardware::QualcommCameraHardware()
    : mParameters(),
      mCameraRunning(false),
      mPreviewInitialized(false),
      mFrameThreadRunning(false),
      mVideoThreadRunning(false),
      mSnapshotThreadRunning(false),
      mInSnapshotMode(false),
      mJpegThreadRunning(false),
      mSnapshotFormat(0),
      mReleasedRecordingFrame(false),
      mPreviewFrameSize(0),
      mRawSize(0),
      mCameraControlFd(-1),
      mAutoFocusThreadRunning(false),
      mAutoFocusFd(-1),
      mBrightness(0),
      mHJR(0),
      mInPreviewCallback(false),
      mUseOverlay(0),
      mOverlay(0),
      mMsgEnabled(0),
      mNotifyCallback(0),
      mDataCallback(0),
      mDataCallbackTimestamp(0),
      mCallbackCookie(0),
      mInitialized(false),
      mDebugFps(0)
{

    // Start opening camera device in a separate thread/ Since this
    // initializes the sensor hardware, this can take a long time. So,
    // start the process here so it will be ready by the time it's
    // needed.
    if ((pthread_create(&w_thread, NULL, opencamerafd, NULL)) != 0) {
        LOGE("Camera open thread creation failed");
    }

    memset(&mDimension, 0, sizeof(mDimension));
    memset(&mCrop, 0, sizeof(mCrop));
    memset(&zoomCropInfo, 0, sizeof(zoom_crop_info));
    char value[PROPERTY_VALUE_MAX];
    property_get("persist.debug.sf.showfps", value, "0");
    mDebugFps = atoi(value);
    kPreviewBufferCountActual = kPreviewBufferCount + NUM_MORE_BUFS;

  jpegPadding = 8;
    LOGV("constructor EX");
}


void QualcommCameraHardware::filterPreviewSizes(){

    unsigned int boardMask = 0;
    int prop = 0;
    for(prop=0;prop<sizeof(boardProperties)/sizeof(board_property);prop++){
        if(mCurrentTarget == boardProperties[prop].target){
            boardMask = boardProperties[prop].previewSizeMask;
            break;
        }
    }

    if (!strcmp(mSensorInfo.name, "ov5642"))
        boardMask = 0xff;

    int bitMask = boardMask & sensorType->bitMask;
    if(bitMask){
        unsigned int mask = 1<<(PREVIEW_SIZE_COUNT-1);
        previewSizeCount=0;
        unsigned int i = 0;
        while(mask){
            if(mask&bitMask)
                supportedPreviewSizes[previewSizeCount++] =
                        preview_sizes[i];
            i++;
            mask = mask >> 1;
        }
    }
}

//filter Picture sizes based on max width and height
void QualcommCameraHardware::filterPictureSizes(){
    int i;
    for(i=0;i<PICTURE_SIZE_COUNT;i++){
        if(((picture_sizes[i].width <=
                sensorType->max_supported_snapshot_width) &&
           (picture_sizes[i].height <=
                   sensorType->max_supported_snapshot_height))){
            picture_sizes_ptr = picture_sizes + i;
            supportedPictureSizesCount = PICTURE_SIZE_COUNT - i  ;
            return ;
        }
    }
}

void QualcommCameraHardware::initDefaultParameters()
{
    LOGV("initDefaultParameters E");

    // Initialize constant parameter strings. This will happen only once in the
    // lifetime of the mediaserver process.
    if (!parameter_string_initialized) {
        findSensorType();

        //filter preview sizes
        filterPreviewSizes();
        preview_size_values = create_sizes_str(
            supportedPreviewSizes, previewSizeCount);
        //filter picture sizes
        filterPictureSizes();
        picture_size_values = create_sizes_str(
                picture_sizes_ptr, supportedPictureSizesCount);

        if(sensorType->hasAutoFocusSupport){
            focus_mode_values = create_values_str(
                    focus_modes, sizeof(focus_modes) / sizeof(str_map));
        }
        picture_format_values = create_values_str(
            picture_formats, sizeof(picture_formats)/sizeof(str_map));
        preview_frame_rate_values = create_values_range_str(
            MINIMUM_FPS, MAXIMUM_FPS);
        parameter_string_initialized = true;
    }

    mParameters.setPreviewSize(DEFAULT_PREVIEW_WIDTH, DEFAULT_PREVIEW_HEIGHT);
    mDimension.display_width = DEFAULT_PREVIEW_WIDTH;
    mDimension.display_height = DEFAULT_PREVIEW_HEIGHT;

    mParameters.setPreviewFrameRate(DEFAULT_FPS);
        mParameters.set(
            CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATES,
            preview_frame_rate_values.string());

    mParameters.setPictureSize(DEFAULT_PICTURE_WIDTH, DEFAULT_PICTURE_HEIGHT);
    mParameters.setPictureFormat("jpeg"); // informative

    mParameters.set(CameraParameters::KEY_JPEG_QUALITY, "100"); // max quality
    mParameters.set(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH,
                    THUMBNAIL_WIDTH_STR); // informative
    mParameters.set(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT,
                    THUMBNAIL_HEIGHT_STR); // informative
    mDimension.ui_thumbnail_width =
            thumbnail_sizes[DEFAULT_THUMBNAIL_SETTING].width;
    mDimension.ui_thumbnail_height =
            thumbnail_sizes[DEFAULT_THUMBNAIL_SETTING].height;
    mParameters.set(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY, "90");

    mParameters.set(CameraParameters::KEY_FOCUS_MODE,
                    CameraParameters::FOCUS_MODE_AUTO);

    mParameters.set(CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES,
                    preview_size_values.string());
    mParameters.set(CameraParameters::KEY_SUPPORTED_PICTURE_SIZES,
                    picture_size_values.string());
    mParameters.set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES,
                    focus_mode_values);
    mParameters.set(CameraParameters::KEY_SUPPORTED_PICTURE_FORMATS,
                    picture_format_values);


    if (setParameters(mParameters) != NO_ERROR) {
        LOGE("Failed to set default parameters?!");
    }

    mUseOverlay = useOverlay();

    /* Initialize the camframe_timeout_flag*/
    Mutex::Autolock l(&mCamframeTimeoutLock);
    camframe_timeout_flag = FALSE;
    mPostViewHeap = NULL;

    mInitialized = true;

    LOGV("initDefaultParameters X");
}

void QualcommCameraHardware::findSensorType(){
    mDimension.picture_width = DEFAULT_PICTURE_WIDTH;
    mDimension.picture_height = DEFAULT_PICTURE_HEIGHT;
    bool ret = native_set_parm(CAMERA_SET_PARM_DIMENSION,
                    sizeof(cam_ctrl_dimension_t), &mDimension);

    //default to 5 mp
    sensorType = sensorTypes;
    return;
}

#define ROUND_TO_PAGE(x)  (((x)+0xfff)&~0xfff)

bool QualcommCameraHardware::startCamera()
{
    LOGV("startCamera E");
    if( mCurrentTarget == TARGET_MAX ) {
        LOGE(" Unable to determine the target type. Camera will not work ");
        return false;
    }

    libmmcamera = ::dlopen("liboemcamera.so", RTLD_NOW);
    LOGV("loading liboemcamera at %p", libmmcamera);
    if (!libmmcamera) {
        LOGE("FATAL ERROR: could not dlopen liboemcamera.so: %s", dlerror());
        return false;
    }

    *(void **)&LINK_cam_frame =
        ::dlsym(libmmcamera, "cam_frame");
    *(void **)&LINK_camframe_terminate =
        ::dlsym(libmmcamera, "camframe_terminate");

    *(void **)&LINK_jpeg_encoder_init =
        ::dlsym(libmmcamera, "jpeg_encoder_init");

    *(void **)&LINK_jpeg_encoder_encode =
        ::dlsym(libmmcamera, "jpeg_encoder_encode");

    *(void **)&LINK_jpeg_encoder_join =
        ::dlsym(libmmcamera, "jpeg_encoder_join");

    *(void **)&LINK_mmcamera_camframe_callback =
        ::dlsym(libmmcamera, "mmcamera_camframe_callback");

    *LINK_mmcamera_camframe_callback = receive_camframe_callback;

    *(void **)&LINK_mmcamera_jpegfragment_callback =
        ::dlsym(libmmcamera, "mmcamera_jpegfragment_callback");

    *LINK_mmcamera_jpegfragment_callback = receive_jpeg_fragment_callback;

    *(void **)&LINK_mmcamera_jpeg_callback =
        ::dlsym(libmmcamera, "mmcamera_jpeg_callback");

    *LINK_mmcamera_jpeg_callback = receive_jpeg_callback;

    *(void **)&LINK_camframe_timeout_callback =
        ::dlsym(libmmcamera, "camframe_timeout_callback");

    *LINK_camframe_timeout_callback = receive_camframetimeout_callback;

    *(void **)&LINK_mmcamera_shutter_callback =
        ::dlsym(libmmcamera, "mmcamera_shutter_callback");

    *LINK_mmcamera_shutter_callback = receive_shutter_callback;

    *(void**)&LINK_jpeg_encoder_setMainImageQuality =
        ::dlsym(libmmcamera, "jpeg_encoder_setMainImageQuality");

    *(void**)&LINK_jpeg_encoder_setThumbnailQuality =
        ::dlsym(libmmcamera, "jpeg_encoder_setThumbnailQuality");

    *(void **)&LINK_cam_conf =
        ::dlsym(libmmcamera, "cam_conf");

    *(void **)&LINK_launch_cam_conf_thread =
        ::dlsym(libmmcamera, "launch_cam_conf_thread");

    *(void **)&LINK_release_cam_conf_thread =
        ::dlsym(libmmcamera, "release_cam_conf_thread");

    /* The control thread is in libcamera itself. */
    if (pthread_join(w_thread, NULL) != 0) {
        LOGE("Camera open thread exit failed");
        return false;
    }
    mCameraControlFd = camerafd;

    if (mCameraControlFd < 0) {
        LOGE("startCamera X: %s open failed: %s!",
             MSM_CAMERA_CONTROL,
             strerror(errno));
        return false;
    }

        fb_fd = open("/dev/graphics/fb0", O_RDWR);
        if (fb_fd < 0) {
            LOGE("startCamera: fb0 open failed: %s!", strerror(errno));
            return FALSE;
        }

    /* This will block until the control thread is launched. After that, sensor
     * information becomes available.
     */

    if (LINK_launch_cam_conf_thread()) {
        LOGE("failed to launch the camera config thread");
        return false;
    }

    memset(&mSensorInfo, 0, sizeof(mSensorInfo));
    if (ioctl(mCameraControlFd,
              MSM_CAM_IOCTL_GET_SENSOR_INFO,
              &mSensorInfo) < 0)
        LOGW("%s: cannot retrieve sensor info!", __FUNCTION__);
    else
        LOGI("%s: camsensor name %s, flash %d", __FUNCTION__,
             mSensorInfo.name, mSensorInfo.flash_enabled);
    LOGV("startCamera X");
    return true;
}

status_t QualcommCameraHardware::dump(int fd,
                                      const Vector<String16>& args) const
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;

    // Dump internal primitives.
    result.append("QualcommCameraHardware::dump");
    snprintf(buffer, 255, "mMsgEnabled (%d)\n", mMsgEnabled);
    result.append(buffer);
    int width, height;
    mParameters.getPreviewSize(&width, &height);
    snprintf(buffer, 255, "preview width(%d) x height (%d)\n", width, height);
    result.append(buffer);
    mParameters.getPictureSize(&width, &height);
    snprintf(buffer, 255, "raw width(%d) x height (%d)\n", width, height);
    result.append(buffer);
    snprintf(buffer, 255,
             "preview frame size(%d), raw size (%d), jpeg size (%d) "
             "and jpeg max size (%d)\n", mPreviewFrameSize, mRawSize,
             mJpegSize, mJpegMaxSize);
    result.append(buffer);
    write(fd, result.string(), result.size());

    // Dump internal objects.
    if (mPreviewHeap != 0) {
        mPreviewHeap->dump(fd, args);
    }
    if (mRawHeap != 0) {
        mRawHeap->dump(fd, args);
    }
    if (mJpegHeap != 0) {
        mJpegHeap->dump(fd, args);
    }
    mParameters.dump(fd, args);
    return NO_ERROR;
}

static bool native_set_afmode(int camfd, isp3a_af_mode_t af_type)
{
    int rc;
    struct msm_ctrl_cmd ctrlCmd;

    ctrlCmd.timeout_ms = 5000;
    ctrlCmd.type = CAMERA_SET_PARM_AUTO_FOCUS;
    ctrlCmd.length = sizeof(af_type);
    ctrlCmd.value = &af_type;
    ctrlCmd.resp_fd = camfd; // FIXME: this will be put in by the kernel

    if ((rc = ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd)) < 0)
        LOGE("native_set_afmode: ioctl fd %d error %s\n",
             camfd,
             strerror(errno));

    LOGV("native_set_afmode: ctrlCmd.status == %d\n", ctrlCmd.status);
    return rc >= 0 && ctrlCmd.status == CAMERA_EXIT_CB_DONE;
}

static bool native_cancel_afmode(int camfd, int af_fd)
{
    int rc;
    struct msm_ctrl_cmd ctrlCmd;

    ctrlCmd.timeout_ms = 0;
    ctrlCmd.type = CAMERA_AUTO_FOCUS_CANCEL;
    ctrlCmd.length = 0;
    ctrlCmd.value = NULL;
    ctrlCmd.resp_fd = -1; // there's no response fd

    if ((rc = ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND_2, &ctrlCmd)) < 0)
    {
        LOGE("native_cancel_afmode: ioctl fd %d error %s\n",
             camfd,
             strerror(errno));
        return false;
    }

    return true;
}

static bool native_start_preview(int camfd)
{
    struct msm_ctrl_cmd ctrlCmd;

    ctrlCmd.timeout_ms = 5000;
    ctrlCmd.type       = CAMERA_START_PREVIEW;
    ctrlCmd.length     = 0;
    ctrlCmd.resp_fd    = camfd; // FIXME: this will be put in by the kernel

    if (ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd) < 0) {
        LOGE("native_start_preview: MSM_CAM_IOCTL_CTRL_COMMAND fd %d error %s",
             camfd,
             strerror(errno));
        return false;
    }

    return true;
}

static bool native_get_picture (int camfd, common_crop_t *crop)
{
    struct msm_ctrl_cmd ctrlCmd;

    ctrlCmd.timeout_ms = 5000;
    ctrlCmd.length     = sizeof(common_crop_t);
    ctrlCmd.value      = crop;

    if(ioctl(camfd, MSM_CAM_IOCTL_GET_PICTURE, &ctrlCmd) < 0) {
        LOGE("native_get_picture: MSM_CAM_IOCTL_GET_PICTURE fd %d error %s",
             camfd,
             strerror(errno));
        return false;
    }

    LOGI("crop: in1_w %d", crop->in1_w);
    LOGI("crop: in1_h %d", crop->in1_h);
    LOGI("crop: out1_w %d", crop->out1_w);
    LOGI("crop: out1_h %d", crop->out1_h);

    LOGI("crop: in2_w %d", crop->in2_w);
    LOGI("crop: in2_h %d", crop->in2_h);
    LOGI("crop: out2_w %d", crop->out2_w);
    LOGI("crop: out2_h %d", crop->out2_h);

    LOGI("crop: update %d", crop->update_flag);

    return true;
}

static bool native_stop_preview(int camfd)
{
    struct msm_ctrl_cmd ctrlCmd;
    ctrlCmd.timeout_ms = 5000;
    ctrlCmd.type       = CAMERA_STOP_PREVIEW;
    ctrlCmd.length     = 0;
    ctrlCmd.resp_fd    = camfd; // FIXME: this will be put in by the kernel

    if(ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd) < 0) {
        LOGE("native_stop_preview: ioctl fd %d error %s",
             camfd,
             strerror(errno));
        return false;
    }

    return true;
}

static bool native_prepare_snapshot(int camfd)
{
    int ioctlRetVal = true;
    struct msm_ctrl_cmd ctrlCmd;

    ctrlCmd.timeout_ms = 1000;
    ctrlCmd.type       = CAMERA_PREPARE_SNAPSHOT;
    ctrlCmd.length     = 0;
    ctrlCmd.value      = NULL;
    ctrlCmd.resp_fd = camfd;

    if (ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd) < 0) {
        LOGE("native_prepare_snapshot: ioctl fd %d error %s",
             camfd,
             strerror(errno));
        return false;
    }
    return true;
}

static bool native_start_snapshot(int camfd)
{
    struct msm_ctrl_cmd ctrlCmd;

    ctrlCmd.timeout_ms = 5000;
    ctrlCmd.type       = CAMERA_START_SNAPSHOT;
    ctrlCmd.length     = 0;
    ctrlCmd.resp_fd    = camfd; // FIXME: this will be put in by the kernel

    if(ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd) < 0) {
        LOGE("native_start_snapshot: ioctl fd %d error %s",
             camfd,
             strerror(errno));
        return false;
    }

    return true;
}

static bool native_stop_snapshot (int camfd)
{
    struct msm_ctrl_cmd ctrlCmd;

    ctrlCmd.timeout_ms = 0;
    ctrlCmd.type       = CAMERA_STOP_SNAPSHOT;
    ctrlCmd.length     = 0;
    ctrlCmd.resp_fd    = -1;

    if (ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND_2, &ctrlCmd) < 0) {
        LOGE("native_stop_snapshot: ioctl fd %d error %s",
             camfd,
             strerror(errno));
        return false;
    }

    return true;
}


static cam_frame_start_parms frame_parms;
static int recordingState = 0;

static rat_t latitude[3];
static rat_t longitude[3];
static char lonref[2];
static char latref[2];
static char dateTime[20];
static rat_t altitude;

static void addExifTag(exif_tag_id_t tagid, exif_tag_type_t type,
                        uint32_t count, uint8_t copy, void *data) {

    if(exif_table_numEntries == MAX_EXIF_TABLE_ENTRIES) {
        LOGE("Number of entries exceeded limit");
        return;
    }

    int index = exif_table_numEntries;
    exif_data[index].tag_id = tagid;
	exif_data[index].tag_entry.type = type;
	exif_data[index].tag_entry.count = count;
	exif_data[index].tag_entry.copy = copy;
    if((type == EXIF_RATIONAL) && (count > 1))
        exif_data[index].tag_entry.data._rats = (rat_t *)data;
    if((type == EXIF_RATIONAL) && (count == 1))
		exif_data[index].tag_entry.data._rat = *(rat_t *)data;
    else if(type == EXIF_ASCII)
        exif_data[index].tag_entry.data._ascii = (char *)data;
    else if(type == EXIF_BYTE)
		exif_data[index].tag_entry.data._byte = *(uint8_t *)data;

    // Increase number of entries
    exif_table_numEntries++;
    return;
}

bool QualcommCameraHardware::native_jpeg_encode(void)
{
    int jpeg_quality = mParameters.getInt("jpeg-quality");
    if (jpeg_quality >= 0) {
        LOGV("native_jpeg_encode, current jpeg main img quality =%d",
             jpeg_quality);
        if(!LINK_jpeg_encoder_setMainImageQuality(jpeg_quality)) {
            LOGE("native_jpeg_encode set jpeg-quality failed");
            return false;
        }
    }

    int thumbnail_quality = mParameters.getInt("jpeg-thumbnail-quality");
    if (thumbnail_quality >= 0) {
        LOGV("native_jpeg_encode, current jpeg thumbnail quality =%d",
             thumbnail_quality);
        if(!LINK_jpeg_encoder_setThumbnailQuality(thumbnail_quality)) {
            LOGE("native_jpeg_encode set thumbnail-quality failed");
            return false;
        }
    }

    //set TimeStamp
	time_t now;
	struct tm * curtime;

	time(&now);
    curtime = localtime(&now);
    strftime(dateTime,20,"%Y:%m:%d %H:%M:%S",curtime);
    dateTime[19] = '\0';
    addExifTag(EXIFTAGID_EXIF_DATE_TIME_ORIGINAL, EXIF_ASCII,
                  20, 1, (void *)dateTime);
    addExifTag(EXIFTAGID_EXIF_DATE_TIME, EXIF_ASCII,
                  20, 1, (void *)dateTime);

   /* Set maker and model. Read the NOTICE before changing this */
   char model[PROP_VALUE_MAX];
   char maker[12];
   int modelLen = 0;

   strncpy(maker,"CyanogenMod",11);
   maker[11] = '\0';
   __system_property_get("ro.product.device", model);
   modelLen=strlen(model);
   model[modelLen] = '\0';

    addExifTag(EXIFTAGID_EXIF_CAMERA_MAKER, EXIF_ASCII,
                  12, 1, (void *)maker);
    addExifTag(EXIFTAGID_EXIF_CAMERA_MODEL, EXIF_ASCII,
                  modelLen, 1, (void *)model);

    if (!LINK_jpeg_encoder_encode(&mDimension,
                                  (uint8_t *)mThumbnailHeap->mHeap->base(),
                                  mThumbnailHeap->mHeap->getHeapID(),
                                  (uint8_t *)mRawHeap->mHeap->base(),
                                  mRawHeap->mHeap->getHeapID(),
                                  &mCrop, exif_data, exif_table_numEntries)) {
        LOGE("native_jpeg_encode: jpeg_encoder_encode failed.");
        return false;
    }
    return true;
}

bool QualcommCameraHardware::native_set_parm(
    cam_ctrl_type type, uint16_t length, void *value)
{
    struct msm_ctrl_cmd ctrlCmd;

    ctrlCmd.timeout_ms = 5000;
    ctrlCmd.type       = (uint16_t)type;
    ctrlCmd.length     = length;
    // FIXME: this will be put in by the kernel
    ctrlCmd.resp_fd    = mCameraControlFd;
    ctrlCmd.value = value;

    LOGV("%s: fd %d, type %d, length %d", __FUNCTION__,
         mCameraControlFd, type, length);
    if (ioctl(mCameraControlFd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd) < 0 ||
                ctrlCmd.status != CAM_CTRL_SUCCESS) {
        LOGE("%s: error (%s): fd %d, type %d, length %d, status %d",
             __FUNCTION__, strerror(errno),
             mCameraControlFd, type, length, ctrlCmd.status);
        return false;
    }
    return true;
}

void QualcommCameraHardware::runFrameThread(void *data)
{
    LOGV("runFrameThread E");

    int cnt;

#if DLOPEN_LIBMMCAMERA
    // We need to maintain a reference to libqcamera.so for the duration of the
    // frame thread, because we do not know when it will exit relative to the
    // lifetime of this object.  We do not want to dlclose() libqcamera while
    // LINK_cam_frame is still running.
    void *libhandle = ::dlopen("liboemcamera.so", RTLD_NOW);
    LOGV("FRAME: loading libqcamera at %p", libhandle);
    if (!libhandle) {
        LOGE("FATAL ERROR: could not dlopen liboemcamera.so: %s", dlerror());
    }
    if (libhandle)
#endif
    {
        LINK_cam_frame(data);
    }

    mPreviewHeap.clear();
        mRecordHeap.clear();

#if DLOPEN_LIBMMCAMERA
    if (libhandle) {
        ::dlclose(libhandle);
        LOGV("FRAME: dlclose(libqcamera)");
    }
#endif

    mFrameThreadWaitLock.lock();
    mFrameThreadRunning = false;
    mFrameThreadWait.signal();
    mFrameThreadWaitLock.unlock();

    LOGV("runFrameThread X");
}



void *video_thread(void *user)
{
    LOGV("video_thread E");
    sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();

    LOGV("video_thread X");
    return NULL;
}

void *frame_thread(void *user)
{
    LOGD("frame_thread E");
    sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
    if (obj != 0) {
        obj->runFrameThread(user);
    }
    else LOGW("not starting frame thread: the object went away!");
    LOGD("frame_thread X");
    return NULL;
}

bool QualcommCameraHardware::initPreview()
{
    // See comments in deinitPreview() for why we have to wait for the frame
    // thread here, and why we can't use pthread_join().
    int videoWidth, videoHeight;
    mParameters.getPreviewSize(&previewWidth, &previewHeight);

    videoWidth = previewWidth;  // temporary , should be configurable later
    videoHeight = previewHeight;
    LOGV("initPreview E: preview size=%dx%d videosize = %d x %d", previewWidth, previewHeight, videoWidth, videoHeight );

    mFrameThreadWaitLock.lock();
    while (mFrameThreadRunning) {
        LOGV("initPreview: waiting for old frame thread to complete.");
        mFrameThreadWait.wait(mFrameThreadWaitLock);
        LOGV("initPreview: old frame thread completed.");
    }
    mFrameThreadWaitLock.unlock();

    mInSnapshotModeWaitLock.lock();
    while (mInSnapshotMode) {
        LOGV("initPreview: waiting for snapshot mode to complete.");
        mInSnapshotModeWait.wait(mInSnapshotModeWaitLock);
        LOGV("initPreview: snapshot mode completed.");
    }
    mInSnapshotModeWaitLock.unlock();

    int cnt = 0;
    mPreviewFrameSize = previewWidth * previewHeight * 3/2;
    dstOffset = 0;
    mPreviewHeap = new PmemPool("/dev/pmem_adsp",
                                MemoryHeapBase::READ_ONLY,
                                mCameraControlFd,
                                MSM_PMEM_PREVIEW, //MSM_PMEM_OUTPUT2,
                                mPreviewFrameSize,
                                kPreviewBufferCountActual,
                                mPreviewFrameSize,
                                "preview");

    if (!mPreviewHeap->initialized()) {
        mPreviewHeap.clear();
        LOGE("initPreview X: could not initialize Camera preview heap.");
        return false;
    }

    // mDimension will be filled with thumbnail_width, thumbnail_height,
    // orig_picture_dx, and orig_picture_dy after this function call. We need to
    // keep it for jpeg_encoder_encode.
    bool ret = native_set_parm(CAMERA_SET_PARM_DIMENSION,
                               sizeof(cam_ctrl_dimension_t), &mDimension);

    if (ret) {
        for (cnt = 0; cnt < kPreviewBufferCount; cnt++) {
            frames[cnt].fd = mPreviewHeap->mHeap->getHeapID();
            frames[cnt].buffer =
                (uint32_t)mPreviewHeap->mHeap->base() + mPreviewHeap->mAlignedBufferSize * cnt;
            frames[cnt].y_off = 0;
            frames[cnt].cbcr_off = previewWidth * previewHeight;
            frames[cnt].path = OUTPUT_TYPE_P; // MSM_FRAME_ENC;
        }

        mFrameThreadWaitLock.lock();
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

        frame_parms.frame = frames[kPreviewBufferCount - 1];

            frame_parms.video_frame =  frames[kPreviewBufferCount - 1];

        LOGV ("initpreview before cam_frame thread carete , video frame  buffer=%lu fd=%d y_off=%d cbcr_off=%d \n",
          (unsigned long)frame_parms.video_frame.buffer, frame_parms.video_frame.fd, frame_parms.video_frame.y_off,
          frame_parms.video_frame.cbcr_off);
        mFrameThreadRunning = !pthread_create(&mFrameThread,
                                              &attr,
                                              frame_thread,
                                              (void*)&(frame_parms));
        ret = mFrameThreadRunning;
        mFrameThreadWaitLock.unlock();
    }

    LOGV("initPreview X: %d", ret);
    return ret;
}

void QualcommCameraHardware::deinitPreview(void)
{
    LOGI("deinitPreview E");

    // When we call deinitPreview(), we signal to the frame thread that it
    // needs to exit, but we DO NOT WAIT for it to complete here.  The problem
    // is that deinitPreview is sometimes called from the frame-thread's
    // callback, when the refcount on the Camera client reaches zero.  If we
    // called pthread_join(), we would deadlock.  So, we just call
    // LINK_camframe_terminate() in deinitPreview(), which makes sure that
    // after the preview callback returns, the camframe thread will exit.  We
    // could call pthread_join() in initPreview() to join the last frame
    // thread.  However, we would also have to call pthread_join() in release
    // as well, shortly before we destroy the object; this would cause the same
    // deadlock, since release(), like deinitPreview(), may also be called from
    // the frame-thread's callback.  This we have to make the frame thread
    // detached, and use a separate mechanism to wait for it to complete.

    LINK_camframe_terminate();
    LOGI("deinitPreview X");
}



bool QualcommCameraHardware::initRaw(bool initJpegHeap)
{
    int rawWidth, rawHeight;

    mParameters.getPictureSize(&rawWidth, &rawHeight);
    LOGE("initRaw E: picture size=%dx%d", rawWidth, rawHeight);

    int thumbnailBufferSize;
    //Thumbnail height should be smaller than Picture height
    if (rawHeight > (int)thumbnail_sizes[DEFAULT_THUMBNAIL_SETTING].height){
        mDimension.ui_thumbnail_width = thumbnail_sizes[DEFAULT_THUMBNAIL_SETTING].width;
        mDimension.ui_thumbnail_height = thumbnail_sizes[DEFAULT_THUMBNAIL_SETTING].height;

        uint32_t pictureAspectRatio = (uint32_t)((rawWidth * Q12) / rawHeight);
        uint32_t i;
    LOGE("initRaw E: aspect ratio=%d", pictureAspectRatio);

        for(i = 0; i < THUMBNAIL_SIZE_COUNT; i++ )
        {
            if(thumbnail_sizes[i].aspect_ratio == pictureAspectRatio)
            {
                mDimension.ui_thumbnail_width = thumbnail_sizes[i].width;
                mDimension.ui_thumbnail_height = thumbnail_sizes[i].height;
                break;
            }
        }
    }
    else{
        mDimension.ui_thumbnail_height = THUMBNAIL_SMALL_HEIGHT;
        mDimension.ui_thumbnail_width = (THUMBNAIL_SMALL_HEIGHT * rawWidth)/ rawHeight;
    }

    LOGE("Thumbnail Size Width %d Height %d", mDimension.ui_thumbnail_width, mDimension.ui_thumbnail_height);

    thumbnailBufferSize = mDimension.ui_thumbnail_width * mDimension.ui_thumbnail_height * 3 / 2;

    // mDimension will be filled with thumbnail_width, thumbnail_height,
    // orig_picture_dx, and orig_picture_dy after this function call. We need to
    // keep it for jpeg_encoder_encode.
    bool ret = native_set_parm(CAMERA_SET_PARM_DIMENSION, sizeof(cam_ctrl_dimension_t), &mDimension);

    LOGE("NEW UI Thumbnail Size Width %d Height %d", mDimension.ui_thumbnail_width, mDimension.ui_thumbnail_height);
    LOGE("NEW Thumbnail Size Width %d Height %d", mDimension.thumbnail_width, mDimension.thumbnail_height);
    LOGE("NEW ORIG_PICTURE_DX Width %d Height %d", mDimension.orig_picture_dx, mDimension.orig_picture_dy);

    if(!ret) {
        LOGE("initRaw X: failed to set dimension");
        return false;
    }

    if (mJpegHeap != NULL) {
        LOGV("initRaw: clearing old mJpegHeap.");
        mJpegHeap.clear();
    }

    // Snapshot
    mRawSize = rawWidth * rawHeight * 3 / 2;
    mJpegMaxSize = rawWidth * rawHeight * 3 / 2;

    LOGV("initRaw: initializing mRawHeap. mRawSize:%d , mJpegMaxSize:%d",mRawSize,mJpegMaxSize);
    mRawHeap =
        new PmemPool("/dev/pmem",
                     MemoryHeapBase::READ_ONLY,
                     mCameraControlFd,
                     MSM_PMEM_MAINIMG,
                     mJpegMaxSize,
                     kRawBufferCount,
                     mRawSize,
                     "snapshot camera");

    if (!mRawHeap->initialized()) {
	LOGE("initRaw X failed ");
	mRawHeap.clear();
	LOGE("initRaw X: error initializing mRawHeap");
	return false;
    }

    LOGE("do_mmap snapshot pbuf = %p, pmem_fd = %d", (uint8_t *)mRawHeap->mHeap->base(), mRawHeap->mHeap->getHeapID());

    // Jpeg

    if (initJpegHeap) {
        LOGV("initRaw: initializing mJpegHeap.");
        mJpegHeap =  new AshmemPool(mJpegMaxSize,
                           kJpegBufferCount,
                           0, // we do not know how big the picture will be
                           "jpeg");

        if (!mJpegHeap->initialized()) {
            mJpegHeap.clear();
            mRawHeap.clear();
            LOGE("initRaw X failed: error initializing mJpegHeap.");
            return false;
        }

        // Thumbnails

        mThumbnailHeap =
            new PmemPool("/dev/pmem_adsp",
                         MemoryHeapBase::READ_ONLY,
                         mCameraControlFd,
                         MSM_PMEM_THUMBNAIL,
                         thumbnailBufferSize,
                         1,
                         thumbnailBufferSize,
                         "thumbnail");

        if (!mThumbnailHeap->initialized()) {
            mThumbnailHeap.clear();
            mJpegHeap.clear();
            mRawHeap.clear();
            LOGE("initRaw X failed: error initializing mThumbnailHeap.");
            return false;
        }
    }

    LOGV("initRaw X");
    return true;
}

void QualcommCameraHardware::deinitRaw()
{
    LOGV("deinitRaw E");

    mThumbnailHeap.clear();
    mJpegHeap.clear();
    mRawHeap.clear();
    mDisplayHeap.clear();

    LOGV("deinitRaw X");
}

void QualcommCameraHardware::release()
{
    LOGD("release E");
    Mutex::Autolock l(&mLock);

#if DLOPEN_LIBMMCAMERA
    if (libmmcamera == NULL) {
        LOGE("ERROR: multiple release!");
        return;
    }
#else
#warning "Cannot detect multiple release when not dlopen()ing libqcamera!"
#endif

    int cnt, rc;
    struct msm_ctrl_cmd ctrlCmd;
    if (mCameraRunning) {
        if(mDataCallbackTimestamp && (mMsgEnabled & CAMERA_MSG_VIDEO_FRAME)) {
            mRecordFrameLock.lock();
            mReleasedRecordingFrame = true;
            mRecordWait.signal();
            mRecordFrameLock.unlock();
        }
        stopPreviewInternal();
    }

    LINK_jpeg_encoder_join();
    {
        deinitRaw();
    }
    //Signal the snapshot thread
    mJpegThreadWaitLock.lock();
    mJpegThreadRunning = false;
    mJpegThreadWait.signal();
    mJpegThreadWaitLock.unlock();


    ctrlCmd.timeout_ms = 5000;
    ctrlCmd.length = 0;
    ctrlCmd.type = (uint16_t)CAMERA_EXIT;
    ctrlCmd.resp_fd = mCameraControlFd; // FIXME: this will be put in by the kernel
    if (ioctl(mCameraControlFd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd) < 0)
          LOGE("ioctl CAMERA_EXIT fd %d error %s",
              mCameraControlFd, strerror(errno));

    LINK_release_cam_conf_thread();
    close(mCameraControlFd);
    mCameraControlFd = -1;
    if(fb_fd >= 0) {
        close(fb_fd);
        fb_fd = -1;
    }
#if DLOPEN_LIBMMCAMERA
    if (libmmcamera) {
        ::dlclose(libmmcamera);
        LOGV("dlclose(liboemcamera)");
        libmmcamera = NULL;
    }
#endif

    singleton_lock.lock();
    singleton_releasing = true;
    singleton_releasing_start_time = systemTime();
    singleton_lock.unlock();

    LOGD("release X");
}

QualcommCameraHardware::~QualcommCameraHardware()
{
    LOGD("~QualcommCameraHardware E");
    singleton_lock.lock();

    singleton.clear();
    singleton_releasing = false;
    singleton_releasing_start_time = 0;
    singleton_wait.signal();
    singleton_lock.unlock();
    LOGD("~QualcommCameraHardware X");
}

sp<IMemoryHeap> QualcommCameraHardware::getRawHeap() const
{
    LOGV("getRawHeap");
    return mDisplayHeap != NULL ? mDisplayHeap->mHeap : NULL;
}

sp<IMemoryHeap> QualcommCameraHardware::getPreviewHeap() const
{
    LOGV("getPreviewHeap");
    return mPreviewHeap != NULL ? mPreviewHeap->mHeap : NULL;
}

status_t QualcommCameraHardware::startPreviewInternal()
{
    LOGV("in startPreviewInternal : E");
    if(mCameraRunning) {
        LOGV("startPreview X: preview already running.");
        return NO_ERROR;
    }

    if (!mPreviewInitialized) {
        mLastQueuedFrame = NULL;
        mPreviewInitialized = initPreview();
        if (!mPreviewInitialized) {
            LOGE("startPreview X initPreview failed.  Not starting preview.");
            return UNKNOWN_ERROR;
        }
    }

    {
        Mutex::Autolock cameraRunningLock(&mCameraRunningLock);
            mCameraRunning = native_start_preview(mCameraControlFd);
    }

    if(!mCameraRunning) {
        deinitPreview();
        mPreviewInitialized = false;
        mOverlay = NULL;
        LOGE("startPreview X: native_start_preview failed!");
        return UNKNOWN_ERROR;
    }

    //Reset the Gps Information
    exif_table_numEntries = 0;


        mParameters.set("zoom-supported", "false");
        mMaxZoom = 0;
    mParameters.set("max-zoom",mMaxZoom);

    LOGV("startPreviewInternal X");
    return NO_ERROR;
}

status_t QualcommCameraHardware::startPreview()
{
    LOGV("startPreview E");
    Mutex::Autolock l(&mLock);
    return startPreviewInternal();
}

void QualcommCameraHardware::stopPreviewInternal()
{
    LOGV("stopPreviewInternal E: %d", mCameraRunning);
    if (mCameraRunning) {
        // Cancel auto focus.
        {
            if (mNotifyCallback && (mMsgEnabled & CAMERA_MSG_FOCUS)) {
                cancelAutoFocusInternal();
            }
        }

        Mutex::Autolock l(&mCamframeTimeoutLock);
        {
            Mutex::Autolock cameraRunningLock(&mCameraRunningLock);
            if(!camframe_timeout_flag) {
                    mCameraRunning = !native_stop_preview(mCameraControlFd);
            } else {
                /* This means that the camframetimeout was issued.
                 * But we did not issue native_stop_preview(), so we
                 * need to update mCameraRunning to indicate that
                 * Camera is no longer running. */
                mCameraRunning = 0;
            }
        }

	if (!mCameraRunning && mPreviewInitialized) {
	    deinitPreview();
	    mPreviewInitialized = false;
	}
	else LOGE("stopPreviewInternal: failed to stop preview");
    }
    LOGV("stopPreviewInternal X: %d", mCameraRunning);
}

void QualcommCameraHardware::stopPreview()
{
    LOGV("stopPreview: E");
    Mutex::Autolock l(&mLock);
    {
        if (mDataCallbackTimestamp && (mMsgEnabled & CAMERA_MSG_VIDEO_FRAME))
            return;
    }
    stopPreviewInternal();
    LOGV("stopPreview: X");
}

void QualcommCameraHardware::runAutoFocus()
{
    bool status = true;
    void *libhandle = NULL;
    isp3a_af_mode_t afMode;

    mAutoFocusThreadLock.lock();

    if(!sensorType->hasAutoFocusSupport){
        bool status = false;
        mCallbackLock.lock();
        bool autoFocusEnabled = mNotifyCallback && (mMsgEnabled & CAMERA_MSG_FOCUS);
        notify_callback cb = mNotifyCallback;
        void *data = mCallbackCookie;
        mCallbackLock.unlock();
        if (autoFocusEnabled)
            cb(CAMERA_MSG_FOCUS, true, 0, data);
        goto done;
    }

    // Skip autofocus if focus mode is infinity.
    if ((mParameters.get(CameraParameters::KEY_FOCUS_MODE) == 0)
           || (strcmp(mParameters.get(CameraParameters::KEY_FOCUS_MODE),
               CameraParameters::FOCUS_MODE_INFINITY) == 0)) {
        goto done;
    }

    mAutoFocusFd = open(MSM_CAMERA_CONTROL, O_RDWR);
    if (mAutoFocusFd < 0) {
        LOGE("autofocus: cannot open %s: %s",
             MSM_CAMERA_CONTROL,
             strerror(errno));
        mAutoFocusThreadRunning = false;
        mAutoFocusThreadLock.unlock();
        return;
    }

#if DLOPEN_LIBMMCAMERA
    // We need to maintain a reference to libqcamera.so for the duration of the
    // AF thread, because we do not know when it will exit relative to the
    // lifetime of this object.  We do not want to dlclose() libqcamera while
    // LINK_cam_frame is still running.
    libhandle = ::dlopen("liboemcamera.so", RTLD_NOW);
    LOGV("AF: loading libqcamera at %p", libhandle);
    if (!libhandle) {
        LOGE("FATAL ERROR: could not dlopen liboemcamera.so: %s", dlerror());
        close(mAutoFocusFd);
        mAutoFocusFd = -1;
        mAutoFocusThreadRunning = false;
        mAutoFocusThreadLock.unlock();
        return;
    }
#endif

    afMode = (isp3a_af_mode_t)attr_lookup(focus_modes,
                                sizeof(focus_modes) / sizeof(str_map),
                                mParameters.get(CameraParameters::KEY_FOCUS_MODE));

    /* This will block until either AF completes or is cancelled. */
    LOGV("af start (fd %d mode %d)", mAutoFocusFd, afMode);
    status_t err;
    err = mAfLock.tryLock();
    if(err == NO_ERROR) {
        {
            Mutex::Autolock cameraRunningLock(&mCameraRunningLock);
            if(mCameraRunning){
                LOGV("Start AF");
                status = native_set_afmode(mAutoFocusFd, afMode);
            }else{
                LOGV("As Camera preview is not running, AF not issued");
                status = false;
            }
        }
        mAfLock.unlock();
    }
    else{
        //AF Cancel would have acquired the lock,
        //so, no need to perform any AF
        LOGV("As Cancel auto focus is in progress, auto focus request "
                "is ignored");
        status = FALSE;
    }

    LOGV("af done: %d", (int)status);
    close(mAutoFocusFd);
    mAutoFocusFd = -1;

done:
    mAutoFocusThreadRunning = false;
    mAutoFocusThreadLock.unlock();

    mCallbackLock.lock();
    bool autoFocusEnabled = mNotifyCallback && (mMsgEnabled & CAMERA_MSG_FOCUS);
    notify_callback cb = mNotifyCallback;
    void *data = mCallbackCookie;
    mCallbackLock.unlock();
    if (autoFocusEnabled)
        cb(CAMERA_MSG_FOCUS, status, 0, data);

#if DLOPEN_LIBMMCAMERA
    if (libhandle) {
        ::dlclose(libhandle);
        LOGV("AF: dlclose(libqcamera)");
    }
#endif
}

status_t QualcommCameraHardware::cancelAutoFocusInternal()
{
    LOGV("cancelAutoFocusInternal E");

    if(!sensorType->hasAutoFocusSupport){
        LOGV("cancelAutoFocusInternal X");
        return NO_ERROR;
    }

#if 0
    if (mAutoFocusFd < 0) {
        LOGV("cancelAutoFocusInternal X: not in progress");
        return NO_ERROR;
    }
#endif

    status_t rc = NO_ERROR;
    status_t err;
    err = mAfLock.tryLock();
    if(err == NO_ERROR) {
        //Got Lock, means either AF hasn't started or
        // AF is done. So no need to cancel it, just change the state
        LOGV("As Auto Focus is not in progress, Cancel Auto Focus "
                "is ignored");
        mAfLock.unlock();
    }
    else {
        //AF is in Progess, So cancel it
        LOGV("Lock busy...cancel AF");
        rc = native_cancel_afmode(mCameraControlFd, mAutoFocusFd) ?
                NO_ERROR :
                UNKNOWN_ERROR;
    }



    LOGV("cancelAutoFocusInternal X: %d", rc);
    return rc;
}

void *auto_focus_thread(void *user)
{
    LOGV("auto_focus_thread E");
    sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
    if (obj != 0) {
        obj->runAutoFocus();
    }
    else LOGW("not starting autofocus: the object went away!");
    LOGV("auto_focus_thread X");
    return NULL;
}

status_t QualcommCameraHardware::autoFocus()
{
    LOGV("autoFocus E");
    Mutex::Autolock l(&mLock);

    if (mCameraControlFd < 0) {
        LOGE("not starting autofocus: main control fd %d", mCameraControlFd);
        return UNKNOWN_ERROR;
    }

    {
        mAutoFocusThreadLock.lock();
        if (!mAutoFocusThreadRunning) {
            if (native_prepare_snapshot(mCameraControlFd) == FALSE) {
               LOGE("native_prepare_snapshot failed!\n");
               mAutoFocusThreadLock.unlock();
               return UNKNOWN_ERROR;
            }

            // Create a detached thread here so that we don't have to wait
            // for it when we cancel AF.
            pthread_t thr;
            pthread_attr_t attr;
            pthread_attr_init(&attr);
            pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
            mAutoFocusThreadRunning =
                !pthread_create(&thr, &attr,
                                auto_focus_thread, NULL);
            if (!mAutoFocusThreadRunning) {
                LOGE("failed to start autofocus thread");
                mAutoFocusThreadLock.unlock();
                return UNKNOWN_ERROR;
            }
        }
        mAutoFocusThreadLock.unlock();
    }

    LOGV("autoFocus X");
    return NO_ERROR;
}

status_t QualcommCameraHardware::cancelAutoFocus()
{
    LOGV("cancelAutoFocus E");
    Mutex::Autolock l(&mLock);

    int rc = NO_ERROR;
    if (mCameraRunning && mNotifyCallback && (mMsgEnabled & CAMERA_MSG_FOCUS)) {
        rc = cancelAutoFocusInternal();
    }

    LOGV("cancelAutoFocus X");
    return rc;
}

void QualcommCameraHardware::runSnapshotThread(void *data)
{
    LOGV("runSnapshotThread E");
    if(mSnapshotFormat == PICTURE_FORMAT_JPEG){
        if (native_start_snapshot(mCameraControlFd))
            receiveRawPicture();
        else
            LOGE("main: native_start_snapshot failed!");
    }
    mInSnapshotModeWaitLock.lock();
    mInSnapshotMode = false;
    mInSnapshotModeWait.signal();
    mInSnapshotModeWaitLock.unlock();

    mSnapshotFormat = 0;

    mJpegThreadWaitLock.lock();
    while (mJpegThreadRunning) {
        LOGV("runSnapshotThread: waiting for jpeg thread to complete.");
        mJpegThreadWait.wait(mJpegThreadWaitLock);
        LOGV("runSnapshotThread: jpeg thread completed.");
    }
    mJpegThreadWaitLock.unlock();
    //clear the resources
    LINK_jpeg_encoder_join();
    deinitRaw();

    mSnapshotThreadWaitLock.lock();
    mSnapshotThreadRunning = false;
    mSnapshotThreadWait.signal();
    mSnapshotThreadWaitLock.unlock();

    LOGV("runSnapshotThread X");
}

void *snapshot_thread(void *user)
{
    LOGD("snapshot_thread E");
    sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
    if (obj != 0) {
        obj->runSnapshotThread(user);
    }
    else LOGW("not starting snapshot thread: the object went away!");
    LOGD("snapshot_thread X");
    return NULL;
}

status_t QualcommCameraHardware::takePicture()
{
    LOGV("takePicture(%d)", mMsgEnabled);
    Mutex::Autolock l(&mLock);

    // Wait for old snapshot thread to complete.
    mSnapshotThreadWaitLock.lock();
    while (mSnapshotThreadRunning) {
        LOGV("takePicture: waiting for old snapshot thread to complete.");
        mSnapshotThreadWait.wait(mSnapshotThreadWaitLock);
        LOGV("takePicture: old snapshot thread completed.");
    }

        mSnapshotFormat = PICTURE_FORMAT_JPEG;

    if(mSnapshotFormat == PICTURE_FORMAT_JPEG){
        if(!native_prepare_snapshot(mCameraControlFd)) {
            mSnapshotThreadWaitLock.unlock();
            return UNKNOWN_ERROR;
        }
    }

    stopPreviewInternal();

    if(mSnapshotFormat == PICTURE_FORMAT_JPEG){
        if (!initRaw(mDataCallback && (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE))) {
            LOGE("initRaw failed.  Not taking picture.");
            mSnapshotThreadWaitLock.unlock();
            return UNKNOWN_ERROR;
        }
    }
    mShutterLock.lock();
    mShutterPending = true;
    mShutterLock.unlock();

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    mSnapshotThreadRunning = !pthread_create(&mSnapshotThread,
                                             &attr,
                                             snapshot_thread,
                                             NULL);
    mSnapshotThreadWaitLock.unlock();

    mInSnapshotModeWaitLock.lock();
    mInSnapshotMode = true;
    mInSnapshotModeWaitLock.unlock();

    LOGV("takePicture: X");
    return mSnapshotThreadRunning ? NO_ERROR : UNKNOWN_ERROR;
}

status_t QualcommCameraHardware::cancelPicture()
{
    status_t rc;
    LOGV("cancelPicture: E");
    rc = native_stop_snapshot(mCameraControlFd) ? NO_ERROR : UNKNOWN_ERROR;
    LOGV("cancelPicture: X: %d", rc);
    return rc;
}

status_t QualcommCameraHardware::setParameters(const CameraParameters& params)
{
    LOGV("setParameters: E params = %p", &params);

    Mutex::Autolock l(&mLock);
    status_t rc, final_rc = NO_ERROR;

    if ((rc = setPreviewSize(params))) final_rc = rc;
    if ((rc = setPreviewFrameRate(params))) final_rc = rc;
    if ((rc = setPictureSize(params)))  final_rc = rc;
    if ((rc = setJpegQuality(params)))  final_rc = rc;
    if ((rc = setPictureFormat(params))) final_rc = rc;

    LOGV("setParameters: X");
    return NO_ERROR;
}

CameraParameters QualcommCameraHardware::getParameters() const
{
    LOGV("getParameters: EX");
    return mParameters;
}

status_t QualcommCameraHardware::sendCommand(int32_t command, int32_t arg1,
                                             int32_t arg2)
{
    LOGV("sendCommand: EX");
    return BAD_VALUE;
}

extern "C" sp<CameraHardwareInterface> openCameraHardware()
{
    LOGV("openCameraHardware: call createInstance");
    return QualcommCameraHardware::createInstance();
}

static CameraInfo sCameraInfo[] = {
	{
		CAMERA_FACING_BACK,
		90,  /* orientation */
	}
};

extern "C" int HAL_getNumberOfCameras()
{
	return sizeof(sCameraInfo) / sizeof(sCameraInfo[0]);
}

extern "C" void HAL_getCameraInfo(int cameraId, struct CameraInfo* cameraInfo)
{
	memcpy(cameraInfo, &sCameraInfo[cameraId], sizeof(CameraInfo));
}

extern "C" sp<CameraHardwareInterface> HAL_openCameraHardware(int cameraId)
{
	LOGV("openCameraHardware: call createInstance");
	return QualcommCameraHardware::createInstance();
}

wp<QualcommCameraHardware> QualcommCameraHardware::singleton;

// If the hardware already exists, return a strong pointer to the current
// object. If not, create a new hardware object, put it in the singleton,
// and return it.
sp<CameraHardwareInterface> QualcommCameraHardware::createInstance()
{
    LOGD("createInstance: E");

    singleton_lock.lock();

    // Wait until the previous release is done.
    while (singleton_releasing) {
        if((singleton_releasing_start_time != 0) &&
                (systemTime() - singleton_releasing_start_time) > SINGLETON_RELEASING_WAIT_TIME){
            LOGV("in createinstance system time is %lld %lld %lld ",
                    systemTime(), singleton_releasing_start_time, SINGLETON_RELEASING_WAIT_TIME);
            singleton_lock.unlock();
            LOGE("Previous singleton is busy and time out exceeded. Returning null");
            return NULL;
        }
        LOGI("Wait for previous release.");
        singleton_wait.waitRelative(singleton_lock, SINGLETON_RELEASING_RECHECK_TIMEOUT);
        LOGI("out of Wait for previous release.");
    }

    if (singleton != 0) {
        sp<CameraHardwareInterface> hardware = singleton.promote();
        if (hardware != 0) {
            LOGD("createInstance: X return existing hardware=%p", &(*hardware));
            singleton_lock.unlock();
            return hardware;
        }
    }

    {
        struct stat st;
        int rc = stat("/dev/oncrpc", &st);
        if (rc < 0) {
            LOGD("createInstance: X failed to create hardware: %s", strerror(errno));
            singleton_lock.unlock();
            return NULL;
        }
    }

    QualcommCameraHardware *cam = new QualcommCameraHardware();
    sp<QualcommCameraHardware> hardware(cam);
    singleton = hardware;

    if (!cam->startCamera()) {
        LOGE("%s: startCamera failed!", __FUNCTION__);
        singleton_lock.unlock();
        return NULL;
    }

    cam->initDefaultParameters();
    LOGD("createInstance: X created hardware=%p", &(*hardware));
    singleton_lock.unlock();
    return hardware;
}

// For internal use only, hence the strong pointer to the derived type.
sp<QualcommCameraHardware> QualcommCameraHardware::getInstance()
{
    sp<CameraHardwareInterface> hardware = singleton.promote();
    if (hardware != 0) {
        //    LOGV("getInstance: X old instance of hardware");
        return sp<QualcommCameraHardware>(static_cast<QualcommCameraHardware*>(hardware.get()));
    } else {
        LOGV("getInstance: X new instance of hardware");
        return sp<QualcommCameraHardware>();
    }
}


bool QualcommCameraHardware::native_zoom_image(int fd, int srcOffset, int dstOffSet, common_crop_t *crop)
{
    int result = 0;
    struct mdp_blit_req *e;
    struct timeval td1, td2;

    /* Initialize yuv structure */
    zoomImage.list.count = 1;

    e = &zoomImage.list.req[0];

    e->src.width = previewWidth;
    e->src.height = previewHeight;
    e->src.format = MDP_Y_CBCR_H2V2;
    e->src.offset = srcOffset;
    e->src.memory_id = fd;

    e->dst.width = previewWidth;
    e->dst.height = previewHeight;
    e->dst.format = MDP_Y_CBCR_H2V2;
    e->dst.offset = dstOffSet;
    e->dst.memory_id = fd;

    e->transp_mask = 0xffffffff;
    e->flags = 0;
    e->alpha = 0xff;
    if (crop->in2_w != 0 || crop->in2_h != 0) {
        e->src_rect.x = (crop->out2_w - crop->in2_w + 1) / 2 - 1;
        e->src_rect.y = (crop->out2_h - crop->in2_h + 1) / 2 - 1;
        e->src_rect.w = crop->in2_w;
        e->src_rect.h = crop->in2_h;
    } else {
        e->src_rect.x = 0;
        e->src_rect.y = 0;
        e->src_rect.w = previewWidth;
        e->src_rect.h = previewHeight;
    }
    //LOGV(" native_zoom : SRC_RECT : x,y = %d,%d \t w,h = %d, %d",
    //        e->src_rect.x, e->src_rect.y, e->src_rect.w, e->src_rect.h);

    e->dst_rect.x = 0;
    e->dst_rect.y = 0;
    e->dst_rect.w = previewWidth;
    e->dst_rect.h = previewHeight;

    result = ioctl(fb_fd, MSMFB_BLIT, &zoomImage.list);
    if (result < 0) {
        LOGE("MSM_FBIOBLT failed! line=%d\n", __LINE__);
        return FALSE;
    }
    return TRUE;
}

void QualcommCameraHardware::debugShowPreviewFPS() const
{
    static int mFrameCount;
    static int mLastFrameCount = 0;
    static nsecs_t mLastFpsTime = 0;
    static float mFps = 0;
    mFrameCount++;
    nsecs_t now = systemTime();
    nsecs_t diff = now - mLastFpsTime;
    if (diff > ms2ns(250)) {
        mFps =  ((mFrameCount - mLastFrameCount) * float(s2ns(1))) / diff;
        LOGI("Preview Frames Per Second: %.4f", mFps);
        mLastFpsTime = now;
        mLastFrameCount = mFrameCount;
    }
}

void QualcommCameraHardware::debugShowVideoFPS() const
{
    static int mFrameCount;
    static int mLastFrameCount = 0;
    static nsecs_t mLastFpsTime = 0;
    static float mFps = 0;
    mFrameCount++;
    nsecs_t now = systemTime();
    nsecs_t diff = now - mLastFpsTime;
    if (diff > ms2ns(250)) {
        mFps =  ((mFrameCount - mLastFrameCount) * float(s2ns(1))) / diff;
        LOGI("Video Frames Per Second: %.4f", mFps);
        mLastFpsTime = now;
        mLastFrameCount = mFrameCount;
    }
}
void QualcommCameraHardware::receivePreviewFrame(struct msm_frame *frame)
{
//    LOGV("receivePreviewFrame E");

    if (!mCameraRunning) {
        LOGE("ignoring preview callback--camera has been stopped");
        return;
    }

    if (UNLIKELY(mDebugFps)) {
        debugShowPreviewFPS();
    }

    mCallbackLock.lock();
    int msgEnabled = mMsgEnabled;
    data_callback pcb = mDataCallback;
    void *pdata = mCallbackCookie;
    data_callback_timestamp rcb = mDataCallbackTimestamp;
    void *rdata = mCallbackCookie;
    mCallbackLock.unlock();

    // Find the offset within the heap of the current buffer.
    ssize_t offset_addr =
        (ssize_t)frame->buffer - (ssize_t)mPreviewHeap->mHeap->base();
    ssize_t offset = offset_addr / mPreviewHeap->mAlignedBufferSize;

    common_crop_t *crop = (common_crop_t *) (frame->cropinfo);

    mInPreviewCallback = true;
	if (crop->in2_w != 0 || crop->in2_h != 0) {
	    dstOffset = (dstOffset + 1) % NUM_MORE_BUFS;
	    offset = kPreviewBufferCount + dstOffset;
	    ssize_t dstOffset_addr = offset * mPreviewHeap->mAlignedBufferSize;
	    if( !native_zoom_image(mPreviewHeap->mHeap->getHeapID(),
			offset_addr, dstOffset_addr, crop)) {
		LOGE(" Error while doing MDP zoom ");
                offset = offset_addr / mPreviewHeap->mAlignedBufferSize;
	    }
	}
    if (pcb != NULL && (msgEnabled & CAMERA_MSG_PREVIEW_FRAME))
        pcb(CAMERA_MSG_PREVIEW_FRAME, mPreviewHeap->mBuffers[offset],
            pdata);

        if(rcb != NULL && (msgEnabled & CAMERA_MSG_VIDEO_FRAME)) {
            rcb(systemTime(), CAMERA_MSG_VIDEO_FRAME, mPreviewHeap->mBuffers[offset], rdata);
            Mutex::Autolock rLock(&mRecordFrameLock);
            if (mReleasedRecordingFrame != true) {
                LOGV("block waiting for frame release");
                mRecordWait.wait(mRecordFrameLock);
                LOGV("frame released, continuing");
            }
            mReleasedRecordingFrame = false;
        }
    mInPreviewCallback = false;

    LOGV("receivePreviewFrame X");
}


bool QualcommCameraHardware::initRecord()
{
    char *pmem_region;

    LOGV("initREcord E");

    mRecordFrameSize = (mDimension.video_width  * mDimension.video_height *3)/2;

        pmem_region = "/dev/pmem_adsp";

    mRecordHeap = new PmemPool(pmem_region,
                               MemoryHeapBase::READ_ONLY,
                                mCameraControlFd,
                                MSM_PMEM_VIDEO,
                                mRecordFrameSize,
                                kRecordBufferCount,
                                mRecordFrameSize,
                                "record");

    if (!mRecordHeap->initialized()) {
        mRecordHeap.clear();
        LOGE("initRecord X: could not initialize record heap.");
        return false;
    }
    for (int cnt = 0; cnt < kRecordBufferCount; cnt++) {
        recordframes[cnt].fd = mRecordHeap->mHeap->getHeapID();
        recordframes[cnt].buffer =
            (uint32_t)mRecordHeap->mHeap->base() + mRecordHeap->mAlignedBufferSize * cnt;
        recordframes[cnt].y_off = 0;
        recordframes[cnt].cbcr_off = mDimension.video_width  * mDimension.video_height;
        recordframes[cnt].path = OUTPUT_TYPE_V;

        LOGV ("initRecord :  record heap , video buffers  buffer=%lu fd=%d y_off=%d cbcr_off=%d \n",
          (unsigned long)recordframes[cnt].buffer, recordframes[cnt].fd, recordframes[cnt].y_off,
          recordframes[cnt].cbcr_off);
    }


    mVideoThreadWaitLock.lock();
    while (mVideoThreadRunning) {
        LOGV("initRecord: waiting for old video thread to complete.");
        mVideoThreadWait.wait(mVideoThreadWaitLock);
        LOGV("initRecord : old video thread completed.");
    }
    mVideoThreadWaitLock.unlock();

    LOGV("initREcord X");

    return true;
}

status_t QualcommCameraHardware::startRecording()
{
    LOGV("startRecording E");
    int ret;
    Mutex::Autolock l(&mLock);
    mReleasedRecordingFrame = false;
    if( (ret=startPreviewInternal())== NO_ERROR){

    }
    return ret;
}

void QualcommCameraHardware::stopRecording()
{
    LOGV("stopRecording: E");
    Mutex::Autolock l(&mLock);
    {
        mRecordFrameLock.lock();
        mReleasedRecordingFrame = true;
        mRecordWait.signal();
        mRecordFrameLock.unlock();

        if(mDataCallback && (mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME)) {
            LOGV("stopRecording: X, preview still in progress");
            return;
        }
    }
        stopPreviewInternal();

    recordingState = 0; // recording not started
    LOGV("stopRecording: X");
}

void QualcommCameraHardware::releaseRecordingFrame(
       const sp<IMemory>& mem __attribute__((unused)))
{
    LOGV("releaseRecordingFrame E");
    Mutex::Autolock rLock(&mRecordFrameLock);
    mReleasedRecordingFrame = true;
    mRecordWait.signal();

    LOGV("releaseRecordingFrame X");
}

bool QualcommCameraHardware::recordingEnabled()
{
    return mCameraRunning && mDataCallbackTimestamp && (mMsgEnabled & CAMERA_MSG_VIDEO_FRAME);
}

void QualcommCameraHardware::notifyShutter(common_crop_t *crop)
{
    mShutterLock.lock();
    image_rect_type size;

    if (mShutterPending && mNotifyCallback && (mMsgEnabled & CAMERA_MSG_SHUTTER)) {
        LOGV("out2_w=%d, out2_h=%d, in2_w=%d, in2_h=%d",
             crop->out2_w, crop->out2_h, crop->in2_w, crop->in2_h);
        LOGV("out1_w=%d, out1_h=%d, in1_w=%d, in1_h=%d",
             crop->out1_w, crop->out1_h, crop->in1_w, crop->in1_h);

        // To workaround a bug in MDP which happens if either
        // dimension > 2048, we display the thumbnail instead.
        mDisplayHeap = mRawHeap;
        if (crop->in1_w == 0 || crop->in1_h == 0) {
            // Full size
            size.width = mDimension.picture_width;
            size.height = mDimension.picture_height;
            if (size.width > 2048 || size.height > 2048) {
                size.width = mDimension.ui_thumbnail_width;
                size.height = mDimension.ui_thumbnail_height;
                mDisplayHeap = mThumbnailHeap;
            }
        } else {
            // Cropped
            size.width = (crop->in2_w + jpegPadding) & ~1;
            size.height = (crop->in2_h + jpegPadding) & ~1;
            if (size.width > 2048 || size.height > 2048) {
                size.width = (crop->in1_w + jpegPadding) & ~1;
                size.height = (crop->in1_h + jpegPadding) & ~1;
                mDisplayHeap = mThumbnailHeap;
            }
        }
        /* Now, invoke Notify Callback to unregister preview buffer
         * and register postview buffer with surface flinger. */
        mNotifyCallback(CAMERA_MSG_SHUTTER, (int32_t)&size, 0,
                        mCallbackCookie);
        mShutterPending = false;
    }
    mShutterLock.unlock();
}

static void receive_shutter_callback(common_crop_t *crop)
{
    LOGV("receive_shutter_callback: E");
    LOGV("receive_shutter_callback: X");
}

// Crop the picture in place.
static void crop_yuv420(uint32_t width, uint32_t height,
                 uint32_t cropped_width, uint32_t cropped_height,
                 uint8_t *image)
{
    uint32_t i, x, y;
    uint8_t* chroma_src, *chroma_dst;

    // Calculate the start position of the cropped area.
    x = (width - cropped_width) / 2;
    y = (height - cropped_height) / 2;
    x &= ~1;
    y &= ~1;

    // Copy luma component.
    for(i = 0; i < cropped_height; i++)
        memcpy(image + i * cropped_width,
               image + width * (y + i) + x,
               cropped_width);

    chroma_src = image + width * height;
    chroma_dst = image + cropped_width * cropped_height;

    // Copy chroma components.
    cropped_height /= 2;
    y /= 2;
    for(i = 0; i < cropped_height; i++)
        memcpy(chroma_dst + i * cropped_width,
               chroma_src + width * (y + i) + x,
               cropped_width);
}

void QualcommCameraHardware::receiveRawPicture()
{
    LOGV("receiveRawPicture: E");

    Mutex::Autolock cbLock(&mCallbackLock);
    if (mDataCallback && (mMsgEnabled & CAMERA_MSG_RAW_IMAGE)) {
        if(native_get_picture(mCameraControlFd, &mCrop) == false) {
            LOGE("getPicture failed!");
            return;
        }
        mCrop.in1_w &= ~1;
        mCrop.in1_h &= ~1;
        mCrop.in2_w &= ~1;
        mCrop.in2_h &= ~1;


        // Crop the image if zoomed.
        if (mCrop.in2_w != 0 && mCrop.in2_h != 0 &&
                ((mCrop.in2_w + jpegPadding) < mCrop.out2_w) &&
                ((mCrop.in2_h + jpegPadding) < mCrop.out2_h) &&
                ((mCrop.in1_w + jpegPadding) < mCrop.out1_w)  &&
                ((mCrop.in1_h + jpegPadding) < mCrop.out1_h) ) {

            // By the time native_get_picture returns, picture is taken. Call
            // shutter callback if cam config thread has not done that.
            notifyShutter(&mCrop);
                    crop_yuv420(mCrop.out2_w, mCrop.out2_h, (mCrop.in2_w + jpegPadding), (mCrop.in2_h + jpegPadding),
                            (uint8_t *)mRawHeap->mHeap->base());
                    crop_yuv420(mCrop.out1_w, mCrop.out1_h, (mCrop.in1_w + jpegPadding), (mCrop.in1_h + jpegPadding),
                            (uint8_t *)mThumbnailHeap->mHeap->base());

            // We do not need jpeg encoder to upscale the image. Set the new
            // dimension for encoder.
            mDimension.orig_picture_dx = mCrop.in2_w + jpegPadding;
            mDimension.orig_picture_dy = mCrop.in2_h + jpegPadding;
            mDimension.thumbnail_width = mCrop.in1_w + jpegPadding;
            mDimension.thumbnail_height = mCrop.in1_h + jpegPadding;
        }else {
            memset(&mCrop, 0 ,sizeof(mCrop));
            // By the time native_get_picture returns, picture is taken. Call
            // shutter callback if cam config thread has not done that.
            notifyShutter(&mCrop);
        }

   if (mDataCallback && (mMsgEnabled & CAMERA_MSG_RAW_IMAGE))
       mDataCallback(CAMERA_MSG_RAW_IMAGE, mDisplayHeap->mBuffers[0],
                            mCallbackCookie);
    }
    else LOGV("Raw-picture callback was canceled--skipping.");

    if (mDataCallback && (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE)) {
        mJpegSize = 0;
        mJpegThreadWaitLock.lock();
        if (LINK_jpeg_encoder_init()) {
            mJpegThreadRunning = true;
            mJpegThreadWaitLock.unlock();
            if(native_jpeg_encode()) {
                LOGV("receiveRawPicture: X (success)");
                return;
            }
            LOGE("jpeg encoding failed");
        }
        else {
            LOGE("receiveRawPicture X: jpeg_encoder_init failed.");
            mJpegThreadWaitLock.unlock();
        }
    }
    else LOGV("JPEG callback is NULL, not encoding image.");
    deinitRaw();
    LOGV("receiveRawPicture: X");
}

void QualcommCameraHardware::receiveJpegPictureFragment(
    uint8_t *buff_ptr, uint32_t buff_size)
{
    uint32_t remaining = mJpegHeap->mHeap->virtualSize();
    remaining -= mJpegSize;
    uint8_t *base = (uint8_t *)mJpegHeap->mHeap->base();

    LOGV("receiveJpegPictureFragment size %d", buff_size);
    if (buff_size > remaining) {
        LOGE("receiveJpegPictureFragment: size %d exceeds what "
             "remains in JPEG heap (%d), truncating",
             buff_size,
             remaining);
        buff_size = remaining;
    }
    memcpy(base + mJpegSize, buff_ptr, buff_size);
    mJpegSize += buff_size;
}

void QualcommCameraHardware::receiveJpegPicture(void)
{
    LOGV("receiveJpegPicture: E image (%d uint8_ts out of %d)",
         mJpegSize, mJpegHeap->mBufferSize);
    Mutex::Autolock cbLock(&mCallbackLock);

    int index = 0, rc;

    if (mDataCallback && (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE)) {
        // The reason we do not allocate into mJpegHeap->mBuffers[offset] is
        // that the JPEG image's size will probably change from one snapshot
        // to the next, so we cannot reuse the MemoryBase object.
        sp<MemoryBase> buffer = new
            MemoryBase(mJpegHeap->mHeap,
                       index * mJpegHeap->mBufferSize +
                       0,
                       mJpegSize);
        mDataCallback(CAMERA_MSG_COMPRESSED_IMAGE, buffer, mCallbackCookie);
        buffer = NULL;
    }
    else LOGV("JPEG callback was cancelled--not delivering image.");

    mJpegThreadWaitLock.lock();
    mJpegThreadRunning = false;
    mJpegThreadWait.signal();
    mJpegThreadWaitLock.unlock();

    LOGV("receiveJpegPicture: X callback done.");
}

bool QualcommCameraHardware::previewEnabled()
{
    return mCameraRunning && mDataCallback && (mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME);
}

status_t QualcommCameraHardware::setPreviewSize(const CameraParameters& params)
{
    int width, height;
    params.getPreviewSize(&width, &height);
    LOGV("requested preview size %d x %d", width, height);

    // Validate the preview size
    for (size_t i = 0; i < previewSizeCount; ++i) {
        if (width == supportedPreviewSizes[i].width
           && height == supportedPreviewSizes[i].height) {
                mDimension.display_width = width;
                mDimension.display_height= height;
            mParameters.setPreviewSize(width, height);
            return NO_ERROR;
        }
    }
    LOGE("Invalid preview size requested: %dx%d", width, height);
    return BAD_VALUE;
}

status_t QualcommCameraHardware::setPreviewFrameRate(const CameraParameters& params)
{
    if((!strcmp(mSensorInfo.name, "vx6953")) ||
        (!strcmp(mSensorInfo.name, "VX6953")) ||
        (!strcmp(sensorType->name, "2mp"))){
        LOGI("set fps is not supported for this sensor");
        return NO_ERROR;
    }
    uint16_t previousFps = (uint16_t)mParameters.getPreviewFrameRate();
    uint16_t fps = (uint16_t)params.getPreviewFrameRate();
    LOGV("requested preview frame rate  is %u", fps);

    if(mInitialized && (fps == previousFps)){
        LOGV("fps same as previous fps");
        return NO_ERROR;
    }

    if(MINIMUM_FPS <= fps && fps <=MAXIMUM_FPS){
        mParameters.setPreviewFrameRate(fps);
        bool ret = native_set_parm(CAMERA_SET_PARM_FPS,
                sizeof(fps), (void *)&fps);
        return ret ? NO_ERROR : UNKNOWN_ERROR;
    }
    return BAD_VALUE;

}

status_t QualcommCameraHardware::setPictureSize(const CameraParameters& params)
{
    int width, height;
    params.getPictureSize(&width, &height);
    LOGV("requested picture size %d x %d", width, height);

    // Validate the picture size
    for (int i = 0; i < supportedPictureSizesCount; ++i) {
        if (width == picture_sizes_ptr[i].width
                && height == picture_sizes_ptr[i].height) {
            if (!strcmp(mSensorInfo.name, "ov5642") 
					&& width == 2592) {
				/* WTF... The max this "5MPx" sensor supports is 4.75 */
                width = 2560 ; height = 1920;
            }
            mParameters.setPictureSize(width, height);
            mDimension.picture_width = width;
            mDimension.picture_height = height;
            return NO_ERROR;
        }
    }
    /* Dimension not among the ones in the list. Check if
     * its a valid dimension, if it is, then configure the
     * camera accordingly. else reject it.
     */
    if( isValidDimension(width, height) ) {
        mParameters.setPictureSize(width, height);
        mDimension.picture_width = width;
        mDimension.picture_height = height;
        return NO_ERROR;
    } else
        LOGE("Invalid picture size requested: %dx%d", width, height);
    return BAD_VALUE;
}

status_t QualcommCameraHardware::setJpegQuality(const CameraParameters& params) {
    status_t rc = NO_ERROR;
    int quality = params.getInt(CameraParameters::KEY_JPEG_QUALITY);
    if (quality > 0 && quality <= 100) {
        mParameters.set(CameraParameters::KEY_JPEG_QUALITY, quality);
    } else {
        LOGE("Invalid jpeg quality=%d", quality);
        rc = BAD_VALUE;
    }

    quality = params.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY);
    if (quality > 0 && quality <= 100) {
        mParameters.set(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY, quality);
    } else {
        LOGE("Invalid jpeg thumbnail quality=%d", quality);
        rc = BAD_VALUE;
    }
    return rc;
}


status_t QualcommCameraHardware::setBrightness(const CameraParameters& params) {
        int brightness = params.getInt("luma-adaptation");
        if (mBrightness !=  brightness) {
            LOGV(" new brightness value : %d ", brightness);
            mBrightness =  brightness;

            bool ret = native_set_parm(CAMERA_SET_PARM_BRIGHTNESS, sizeof(mBrightness),
                                       (void *)&mBrightness);
            return ret ? NO_ERROR : UNKNOWN_ERROR;
        } else {
            return NO_ERROR;
        }
}

status_t QualcommCameraHardware::setZoom(const CameraParameters& params)
{
    status_t rc = NO_ERROR;
    // No matter how many different zoom values the driver can provide, HAL
    // provides applictations the same number of zoom levels. The maximum driver
    // zoom value depends on sensor output (VFE input) and preview size (VFE
    // output) because VFE can only crop and cannot upscale. If the preview size
    // is bigger, the maximum zoom ratio is smaller. However, we want the
    // zoom ratio of each zoom level is always the same whatever the preview
    // size is. Ex: zoom level 1 is always 1.2x, zoom level 2 is 1.44x, etc. So,
    // we need to have a fixed maximum zoom value and do read it from the
    // driver.
    static const int ZOOM_STEP = 1;
    int32_t zoom_level = params.getInt("zoom");

    LOGV("Set zoom=%d", zoom_level);
    if(zoom_level >= 0 && zoom_level <= mMaxZoom) {
        mParameters.set("zoom", zoom_level);
        int32_t zoom_value = ZOOM_STEP * zoom_level;
        bool ret = native_set_parm(CAMERA_SET_PARM_ZOOM,
            sizeof(zoom_value), (void *)&zoom_value);
        rc = ret ? NO_ERROR : UNKNOWN_ERROR;
    } else {
        rc = BAD_VALUE;
    }

    return rc;
}

status_t QualcommCameraHardware::setPictureFormat(const CameraParameters& params)
{
    const char * str = params.get(CameraParameters::KEY_PICTURE_FORMAT);

    if(str != NULL){
        int32_t value = attr_lookup(picture_formats,
                                    sizeof(picture_formats) / sizeof(str_map), str);
        if(value != NOT_FOUND){
            mParameters.set(CameraParameters::KEY_PICTURE_FORMAT, str);
        } else {
            LOGE("Invalid Picture Format value: %s", str);
            return BAD_VALUE;
        }
    }
    return NO_ERROR;
}

QualcommCameraHardware::MemPool::MemPool(int buffer_size, int num_buffers,
                                         int frame_size,
                                         const char *name) :
    mBufferSize(buffer_size),
    mNumBuffers(num_buffers),
    mFrameSize(frame_size),
    mBuffers(NULL), mName(name)
{
    int page_size_minus_1 = getpagesize() - 1;
    mAlignedBufferSize = (buffer_size + page_size_minus_1) & (~page_size_minus_1);
}

void QualcommCameraHardware::MemPool::completeInitialization()
{
    // If we do not know how big the frame will be, we wait to allocate
    // the buffers describing the individual frames until we do know their
    // size.

    if (mFrameSize > 0) {
        mBuffers = new sp<MemoryBase>[mNumBuffers];
        for (int i = 0; i < mNumBuffers; i++) {
            mBuffers[i] = new
                MemoryBase(mHeap,
                           i * mAlignedBufferSize,
                           mFrameSize);
        }
    }
}

QualcommCameraHardware::AshmemPool::AshmemPool(int buffer_size, int num_buffers,
                                               int frame_size,
                                               const char *name) :
    QualcommCameraHardware::MemPool(buffer_size,
                                    num_buffers,
                                    frame_size,
                                    name)
{
    LOGV("constructing MemPool %s backed by ashmem: "
         "%d frames @ %d uint8_ts, "
         "buffer size %d",
         mName,
         num_buffers, frame_size, buffer_size);

    int page_mask = getpagesize() - 1;
    int ashmem_size = buffer_size * num_buffers;
    ashmem_size += page_mask;
    ashmem_size &= ~page_mask;

    mHeap = new MemoryHeapBase(ashmem_size);

    completeInitialization();
}

static bool register_buf(int camfd,
                         int size,
                         int frame_size,
                         int pmempreviewfd,
                         uint32_t offset,
                         uint8_t *buf,
                         int pmem_type,
                         bool vfe_can_write,
                         bool register_buffer = true);

QualcommCameraHardware::PmemPool::PmemPool(const char *pmem_pool,
                                           int flags,
                                           int camera_control_fd,
                                           int pmem_type,
                                           int buffer_size, int num_buffers,
                                           int frame_size,
                                           const char *name) :
    QualcommCameraHardware::MemPool(buffer_size,
                                    num_buffers,
                                    frame_size,
                                    name),
    mPmemType(pmem_type),
    mCameraControlFd(dup(camera_control_fd))
{
    LOGV("constructing MemPool %s backed by pmem pool %s: "
         "%d frames @ %d bytes, buffer size %d",
         mName,
         pmem_pool, num_buffers, frame_size,
         buffer_size);

    LOGV("%s: duplicating control fd %d --> %d",
         __FUNCTION__,
         camera_control_fd, mCameraControlFd);

    // Make a new mmap'ed heap that can be shared across processes.
    // mAlignedBufferSize is already in 4k aligned. (do we need total size necessary to be in power of 2??)
    mAlignedSize = mAlignedBufferSize * num_buffers;

    sp<MemoryHeapBase> masterHeap =
        new MemoryHeapBase(pmem_pool, mAlignedSize, flags);

    if (masterHeap->getHeapID() < 0) {
        LOGE("failed to construct master heap for pmem pool %s", pmem_pool);
        masterHeap.clear();
        return;
    }

    sp<MemoryHeapPmem> pmemHeap = new MemoryHeapPmem(masterHeap, flags);
    if (pmemHeap->getHeapID() >= 0) {
        pmemHeap->slap();
        masterHeap.clear();
        mHeap = pmemHeap;
        pmemHeap.clear();

        mFd = mHeap->getHeapID();
        if (::ioctl(mFd, PMEM_GET_SIZE, &mSize)) {
            LOGE("pmem pool %s ioctl(PMEM_GET_SIZE) error %s (%d)",
                 pmem_pool,
                 ::strerror(errno), errno);
            mHeap.clear();
            return;
        }

        LOGV("pmem pool %s ioctl(fd = %d, PMEM_GET_SIZE) is %ld",
             pmem_pool,
             mFd,
             mSize.len);
        LOGD("mBufferSize=%d, mAlignedBufferSize=%d\n", mBufferSize, mAlignedBufferSize);
        // Unregister preview buffers with the camera drivers.  Allow the VFE to write
        // to all preview buffers except for the last one.
        // Only Register the preview, snapshot and thumbnail buffers with the kernel.
        if( (strcmp("postview", mName) != 0) ){
            int num_buf = num_buffers;
            if(!strcmp("preview", mName)) num_buf = kPreviewBufferCount;
            LOGD("num_buffers = %d", num_buf);
            for (int cnt = 0; cnt < num_buf; ++cnt) {
                int active = 1;
                if(pmem_type == MSM_PMEM_VIDEO){
                     active = (cnt<ACTIVE_VIDEO_BUFFERS);
                     LOGV(" pmempool creating video buffers : active %d ", active);
                }
                else if (pmem_type == MSM_PMEM_PREVIEW){
                     active = (cnt < (num_buf-1));
                }
                register_buf(mCameraControlFd,
                         mBufferSize,
                         mFrameSize,
                         mHeap->getHeapID(),
                         mAlignedBufferSize * cnt,
                         (uint8_t *)mHeap->base() + mAlignedBufferSize * cnt,
                         pmem_type,
                         active);
            }
        }

        completeInitialization();
    }
    else LOGE("pmem pool %s error: could not create master heap!",
              pmem_pool);
}

QualcommCameraHardware::PmemPool::~PmemPool()
{
    LOGV("%s: %s E", __FUNCTION__, mName);
    if (mHeap != NULL) {
        // Unregister preview buffers with the camera drivers.
        //  Only Unregister the preview, snapshot and thumbnail
        //  buffers with the kernel.
        if( (strcmp("postview", mName) != 0) ){
            int num_buffers = mNumBuffers;
            if(!strcmp("preview", mName)) num_buffers = kPreviewBufferCount;
            for (int cnt = 0; cnt < num_buffers; ++cnt) {
                register_buf(mCameraControlFd,
                         mBufferSize,
                         mFrameSize,
                         mHeap->getHeapID(),
                         mAlignedBufferSize * cnt,
                         (uint8_t *)mHeap->base() + mAlignedBufferSize * cnt,
                         mPmemType,
                         false,
                         false /* unregister */);
            }
        }
    }
    LOGV("destroying PmemPool %s: closing control fd %d",
         mName,
         mCameraControlFd);
    close(mCameraControlFd);
    LOGV("%s: %s X", __FUNCTION__, mName);
}

QualcommCameraHardware::MemPool::~MemPool()
{
    LOGV("destroying MemPool %s", mName);
    if (mFrameSize > 0)
        delete [] mBuffers;
    mHeap.clear();
    LOGV("destroying MemPool %s completed", mName);
}

static bool register_buf(int camfd,
                         int size,
                         int frame_size,
                         int pmempreviewfd,
                         uint32_t offset,
                         uint8_t *buf,
                         int pmem_type,
                         bool vfe_can_write,
                         bool register_buffer)
{
    struct msm_pmem_info pmemBuf;

    pmemBuf.type     = pmem_type;
    pmemBuf.fd       = pmempreviewfd;
    pmemBuf.offset   = offset;
    pmemBuf.len      = size;
    pmemBuf.vaddr    = buf;
    pmemBuf.y_off    = 0;

    if(pmem_type == MSM_PMEM_RAW_MAINIMG)
        pmemBuf.cbcr_off = 0;
    else
        pmemBuf.cbcr_off = PAD_TO_WORD(frame_size * 2 / 3);

    pmemBuf.active   = vfe_can_write;

    LOGV("register_buf: camfd = %d, reg = %d buffer = %p",
         camfd, !register_buffer, buf);
    if (ioctl(camfd,
              register_buffer ?
              MSM_CAM_IOCTL_REGISTER_PMEM :
              MSM_CAM_IOCTL_UNREGISTER_PMEM,
              &pmemBuf) < 0) {
        LOGE("register_buf: MSM_CAM_IOCTL_(UN)REGISTER_PMEM fd %d error %s",
             camfd,
             strerror(errno));
        return false;
    }
    return true;
}

status_t QualcommCameraHardware::MemPool::dump(int fd, const Vector<String16>& args) const
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;
    snprintf(buffer, 255, "QualcommCameraHardware::AshmemPool::dump\n");
    result.append(buffer);
    if (mName) {
        snprintf(buffer, 255, "mem pool name (%s)\n", mName);
        result.append(buffer);
    }
    if (mHeap != 0) {
        snprintf(buffer, 255, "heap base(%p), size(%d), flags(%d), device(%s)\n",
                 mHeap->getBase(), mHeap->getSize(),
                 mHeap->getFlags(), mHeap->getDevice());
        result.append(buffer);
    }
    snprintf(buffer, 255,
             "buffer size (%d), number of buffers (%d), frame size(%d)",
             mBufferSize, mNumBuffers, mFrameSize);
    result.append(buffer);
    write(fd, result.string(), result.size());
    return NO_ERROR;
}

static void receive_camframe_callback(struct msm_frame *frame)
{
    sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
    if (obj != 0) {
        obj->receivePreviewFrame(frame);
    }
}

static void receive_jpeg_fragment_callback(uint8_t *buff_ptr, uint32_t buff_size)
{
    LOGV("receive_jpeg_fragment_callback E");
    sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
    if (obj != 0) {
        obj->receiveJpegPictureFragment(buff_ptr, buff_size);
    }
    LOGV("receive_jpeg_fragment_callback X");
}

static void receive_jpeg_callback(jpeg_event_t status)
{
    LOGV("receive_jpeg_callback E (completion status %d)", status);
    if (status == JPEG_EVENT_DONE) {
        sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
        if (obj != 0) {
            obj->receiveJpegPicture();
        }
    }
    LOGV("receive_jpeg_callback X");
}

void QualcommCameraHardware::setCallbacks(notify_callback notify_cb,
                             data_callback data_cb,
                             data_callback_timestamp data_cb_timestamp,
                             void* user)
{
    Mutex::Autolock lock(mLock);
    mNotifyCallback = notify_cb;
    mDataCallback = data_cb;
    mDataCallbackTimestamp = data_cb_timestamp;
    mCallbackCookie = user;
}

void QualcommCameraHardware::enableMsgType(int32_t msgType)
{
    Mutex::Autolock lock(mLock);
    mMsgEnabled |= msgType;
}

void QualcommCameraHardware::disableMsgType(int32_t msgType)
{
    Mutex::Autolock lock(mLock);
    mMsgEnabled &= ~msgType;
}

bool QualcommCameraHardware::msgTypeEnabled(int32_t msgType)
{
    return (mMsgEnabled & msgType);
}

bool QualcommCameraHardware::useOverlay(void)
{
        mUseOverlay = FALSE;

    LOGV(" Using Overlay : %s ", mUseOverlay ? "YES" : "NO" );
    return mUseOverlay;
}

status_t QualcommCameraHardware::setOverlay(const sp<Overlay> &Overlay)
{
    if( Overlay != NULL) {
        LOGV(" Valid overlay object ");
        mOverlayLock.lock();
        mOverlay = Overlay;
        mOverlayLock.unlock();
    } else {
        LOGV(" Overlay object NULL. returning ");
        mOverlay = NULL;
        return UNKNOWN_ERROR;
    }
    return NO_ERROR;
}

void QualcommCameraHardware::receive_camframetimeout(void) {
    LOGV("receive_camframetimeout: E");
    Mutex::Autolock l(&mCamframeTimeoutLock);
    LOGD(" Camframe timed out. Not receiving any frames from camera driver ");
    camframe_timeout_flag = TRUE;
    mNotifyCallback(CAMERA_MSG_ERROR, CAMERA_ERROR_UKNOWN, 0,
                    mCallbackCookie);
    LOGV("receive_camframetimeout: X");
}

static void receive_camframetimeout_callback(void) {
    sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
    if (obj != 0) {
        obj->receive_camframetimeout();
    }
}

void QualcommCameraHardware::storePreviewFrameForPostview(void) {
    LOGV(" storePreviewFrameForPostview : E ");

    /* Since there is restriction on the maximum overlay dimensions
     * that can be created, we use the last preview frame as postview
     * for 7x30. */
    LOGV(" Copying the preview buffer to postview buffer %d  ", 
         mPreviewFrameSize);
    if( mPostViewHeap != NULL && mLastQueuedFrame != NULL) {
	memcpy(mPostViewHeap->mHeap->base(),
		(uint8_t *)mLastQueuedFrame, mPreviewFrameSize );
    } else
        LOGE(" Failed to store Preview frame. No Postview ");

    LOGV(" storePreviewFrameForPostview : X ");
}

bool QualcommCameraHardware::isValidDimension(int width, int height) {
    bool retVal = FALSE;
    /* This function checks if a given resolution is valid or not.
     * A particular resolution is considered valid if it satisfies
     * the following conditions:
     * 1. width & height should be multiple of 16.
     * 2. width & height should be less than/equal to the dimensions
     *    supported by the camera sensor.
     * 3. the aspect ratio is a valid aspect ratio and is among the
     *    commonly used aspect ratio as determined by the thumbnail_sizes
     *    data structure.
     */

    if( (width == CEILING16(width)) && (height == CEILING16(height))
     && (width <= sensorType->max_supported_snapshot_width)
     && (height <= sensorType->max_supported_snapshot_height) )
    {
        uint32_t pictureAspectRatio = (uint32_t)((width * Q12)/height);
        for(int i = 0; i < THUMBNAIL_SIZE_COUNT; i++ ) {
            if(thumbnail_sizes[i].aspect_ratio == pictureAspectRatio) {
                retVal = TRUE;
                break;
            }
        }
    }
    return retVal;
}
status_t QualcommCameraHardware::getBufferInfo(sp<IMemory>& Frame, size_t *alignedSize) {
    status_t ret;
    LOGV(" getBufferInfo : E ");
	if(mPreviewHeap != NULL) {
		LOGV(" Setting valid buffer information ");
		Frame = mPreviewHeap->mBuffers[0];
		if( alignedSize != NULL) {
			*alignedSize = mPreviewHeap->mAlignedBufferSize;
		        LOGV(" HAL : alignedSize = %d ", *alignedSize);
		        ret = NO_ERROR;
	        } else {
		        LOGE(" HAL : alignedSize is NULL. Cannot update alignedSize ");
		        ret = UNKNOWN_ERROR;
	        }
	} else {
	        LOGE(" PreviewHeap is null. Buffer information wont be updated ");
	        Frame = NULL;
	        ret = UNKNOWN_ERROR;
	}
    LOGV(" getBufferInfo : X ");
    return ret;
}

}; // namespace android
