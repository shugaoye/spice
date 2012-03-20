#include <config.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <wait.h>
#include <sys/select.h>
#include <sys/types.h>
#include <getopt.h>

#include <spice/qxl_dev.h>

#include "test_display_base.h"
#include "red_channel.h"
#include "test_util.h"

#define MEM_SLOT_GROUP_ID 0

/* Parts cribbed from spice-display.h/.c/qxl.c */

typedef struct SimpleSpiceUpdate {
    QXLCommandExt ext; // first
    QXLDrawable drawable;
    QXLImage image;
    uint8_t *bitmap;
} SimpleSpiceUpdate;

typedef struct SimpleSurfaceCmd {
    QXLCommandExt ext; // first
    QXLSurfaceCmd surface_cmd;
} SimpleSurfaceCmd;

static void test_spice_destroy_update(SimpleSpiceUpdate *update)
{
    if (!update) {
        return;
    }
    free(update->bitmap);
    free(update);
}

#define WIDTH 640
#define HEIGHT 320

#define SINGLE_PART 4
static const int angle_parts = 64 / SINGLE_PART;
static int unique = 1;
static int color = -1;
static int c_i = 0;

/* Used for automated tests */
static int control = 3; //used to know when we can take a screenshot
static int rects = 16; //number of rects that will be draw
static int has_automated_tests = 0; //automated test flag

static void sigchld_handler(int signal_num) // wait for the child process and exit
{
    int status;
    wait(&status);
    exit(0);
}

static void regression_test(void)
{
    pid_t pid;

    if (--rects != 0) {
        return;
    }

    rects = 16;

    if (--control != 0) {
        return;
    }

    pid = fork();
    if (pid == 0) {
        char buf[PATH_MAX];
        char *envp[] = {buf, NULL};

        snprintf(buf, sizeof(buf), "PATH=%s", getenv("PATH"));
        execve("regression_test.py", NULL, envp);
    } else if (pid > 0) {
        return;
    }
}

static void set_cmd(QXLCommandExt *ext, uint32_t type, QXLPHYSICAL data)
{
    ext->cmd.type = type;
    ext->cmd.data = data;
    ext->cmd.padding = 0;
    ext->group_id = MEM_SLOT_GROUP_ID;
    ext->flags = 0;
}

static void simple_set_release_info(QXLReleaseInfo *info, intptr_t ptr)
{
    info->id = ptr;
    //info->group_id = MEM_SLOT_GROUP_ID;
}

typedef struct Path {
    int t;
    int min_t;
    int max_t;
} Path;

static void path_init(Path *path, int min, int max)
{
    path->t = min;
    path->min_t = min;
    path->max_t = max;
}

static void path_progress(Path *path)
{
    path->t = (path->t+1)% (path->max_t - path->min_t) + path->min_t;
}

Path path;

static void draw_pos(int t, int *x, int *y)
{
#ifdef CIRCLE
    *y = HEIGHT/2 + (HEIGHT/3)*cos(t*2*M_PI/angle_parts);
    *x = WIDTH/2 + (WIDTH/3)*sin(t*2*M_PI/angle_parts);
#else
    *y = HEIGHT*(t % SINGLE_PART)/SINGLE_PART;
    *x = ((WIDTH/SINGLE_PART)*(t / SINGLE_PART)) % WIDTH;
#endif
}

static SimpleSpiceUpdate *test_spice_create_update_draw(uint32_t surface_id, int t)
{
    SimpleSpiceUpdate *update;
    QXLDrawable *drawable;
    QXLImage *image;
    int top, left;
    draw_pos(t, &left, &top);
    QXLRect bbox = {
        .top = top,
        .left = left,
    };
    uint8_t *dst;
    int bw, bh;
    int i;

    if ((t % angle_parts) == 0) {
        c_i++;
    }

    if(surface_id != 0) {
        color = (color + 1) % 2;
    } else {
        color = surface_id;
    }

    unique++;

    update   = calloc(sizeof(*update), 1);
    drawable = &update->drawable;
    image    = &update->image;

    bw       = WIDTH/SINGLE_PART;
    bh       = 48;

    bbox.right = bbox.left + bw;
    bbox.bottom = bbox.top + bh;
    update->bitmap = malloc(bw * bh * 4);
    //printf("allocated %p, %p\n", update, update->bitmap);

    drawable->surface_id      = surface_id;

    drawable->bbox            = bbox;
    drawable->clip.type       = SPICE_CLIP_TYPE_NONE;
    drawable->effect          = QXL_EFFECT_OPAQUE;
    simple_set_release_info(&drawable->release_info, (intptr_t)update);
    drawable->type            = QXL_DRAW_COPY;
    drawable->surfaces_dest[0] = -1;
    drawable->surfaces_dest[1] = -1;
    drawable->surfaces_dest[2] = -1;

    drawable->u.copy.rop_descriptor  = SPICE_ROPD_OP_PUT;
    drawable->u.copy.src_bitmap      = (intptr_t)image;
    drawable->u.copy.src_area.right  = bw;
    drawable->u.copy.src_area.bottom = bh;

    QXL_SET_IMAGE_ID(image, QXL_IMAGE_GROUP_DEVICE, unique);
    image->descriptor.type   = SPICE_IMAGE_TYPE_BITMAP;
    image->bitmap.flags      = QXL_BITMAP_DIRECT | QXL_BITMAP_TOP_DOWN;
    image->bitmap.stride     = bw * 4;
    image->descriptor.width  = image->bitmap.x = bw;
    image->descriptor.height = image->bitmap.y = bh;
    image->bitmap.data = (intptr_t)(update->bitmap);
    image->bitmap.palette = 0;
    image->bitmap.format = SPICE_BITMAP_FMT_32BIT;

    dst = update->bitmap;
    for (i = 0 ; i < bh * bw ; ++i, dst+=4) {
        *dst = (color+i % 255);
        *(dst+((1+c_i)%3)) = 255 - color;
        *(dst+((2+c_i)%3)) = (color * (color + i)) & 0xff;
        *(dst+((3+c_i)%3)) = 0;
    }

    set_cmd(&update->ext, QXL_CMD_DRAW, (intptr_t)drawable);

    return update;
}

static SimpleSpiceUpdate *test_spice_create_update_copy_bits(uint32_t surface_id)
{
    SimpleSpiceUpdate *update;
    QXLDrawable *drawable;
    int bw, bh;
    QXLRect bbox = {
        .left = 10,
        .top = 0,
    };

    update   = calloc(sizeof(*update), 1);
    drawable = &update->drawable;

    bw       = WIDTH/SINGLE_PART;
    bh       = 48;
    bbox.right = bbox.left + bw;
    bbox.bottom = bbox.top + bh;
    //printf("allocated %p, %p\n", update, update->bitmap);

    drawable->surface_id      = surface_id;

    drawable->bbox            = bbox;
    drawable->clip.type       = SPICE_CLIP_TYPE_NONE;
    drawable->effect          = QXL_EFFECT_OPAQUE;
    simple_set_release_info(&drawable->release_info, (intptr_t)update);
    drawable->type            = QXL_COPY_BITS;
    drawable->surfaces_dest[0] = -1;
    drawable->surfaces_dest[1] = -1;
    drawable->surfaces_dest[2] = -1;

    drawable->u.copy_bits.src_pos.x = 0;
    drawable->u.copy_bits.src_pos.y = 0;

    set_cmd(&update->ext, QXL_CMD_DRAW, (intptr_t)drawable);

    return update;
}

static SimpleSurfaceCmd *create_surface(int surface_id, int width, int height, uint8_t *data)
{
    SimpleSurfaceCmd *simple_cmd = calloc(sizeof(SimpleSurfaceCmd), 1);
    QXLSurfaceCmd *surface_cmd = &simple_cmd->surface_cmd;

    set_cmd(&simple_cmd->ext, QXL_CMD_SURFACE, (intptr_t)surface_cmd);
    simple_set_release_info(&surface_cmd->release_info, (intptr_t)simple_cmd);
    surface_cmd->type = QXL_SURFACE_CMD_CREATE;
    surface_cmd->flags = 0; // ?
    surface_cmd->surface_id = surface_id;
    surface_cmd->u.surface_create.format = SPICE_SURFACE_FMT_32_xRGB;
    surface_cmd->u.surface_create.width = width;
    surface_cmd->u.surface_create.height = height;
    surface_cmd->u.surface_create.stride = -width * 4;
    surface_cmd->u.surface_create.data = (intptr_t)data;
    return simple_cmd;
}

static SimpleSurfaceCmd *destroy_surface(int surface_id)
{
    SimpleSurfaceCmd *simple_cmd = calloc(sizeof(SimpleSurfaceCmd), 1);
    QXLSurfaceCmd *surface_cmd = &simple_cmd->surface_cmd;

    set_cmd(&simple_cmd->ext, QXL_CMD_SURFACE, (intptr_t)surface_cmd);
    simple_set_release_info(&surface_cmd->release_info, (intptr_t)simple_cmd);
    surface_cmd->type = QXL_SURFACE_CMD_DESTROY;
    surface_cmd->flags = 0; // ?
    surface_cmd->surface_id = surface_id;
    return simple_cmd;
}

static QXLWorker *qxl_worker = NULL;
static uint8_t primary_surface[HEIGHT * WIDTH * 4];

static void create_test_primary_surface(QXLWorker *worker)
{
    QXLDevSurfaceCreate surface = { 0, };

    surface.format     = SPICE_SURFACE_FMT_32_xRGB;
    surface.width      = WIDTH;
    surface.height     = HEIGHT;
    surface.stride     = -WIDTH * 4;
    surface.mouse_mode = TRUE;
    surface.flags      = 0;
    surface.type       = 0;
    surface.mem        = (intptr_t)&primary_surface;
    surface.group_id   = MEM_SLOT_GROUP_ID;

    qxl_worker->create_primary_surface(qxl_worker, 0, &surface);
}

QXLDevMemSlot slot = {
.slot_group_id = MEM_SLOT_GROUP_ID,
.slot_id = 0,
.generation = 0,
.virt_start = 0,
.virt_end = ~0,
.addr_delta = 0,
.qxl_ram_size = ~0,
};

static void attache_worker(QXLInstance *qin, QXLWorker *_qxl_worker)
{
    static int count = 0;
    if (++count > 1) {
        printf("%s ignored\n", __func__);
        return;
    }
    printf("%s\n", __func__);
    qxl_worker = _qxl_worker;
    qxl_worker->add_memslot(qxl_worker, &slot);
    create_test_primary_surface(qxl_worker);
    qxl_worker->start(qxl_worker);
}

static void set_compression_level(QXLInstance *qin, int level)
{
    printf("%s\n", __func__);
}

static void set_mm_time(QXLInstance *qin, uint32_t mm_time)
{
}

// we now have a secondary surface
#define MAX_SURFACE_NUM 2

static void get_init_info(QXLInstance *qin, QXLDevInitInfo *info)
{
    memset(info, 0, sizeof(*info));
    info->num_memslots = 1;
    info->num_memslots_groups = 1;
    info->memslot_id_bits = 1;
    info->memslot_gen_bits = 1;
    info->n_surfaces = MAX_SURFACE_NUM;
}

#define NOTIFY_DISPLAY_BATCH (SINGLE_PART/2)
#define NOTIFY_CURSOR_BATCH 10

int cursor_notify = NOTIFY_CURSOR_BATCH;

#define SURF_WIDTH 320
#define SURF_HEIGHT 240
uint8_t secondary_surface[SURF_WIDTH * SURF_HEIGHT * 4];
int has_secondary;

// We shall now have a ring of commands, so that we can update
// it from a separate thread - since get_command is called from
// the worker thread, and we need to sometimes do an update_area,
// which cannot be done from red_worker context (not via dispatcher,
// since you get a deadlock, and it isn't designed to be done
// any other way, so no point testing that).
int commands_end = 0;
int commands_start = 0;
struct QXLCommandExt* commands[1024];

#define COMMANDS_SIZE COUNT(commands)

static void push_command(QXLCommandExt *ext)
{
    ASSERT(commands_end - commands_start < COMMANDS_SIZE);
    commands[commands_end%COMMANDS_SIZE] = ext;
    commands_end++;
}

static struct QXLCommandExt *get_simple_command(void)
{
    struct QXLCommandExt *ret = commands[commands_start%COMMANDS_SIZE];
    ASSERT(commands_start < commands_end);
    commands_start++;
    return ret;
}

static int num_commands(void)
{
    return commands_end - commands_start;
}

// called from spice_server thread (i.e. red_worker thread)
static int get_command(QXLInstance *qin, struct QXLCommandExt *ext)
{
    if (num_commands() == 0) {
        return FALSE;
    }
    *ext = *get_simple_command();
    return TRUE;
}

static int *simple_commands = NULL;
static int num_simple_commands = 0;

static void produce_command(void)
{
    static int target_surface = 0;
    static int cmd_index = 0;


    if (has_secondary)
        target_surface = 1;

    ASSERT(num_simple_commands);

    switch (simple_commands[cmd_index]) {
        case PATH_PROGRESS:
            path_progress(&path);
            break;
        case SIMPLE_UPDATE: {
            QXLRect rect = {.left = 0, .right = SURF_WIDTH,
                            .top = 0, .bottom = SURF_HEIGHT};
            qxl_worker->update_area(qxl_worker, target_surface, &rect, NULL, 0, 1);
            break;
        }

        case SIMPLE_COPY_BITS:
        case SIMPLE_DRAW: {
            SimpleSpiceUpdate *update;

            if (has_automated_tests)
            {
                if (control == 0) {
                     return;
                }

                regression_test();
            }

            switch (simple_commands[cmd_index]) {
                case SIMPLE_COPY_BITS:
                    update = test_spice_create_update_copy_bits(0);
                    break;
                case SIMPLE_DRAW:
                    update = test_spice_create_update_draw(0, path.t);
                    break;
            }
            push_command(&update->ext);
            break;
        }

        case SIMPLE_CREATE_SURFACE: {
            SimpleSurfaceCmd *update;
            target_surface = MAX_SURFACE_NUM - 1;
            update = create_surface(target_surface, SURF_WIDTH, SURF_HEIGHT,
                                    secondary_surface);
            push_command(&update->ext);
            has_secondary = 1;
            break;
        }

        case SIMPLE_DESTROY_SURFACE: {
            SimpleSurfaceCmd *update;
            has_secondary = 0;
            update = destroy_surface(target_surface);
            target_surface = 0;
            push_command(&update->ext);
            break;
        }
    }
    cmd_index = (cmd_index + 1) % num_simple_commands;
}

SpiceTimer *wakeup_timer;
int wakeup_ms = 50;
SpiceCoreInterface *g_core;

static int req_cmd_notification(QXLInstance *qin)
{
    g_core->timer_start(wakeup_timer, wakeup_ms);
    return TRUE;
}

static void do_wakeup(void *opaque)
{
    int notify;
    cursor_notify = NOTIFY_CURSOR_BATCH;

    for (notify = NOTIFY_DISPLAY_BATCH; notify > 0;--notify) {
        produce_command();
    }

    g_core->timer_start(wakeup_timer, wakeup_ms);
    qxl_worker->wakeup(qxl_worker);
}

static void release_resource(QXLInstance *qin, struct QXLReleaseInfoExt release_info)
{
    QXLCommandExt *ext = (unsigned long)release_info.info->id;
    //printf("%s\n", __func__);
    ASSERT(release_info.group_id == MEM_SLOT_GROUP_ID);
    switch (ext->cmd.type) {
        case QXL_CMD_DRAW:
            test_spice_destroy_update((void*)ext);
            break;
        case QXL_CMD_SURFACE:
            free(ext);
            break;
        case QXL_CMD_CURSOR: {
            QXLCursorCmd *cmd = (unsigned long)ext->cmd.data;
            if (cmd->type == QXL_CURSOR_SET) {
                free(cmd);
            }
            free(ext);
            break;
        }
        default:
            abort();
    }
}

#define CURSOR_WIDTH 32
#define CURSOR_HEIGHT 32

static struct {
    QXLCursor cursor;
    uint8_t data[CURSOR_WIDTH * CURSOR_HEIGHT * 4]; // 32bit per pixel
} cursor;

static void cursor_init()
{
    cursor.cursor.header.unique = 0;
    cursor.cursor.header.type = SPICE_CURSOR_TYPE_COLOR32;
    cursor.cursor.header.width = CURSOR_WIDTH;
    cursor.cursor.header.height = CURSOR_HEIGHT;
    cursor.cursor.header.hot_spot_x = 0;
    cursor.cursor.header.hot_spot_y = 0;
    cursor.cursor.data_size = CURSOR_WIDTH * CURSOR_HEIGHT * 4;

    // X drivers addes it to the cursor size because it could be
    // cursor data information or another cursor related stuffs.
    // Otherwise, the code will break in client/cursor.cpp side,
    // that expect the data_size plus cursor information.
    // Blame cursor protocol for this. :-)
    cursor.cursor.data_size += 128;
    cursor.cursor.chunk.data_size = cursor.cursor.data_size;
    cursor.cursor.chunk.prev_chunk = cursor.cursor.chunk.next_chunk = 0;
}

static int get_cursor_command(QXLInstance *qin, struct QXLCommandExt *ext)
{
    static int color = 0;
    static int set = 1;
    static int x = 0, y = 0;
    QXLCursorCmd *cursor_cmd;
    QXLCommandExt *cmd;

    if (!cursor_notify) {
        return FALSE;
    }

    cursor_notify--;
    cmd = calloc(sizeof(QXLCommandExt), 1);
    cursor_cmd = calloc(sizeof(QXLCursorCmd), 1);

    cursor_cmd->release_info.id = (unsigned long)cmd;

    if (set) {
        cursor_cmd->type = QXL_CURSOR_SET;
        cursor_cmd->u.set.position.x = 0;
        cursor_cmd->u.set.position.y = 0;
        cursor_cmd->u.set.visible = TRUE;
        cursor_cmd->u.set.shape = (unsigned long)&cursor;
        // Only a white rect (32x32) as cursor
        memset(cursor.data, 255, sizeof(cursor.data));
        set = 0;
    } else {
        cursor_cmd->type = QXL_CURSOR_MOVE;
        cursor_cmd->u.position.x = x++ % WIDTH;
        cursor_cmd->u.position.y = y++ % HEIGHT;
    }

    cmd->cmd.data = (unsigned long)cursor_cmd;
    cmd->cmd.type = QXL_CMD_CURSOR;
    cmd->group_id = MEM_SLOT_GROUP_ID;
    cmd->flags    = 0;
    *ext = *cmd;
    //printf("%s\n", __func__);
    return TRUE;
}

static int req_cursor_notification(QXLInstance *qin)
{
    printf("%s\n", __func__);
    return TRUE;
}

static void notify_update(QXLInstance *qin, uint32_t update_id)
{
    printf("%s\n", __func__);
}

static int flush_resources(QXLInstance *qin)
{
    printf("%s\n", __func__);
    return TRUE;
}

QXLInterface display_sif = {
    .base = {
        .type = SPICE_INTERFACE_QXL,
        .description = "test",
        .major_version = SPICE_INTERFACE_QXL_MAJOR,
        .minor_version = SPICE_INTERFACE_QXL_MINOR
    },
    .attache_worker = attache_worker,
    .set_compression_level = set_compression_level,
    .set_mm_time = set_mm_time,
    .get_init_info = get_init_info,

    /* the callbacks below are called from spice server thread context */
    .get_command = get_command,
    .req_cmd_notification = req_cmd_notification,
    .release_resource = release_resource,
    .get_cursor_command = get_cursor_command,
    .req_cursor_notification = req_cursor_notification,
    .notify_update = notify_update,
    .flush_resources = flush_resources,
};

QXLInstance display_sin = {
    .base = {
        .sif = &display_sif.base,
    },
    .id = 0,
};

/* interface for tests */
void test_add_display_interface(SpiceServer *server)
{
    spice_server_add_interface(server, &display_sin.base);
}

void test_set_simple_command_list(int* commands, int num_commands)
{
    simple_commands = commands;
    num_simple_commands = num_commands;
}

SpiceServer* test_init(SpiceCoreInterface *core)
{
    int port = 5912;
    SpiceServer* server = spice_server_new();
    g_core = core;

    // some common initialization for all display tests
    printf("TESTER: listening on port %d (unsecure)\n", port);
    spice_server_set_port(server, port);
    spice_server_set_noauth(server);
    spice_server_init(server, core);

    cursor_init();
    path_init(&path, 0, angle_parts);
    memset(primary_surface, 0, sizeof(primary_surface));
    memset(secondary_surface, 0, sizeof(secondary_surface));
    has_secondary = 0;
    wakeup_timer = core->timer_add(do_wakeup, NULL);
    return server;
}

void init_automated()
{
    struct sigaction sa;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = &sigchld_handler;
    sigaction(SIGCHLD, &sa, NULL);
}

void spice_test_config_parse_args(int argc, char **argv)
{
    struct option options[] = {
#ifdef AUTOMATED_TESTS
        {"automated-tests", no_argument, &has_automated_tests, 1},
#endif
        {NULL, 0, NULL, 0},
    };
    int option_index;
    int val;

    while ((val = getopt_long(argc, argv, "", options, &option_index)) != -1) {
        switch (val) {
        case '?':
            printf("unrecognized option %s", argv[optind]);
            goto invalid_option;
        case 0:
            break;
        }
    }

    if (has_automated_tests) {
        init_automated();
    }
    return;

invalid_option:
    printf("Invalid option!\n"
           "usage: %s [--automated-tests]\n", argv[0]);
    exit(0);
}
