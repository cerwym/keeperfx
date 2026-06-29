#include "kfx_memory.h"
#include "pre_inc.h"

/* Native TCP socket layer — replaces SDL_net (not in vcpkg SDL3).           */
/* Winsock on Windows, BSD sockets on Linux/macOS.                           */
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN 1
#  include <winsock2.h>
#  include <ws2tcpip.h>
   typedef SOCKET kfx_socket_t;
#  define KFX_INVALID_SOCKET INVALID_SOCKET
#  define kfx_closesocket(s) closesocket(s)
#  define kfx_socket_error() WSAGetLastError()
#else
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <errno.h>
   typedef int kfx_socket_t;
#  define KFX_INVALID_SOCKET (-1)
#  define kfx_closesocket(s) close(s)
#  define kfx_socket_error() errno
#  define SOCKET_ERROR (-1)
#endif

#include <string.h>
#include <stdio.h>

#include "api.h"
#include <json.h>
#include <json-dom.h>
#include "config_keeperfx.h"
#include "config_campaigns.h"
#include "lvl_script.h"
#include "lvl_script_commands.h"
#include "lvl_script_lib.h"
#include "lvl_script_value.h"
#include "dungeon_data.h"
#include "player_data.h"
#include "player_instances.h"
#include "game_legacy.h"
#include "console_cmd.h"
#include "post_inc.h"
#include "value_util.h"
#include "keeperfx.hpp"

#define API_SERVER_BUFFER 4096

#define API_SUBSCRIBE_LIST_SIZE 256

#define API_SUBSCRIBE_INACTIVE 0
#define API_SUBSCRIBE_EVENT 1
#define API_SUBSCRIBE_VAR 2

/**
 * Structure to hold API global variables.
 */
struct ApiGlobals
{
    kfx_socket_t serverSocket;
    kfx_socket_t activeSocket;
} api;

static void api_globals_init(void)
{
    api.serverSocket = KFX_INVALID_SOCKET;
    api.activeSocket = KFX_INVALID_SOCKET;
}

/**
 * Structure representing a subscribed variable.
 */
struct SubscribedVariable
{
    PlayerNumber player_id;
    char name[COMMAND_WORD_LEN];
    unsigned char type;
    unsigned char id;
    long val;
};

/**
 * Structure representing a subscription slot.
 */
struct Subscription
{
    struct SubscribedVariable var;
    char event[COMMAND_WORD_LEN];
    int type;
} api_subscriptions[API_SUBSCRIBE_LIST_SIZE];

int api_sub_count = 0;

/**
 * Structure to hold the state of a dump buffer.
 */
struct dump_buf_state
{
    char *out;
    int out_space;
};

/**
 * Callback function for writing JSON value dump.
 */
static int json_value_dump_writer(const char *str, size_t size, void *dump_buffer_state)
{
    if (size > (size_t)((struct dump_buf_state *)dump_buffer_state)->out_space)
    {
        JUSTLOG("buffer too small");
        return JSON_ERR_OUTOFMEMORY;
    }

    memcpy(((struct dump_buf_state *)dump_buffer_state)->out, str, size);
    ((struct dump_buf_state *)dump_buffer_state)->out += size;
    ((struct dump_buf_state *)dump_buffer_state)->out_space -= (int)size;

    return 0;
}

/**
 * Send raw bytes over the active socket.
 */
static void api_send(const char *data, int len)
{
    if (api.activeSocket == KFX_INVALID_SOCKET || len <= 0)
        return;
    int sent = 0;
    while (sent < len)
    {
        int r = (int)send(api.activeSocket, data + sent, len - sent, 0);
        if (r <= 0)
            break;
        sent += r;
    }
}

/**
 * Function to get the number of max available KeeperFX flags with a name.
 */
size_t get_max_flags()
{
    size_t num = 0;
    while (flag_desc[num].name != NULL)
    {
        num++;
    }
    return num;
}

/**
 * Initialize the TCP API server.
 */
int api_init_server()
{
    if (api.serverSocket != KFX_INVALID_SOCKET)
        return 0;

    if (api_enabled != true)
        return 0;

    JUSTLOG("API server starting on port: %u", api_port);

#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
    {
        JUSTLOG("WSAStartup failed: %d", kfx_socket_error());
        return 1;
    }
#endif

    api_globals_init();

    for (int i = 0; i < API_SUBSCRIBE_LIST_SIZE; ++i)
        api_subscriptions[i].type = API_SUBSCRIBE_INACTIVE;

    kfx_socket_t srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (srv == KFX_INVALID_SOCKET)
    {
        JUSTLOG("socket() failed: %d", kfx_socket_error());
        api_close_server();
        return 1;
    }

    // Allow quick restart after close
    int reuse = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    // Non-blocking server socket so accept() doesn't stall the game loop
#ifdef _WIN32
    u_long nb = 1;
    ioctlsocket(srv, FIONBIO, &nb);
#else
    {
        int flags = fcntl(srv, F_GETFL, 0);
        fcntl(srv, F_SETFL, flags | O_NONBLOCK);
    }
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // localhost only
    addr.sin_port = htons((unsigned short)api_port);

    if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        JUSTLOG("bind() failed: %d", kfx_socket_error());
        kfx_closesocket(srv);
        api_close_server();
        return 1;
    }

    if (listen(srv, 1) == SOCKET_ERROR)
    {
        JUSTLOG("listen() failed: %d", kfx_socket_error());
        kfx_closesocket(srv);
        api_close_server();
        return 1;
    }

    api.serverSocket = srv;
    JUSTLOG("API server active");
    JUSTLOG("Allocated %d API subscription slots", API_SUBSCRIBE_LIST_SIZE);
    return 0;
}

/**
 * Send an API error message.
 */
static void api_err(const char *err, VALUE *ack_id)
{
    if (api.activeSocket == KFX_INVALID_SOCKET)
        return;

    VALUE json_root_real;
    VALUE *json_root = &json_root_real;
    value_init_dict(json_root);

    if (ack_id != NULL)
    {
        VALUE *val_ack = value_dict_add(json_root, "ack");
        *val_ack = *ack_id;
    }

    VALUE *val_success = value_dict_add(json_root, "success");
    value_init_bool(val_success, false);

    VALUE *val_err = value_dict_add(json_root, "error");
    value_init_string(val_err, (char *)err);

    char json_string[1024];
    struct dump_buf_state dump_state = {json_string, sizeof(json_string) - 1};
    int json_dump_return_value = json_dom_dump(json_root, json_value_dump_writer, &dump_state, 0, JSON_DOM_DUMP_MINIMIZE);

    *dump_state.out = 0;
    if (json_dump_return_value != 0)
    {
        value_fini(json_root);
        return;
    }

    dump_state.out[0] = '\n';
    dump_state.out++;

    api_send(json_string, (int)(dump_state.out - json_string));
    value_fini(json_root);
}

/**
 * Send an API success message.
 */
static void api_ok(VALUE *ack_id)
{
    if (api.activeSocket == KFX_INVALID_SOCKET)
        return;

    if (ack_id == NULL)
    {
        const char msg[] = "{\"success\":true}\n";
        api_send(msg, (int)(sizeof(msg) - 1));
        return;
    }

    VALUE json_root_real;
    VALUE *json_root = &json_root_real;
    value_init_dict(json_root);

    VALUE *val_ack = value_dict_add(json_root, "ack");
    *val_ack = *ack_id;

    VALUE *val_success = value_dict_add(json_root, "success");
    value_init_bool(val_success, true);

    char json_string[1024];
    struct dump_buf_state dump_state = {json_string, sizeof(json_string) - 1};
    int json_dump_return_value = json_dom_dump(json_root, json_value_dump_writer, &dump_state, 0, JSON_DOM_DUMP_MINIMIZE);

    *dump_state.out = 0;
    if (json_dump_return_value != 0)
    {
        value_fini(json_root);
        return;
    }

    dump_state.out[0] = '\n';
    dump_state.out++;

    api_send(json_string, (int)(dump_state.out - json_string));
    value_fini(json_root);
}

/**
 * Return data to the API client.
 */
static void api_return_data(TbBool success, VALUE value, VALUE *ack_id)
{
    if (api.activeSocket == KFX_INVALID_SOCKET)
        return;

    VALUE json_root_real;
    VALUE *json_root = &json_root_real;
    value_init_dict(json_root);

    if (ack_id != NULL)
    {
        VALUE *val_ack = value_dict_add(json_root, "ack");
        *val_ack = *ack_id;
    }

    VALUE *val_success = value_dict_add(json_root, "success");
    value_init_bool(val_success, success);

    VALUE *val_data = value_dict_add(json_root, "data");
    *val_data = value;

    char json_string[1024];
    struct dump_buf_state dump_state = {json_string, sizeof(json_string) - 1};
    int json_dump_return_value = json_dom_dump(json_root, json_value_dump_writer, &dump_state, 0, JSON_DOM_DUMP_MINIMIZE);

    *dump_state.out = 0;
    if (json_dump_return_value != 0)
    {
        api_err("FAILED_TO_CREATE_JSON", ack_id);
        value_fini(json_root);
        return;
    }

    dump_state.out[0] = '\n';
    dump_state.out++;

    api_send(json_string, (int)(dump_state.out - json_string));
    value_fini(json_root);
}

void api_return_var_update(PlayerNumber plyr_idx, const char *var_name, long value)
{
    if (api.activeSocket == KFX_INVALID_SOCKET)
        return;

    VALUE json_root_real;
    VALUE *json_root = &json_root_real;
    value_init_dict(json_root);

    VALUE *val_event = value_dict_add(json_root, "event");
    value_init_string(val_event, "VAR_UPDATE");

    VALUE *val_var = value_dict_add(json_root, "var");
    value_init_dict(val_var);

    VALUE *val_var_player = value_dict_add(val_var, "player");
    value_init_string(val_var_player, player_code_name(plyr_idx));

    VALUE *val_var_name = value_dict_add(val_var, "name");
    value_init_string(val_var_name, var_name);

    VALUE *val_var_new_val = value_dict_add(val_var, "value");
    value_init_int32(val_var_new_val, value);

    char json_string[1024];
    struct dump_buf_state dump_state = {json_string, sizeof(json_string) - 1};
    int json_dump_return_value = json_dom_dump(json_root, json_value_dump_writer, &dump_state, 0, JSON_DOM_DUMP_MINIMIZE);

    *dump_state.out = 0;
    if (json_dump_return_value != 0)
    {
        value_fini(json_root);
        return;
    }

    dump_state.out[0] = '\n';
    dump_state.out++;

    api_send(json_string, (int)(dump_state.out - json_string));
    value_fini(json_root);
}

/**
 * Send a long integer data response to the API client.
 */
static void api_return_data_number(long data, VALUE *ack_id)
{
    if (api.activeSocket == KFX_INVALID_SOCKET)
        return;

    if (ack_id == NULL)
    {
        char buf[256];
        int len = snprintf(buf, sizeof(buf) - 1, "{\"success\":true,\"data\":%ld}\n", data);
        api_send(buf, len);
        return;
    }

    VALUE json_root_real;
    VALUE *json_root = &json_root_real;
    value_init_dict(json_root);

    VALUE *val_ack = value_dict_add(json_root, "ack");
    *val_ack = *ack_id;

    VALUE *val_success = value_dict_add(json_root, "success");
    value_init_bool(val_success, true);

    VALUE *val_data = value_dict_add(json_root, "data");
    value_init_int32(val_data, data);

    char json_string[1024];
    struct dump_buf_state dump_state = {json_string, sizeof(json_string) - 1};
    int json_dump_return_value = json_dom_dump(json_root, json_value_dump_writer, &dump_state, 0, JSON_DOM_DUMP_MINIMIZE);

    *dump_state.out = 0;
    if (json_dump_return_value != 0)
    {
        value_fini(json_root);
        return;
    }

    dump_state.out[0] = '\n';
    dump_state.out++;

    api_send(json_string, (int)(dump_state.out - json_string));
    value_fini(json_root);
}

void api_clear_all_subscriptions()
{
    if (api_sub_count == 0)
        return;

    for (int i = 0; i < API_SUBSCRIBE_LIST_SIZE; i++)
    {
        if (api_subscriptions[i].type == API_SUBSCRIBE_INACTIVE)
            continue;

        api_subscriptions[i].type = API_SUBSCRIBE_INACTIVE;
        memset(api_subscriptions[i].event, 0, sizeof(api_subscriptions[i].event));
        memset(&api_subscriptions[i].var, 0, sizeof(struct SubscribedVariable));
    }

    api_sub_count = 0;
}

int api_is_subscribed_to_event(const char *event_name)
{
    int api_sub_found_count = 0;
    for (int i = 0; i < API_SUBSCRIBE_LIST_SIZE; i++)
    {
        if (api_sub_count == api_sub_found_count)
            return false;

        if (api_subscriptions[i].type == API_SUBSCRIBE_INACTIVE)
            continue;

        api_sub_found_count++;

        if (api_subscriptions[i].type != API_SUBSCRIBE_EVENT)
            continue;

        if (strcmp(api_subscriptions[i].event, event_name) == 0)
            return true;
    }

    return false;
}

int api_subscribe_event(const char *event_name)
{
    if (api_is_subscribed_to_event(event_name) == true)
        return true;

    if (api_sub_count >= API_SUBSCRIBE_LIST_SIZE)
    {
        WARNLOG(
            "Tried to register API event '%s' but we are already at the limit of %d subscription slots",
            event_name,
            API_SUBSCRIBE_LIST_SIZE);
        return false;
    }

    for (int i = 0; i < API_SUBSCRIBE_LIST_SIZE; i++)
    {
        if (api_subscriptions[i].type == API_SUBSCRIBE_INACTIVE)
        {
            api_subscriptions[i].type = API_SUBSCRIBE_EVENT;
            strncpy(api_subscriptions[i].event, event_name, sizeof(api_subscriptions[i].event) - 1);
            api_sub_count++;
            return true;
        }
    }

    return false;
}

int api_unsubscribe_event(const char *event_name)
{
    if (api_is_subscribed_to_event(event_name) == false)
        return true;

    for (int i = 0; i < API_SUBSCRIBE_LIST_SIZE; i++)
    {
        if (api_subscriptions[i].type != API_SUBSCRIBE_EVENT)
            continue;

        if (strcmp(api_subscriptions[i].event, event_name) == 0)
        {
            api_subscriptions[i].type = API_SUBSCRIBE_INACTIVE;
            memset(api_subscriptions[i].event, 0, sizeof(api_subscriptions[i].event));
            api_sub_count--;
            return true;
        }
    }

    return false;
}

int api_is_subscribed_to_var(PlayerNumber plyr_idx, unsigned char valtype, short validx)
{
    int api_sub_found_count = 0;
    for (int i = 0; i < API_SUBSCRIBE_LIST_SIZE; i++)
    {
        if (api_sub_count == api_sub_found_count)
            return false;

        if (api_subscriptions[i].type == API_SUBSCRIBE_INACTIVE)
            continue;

        api_sub_found_count++;

        if (api_subscriptions[i].type != API_SUBSCRIBE_VAR)
            continue;

        if (api_subscriptions[i].var.player_id == plyr_idx &&
            api_subscriptions[i].var.type == valtype &&
            api_subscriptions[i].var.id == validx)
        {
            return true;
        }
    }

    return false;
}

int api_subscribe_var(PlayerNumber plyr_idx, const char *var_name, unsigned char valtype, short validx)
{
    JUSTLOG("Sub: %d, %d, %d", plyr_idx, valtype, validx);

    if (api_is_subscribed_to_var(plyr_idx, valtype, validx) == true)
        return true;

    if (api_sub_count >= API_SUBSCRIBE_LIST_SIZE)
    {
        WARNLOG("Tried to register to update of var but we are already at the limit of %d subscription slots", API_SUBSCRIBE_LIST_SIZE);
        return false;
    }

    for (int i = 0; i < API_SUBSCRIBE_LIST_SIZE; i++)
    {
        if (api_subscriptions[i].type == API_SUBSCRIBE_INACTIVE)
        {
            struct SubscribedVariable sub_var;
            sub_var.player_id = plyr_idx;
            sub_var.type = valtype;
            sub_var.id = validx;
            sub_var.val = get_condition_value(plyr_idx, valtype, validx);
            strncpy(sub_var.name, var_name, sizeof(sub_var.name) - 1);

            api_subscriptions[i].type = API_SUBSCRIBE_VAR;
            api_subscriptions[i].var = sub_var;

            api_sub_count++;
            return true;
        }
    }

    return false;
}

int api_unsubscribe_var(PlayerNumber plyr_idx, unsigned char valtype, short validx)
{
    if (api_is_subscribed_to_var(plyr_idx, valtype, validx) == false)
        return true;

    for (int i = 0; i < API_SUBSCRIBE_LIST_SIZE; i++)
    {
        if (api_subscriptions[i].type != API_SUBSCRIBE_VAR)
            continue;

        if (api_subscriptions[i].var.player_id == plyr_idx &&
            api_subscriptions[i].var.type == valtype &&
            api_subscriptions[i].var.id == validx)
        {
            api_subscriptions[i].type = API_SUBSCRIBE_INACTIVE;
            memset(&api_subscriptions[i].var, 0, sizeof(struct SubscribedVariable));
            api_sub_count--;
            return true;
        }
    }

    return false;
}

void api_check_var_update()
{
    if (api.activeSocket == KFX_INVALID_SOCKET)
        return;

    int api_sub_found_count = 0;
    for (int i = 0; i < API_SUBSCRIBE_LIST_SIZE; i++)
    {
        if (api_sub_count == api_sub_found_count)
            return;

        if (api_subscriptions[i].type == API_SUBSCRIBE_INACTIVE)
            continue;

        api_sub_found_count++;

        if (api_subscriptions[i].type != API_SUBSCRIBE_VAR)
            continue;

        long variable_value = get_condition_value(
            api_subscriptions[i].var.player_id,
            api_subscriptions[i].var.type,
            api_subscriptions[i].var.id);

        if (api_subscriptions[i].var.val != variable_value)
        {
            api_subscriptions[i].var.val = variable_value;

            api_return_var_update(
                api_subscriptions[i].var.player_id,
                api_subscriptions[i].var.name,
                api_subscriptions[i].var.val);
        }
    }
}

/**
 * Send an API event message.
 */
void api_event(const char *event_name)
{
    if (api.activeSocket == KFX_INVALID_SOCKET)
        return;

    if (api_is_subscribed_to_event(event_name) == false)
        return;

    char buf[512];
    int len = snprintf(buf, sizeof(buf) - 1, "{\"event\":\"%s\"}\n", event_name);
    api_send(buf, len);
}

/**
 * Process the incoming buffer from the API client.
 */
static void api_process_buffer(const char *buffer, size_t buf_size)
{
    VALUE *ack_id;
    VALUE json_data, *value = &json_data;

    if (buffer[buf_size - 1] == 0)
        buf_size -= 1;

    if (strlen(buffer) < 1)
    {
        api_err("NO_JSON", NULL);
        return;
    }

    int ret = json_dom_parse(buffer, buf_size, NULL, 0, value, NULL);
    if (ret != 0)
    {
        api_err("INVALID_JSON", NULL);
        return;
    }

    if (value_type(value) != VALUE_DICT)
    {
        api_err("INVALID_JSON_OBJECT", NULL);
        value_fini(&json_data);
        return;
    }

    ack_id = value_dict_get(value, "ack");

    const char *action = value_string(value_dict_get(value, "action"));
    if (action == NULL)
    {
        api_err("MISSING_ACTION", ack_id);
        value_fini(&json_data);
        return;
    }

    PlayerNumber player_id = my_player_number;
    VALUE *player = value_dict_get(value, "player");
    if (value_type(player) == VALUE_INT32)
    {
        player_id = (PlayerNumber)value_int32(player);
    }
    else if (value_type(player) == VALUE_STRING)
    {
        player_id = get_id(player_desc, (char *)value_string(player));
    }

    if (strcasecmp("get_kfx_info", action) == 0)
    {
        VALUE data_kfx_info_real;
        VALUE *data_kfx_info = &data_kfx_info_real;
        value_init_dict(data_kfx_info);

        value_init_string(value_dict_add(data_kfx_info, "kfx_version"), VER_STRING);

        api_return_data(true, data_kfx_info_real, ack_id);

        value_fini(&json_data);
        return;
    }

    if (strcasecmp("subscribe_var", action) == 0)
    {
        const char *variable_name = (char *)value_string(value_dict_get(value, "var"));
        if (variable_name == NULL || strlen(variable_name) < 1)
        {
            api_err("MISSING_VAR", ack_id);
            value_fini(&json_data);
            return;
        }

        int32_t variable_id, variable_type;
        if (parse_get_varib(variable_name, &variable_id, &variable_type,1) == false)
        {
            api_err("UNKNOWN_VAR", ack_id);
            value_fini(&json_data);
            return;
        }

        if (api_subscribe_var(player_id, variable_name, variable_type, variable_id))
            api_ok(ack_id);
        else
            api_err("SUB_FAILED", ack_id);

        value_fini(&json_data);
        return;
    }

    if (strcasecmp("unsubscribe_var", action) == 0)
    {
        char *variable_name = (char *)value_string(value_dict_get(value, "var"));
        if (variable_name == NULL || strlen(variable_name) < 1)
        {
            api_err("MISSING_VAR", ack_id);
            value_fini(&json_data);
            return;
        }

        int32_t variable_id, variable_type;
        if (parse_get_varib(variable_name, &variable_id, &variable_type,1) == false)
        {
            api_err("UNKNOWN_VAR", ack_id);
            value_fini(&json_data);
            return;
        }

        if (api_unsubscribe_var(player_id, variable_type, variable_id))
            api_ok(ack_id);
        else
            api_err("SUB_FAILED", ack_id);

        value_fini(&json_data);
        return;
    }

    if (strcasecmp("subscribe_event", action) == 0)
    {
        char *event_name = (char *)value_string(value_dict_get(value, "event"));
        if (event_name == NULL || strlen(event_name) < 1)
        {
            api_err("MISSING_EVENT", ack_id);
            value_fini(&json_data);
            return;
        }

        if (strlen(event_name) > COMMAND_WORD_LEN)
        {
            api_err("STRING_TOO_LONG", ack_id);
            value_fini(&json_data);
            return;
        }

        if (api_subscribe_event(event_name))
            api_ok(ack_id);
        else
            api_err("SUB_FAILED", ack_id);

        value_fini(&json_data);
        return;
    }

    if (strcasecmp("unsubscribe_event", action) == 0)
    {
        char *event_name = (char *)value_string(value_dict_get(value, "event"));
        if (event_name == NULL || strlen(event_name) < 1)
        {
            api_err("MISSING_EVENT", ack_id);
            value_fini(&json_data);
            return;
        }

        if (api_unsubscribe_event(event_name))
            api_ok(ack_id);
        else
            api_err("SUB_FAILED", ack_id);

        value_fini(&json_data);
        return;
    }

    if (strcasecmp("unsubscribe_all", action) == 0)
    {
        api_clear_all_subscriptions();
        api_ok(ack_id);

        value_fini(&json_data);
        return;
    }

    if (game.game_kind != GKind_LocalGame)
    {
        api_err("NOT_IN_LOCAL_GAME", ack_id);
        value_fini(&json_data);
        return;
    }

    if (strcasecmp("map_command", action) == 0)
    {
        if ((game.operation_flags & GOF_Paused) != 0)
        {
            api_err("GAME_IS_PAUSED", ack_id);
            value_fini(&json_data);
            return;
        }

        char *map_command = (char *)value_string(value_dict_get(value, "command"));
        if (map_command == NULL)
        {
            api_err("MISSING_COMMAND", ack_id);
            value_fini(&json_data);
            return;
        }

        if (script_scan_line(map_command, false, 99))
            api_ok(ack_id);
        else
            api_err("FAILED_TO_EXECUTE_MAP_COMMAND", ack_id);

        value_fini(&json_data);
        return;
    }

    if (strcasecmp("console_command", action) == 0)
    {
        if ((game.operation_flags & GOF_Paused) != 0)
        {
            api_err("GAME_IS_PAUSED", ack_id);
            value_fini(&json_data);
            return;
        }

        char *console_command = (char *)value_string(value_dict_get(value, "command"));
        if (console_command == NULL || strlen(console_command) < 1)
        {
            api_err("MISSING_COMMAND", ack_id);
            value_fini(&json_data);
            return;
        }

        if (console_command[0] == cmd_char)
            console_command += 1;

        if (cmd_exec(player_id, console_command))
            api_ok(ack_id);
        else
            api_err("FAILED_TO_EXECUTE_CONSOLE_COMMAND", ack_id);

        value_fini(&json_data);
        return;
    }

    if (strcasecmp("get_all_player_flags", action) == 0)
    {
        VALUE flag_data_real;
        VALUE *flag_data = &flag_data_real;
        value_init_dict(flag_data);

        for (int player_index = 0; player_index < ALL_PLAYERS; player_index++)
        {
            VALUE *player_info = value_dict_add(flag_data, player_code_name(player_index));
            value_init_dict(player_info);

            for (size_t flag_index = 0; flag_index < get_max_flags(); flag_index++)
            {
                long flag_value = get_condition_value(player_id, SVar_FLAG, flag_index);
                const char *flag_string = get_conf_parameter_text(flag_desc, flag_index);
                value_init_int32(value_dict_add(player_info, flag_string), flag_value);
            }
        }

        api_return_data(true, flag_data_real, ack_id);

        value_fini(&json_data);
        return;
    }

    if (strcasecmp("read_var", action) == 0)
    {
        char *variable_name = (char *)value_string(value_dict_get(value, "var"));
        if (variable_name == NULL || strlen(variable_name) < 1)
        {
            api_err("MISSING_VAR", ack_id);
            value_fini(&json_data);
            return;
        }

        int32_t variable_id, variable_type;
        if (parse_get_varib(variable_name, &variable_id, &variable_type,1) == false)
        {
            api_err("UNKNOWN_VAR", ack_id);
            value_fini(&json_data);
            return;
        }

        long variable_value = get_condition_value(player_id, variable_type, variable_id);
        api_return_data_number(variable_value, ack_id);

        value_fini(&json_data);
        return;
    }

    if (strcasecmp("set_var", action) == 0)
    {
        char *variable_name = (char *)value_string(value_dict_get(value, "var"));
        if (variable_name == NULL || strlen(variable_name) < 1)
        {
            api_err("MISSING_VAR", ack_id);
            value_fini(&json_data);
            return;
        }

        int32_t variable_id, variable_type;
        if (parse_get_varib(variable_name, &variable_id, &variable_type,1) == false)
        {
            api_err("UNKNOWN_VAR", ack_id);
            value_fini(&json_data);
            return;
        }

        if (
            variable_type != SVar_FLAG &&
            variable_type != SVar_CAMPAIGN_FLAG &&
            variable_type != SVar_BOX_ACTIVATED &&
            variable_type != SVar_TRAP_ACTIVATED &&
            variable_type != SVar_SACRIFICED &&
            variable_type != SVar_REWARDED)
        {
            api_err("UNABLE_TO_SET_VAR", ack_id);
            value_fini(&json_data);
            return;
        }

        VALUE *new_value = value_dict_get(value, "value");
        if (new_value == NULL || value_type(new_value) != VALUE_INT32)
        {
            api_err("VALUE_MUST_BE_INT", ack_id);
            value_fini(&json_data);
            return;
        }

        set_variable(player_id, variable_type, variable_id, value_int32(new_value));
        api_ok(ack_id);

        value_fini(&json_data);
        return;
    }

    if (strcasecmp("get_level_info", action) == 0 || strcasecmp("get_map_info", action) == 0)
    {
        const char *lv_name = NULL;
        LevelNumber lv_number = get_loaded_level_number();
        struct LevelInformation *lv_info = get_level_info(lv_number);
        if (lv_info != NULL)
        {
            if (lv_info->name_stridx > 0)
                lv_name = get_string(lv_info->name_stridx);
            else
                lv_name = lv_info->name;
        }
        else if (is_multiplayer_level(lv_number))
        {
            lv_name = (const char *)level_name;
        }

        VALUE data_level_info_real;
        VALUE *data_level_info = &data_level_info_real;
        value_init_dict(data_level_info);

        value_init_string(value_dict_add(data_level_info, "level_name"), lv_name);
        value_init_int32(value_dict_add(data_level_info, "level_number"), lv_number);
        value_init_int32(value_dict_add(data_level_info, "players"), lv_info->players);
        value_init_int32(value_dict_add(data_level_info, "mapsize_x"), lv_info->mapsize_x);
        value_init_int32(value_dict_add(data_level_info, "mapsize_y"), lv_info->mapsize_y);
        value_init_bool(value_dict_add(data_level_info, "is_multiplayer"), is_multiplayer_level(lv_number));

        VALUE *data_campaign_info = value_dict_add(data_level_info, "campaign");
        value_init_dict(data_campaign_info);

        value_init_string(value_dict_add(data_campaign_info, "campaign_name"), campaign.name);
        value_init_string(value_dict_add(data_campaign_info, "campaign_display_name"), campaign.display_name);
        value_init_string(value_dict_add(data_campaign_info, "campaign_fname"), campaign.fname);
        value_init_bool(value_dict_add(data_campaign_info, "is_map_pack"), is_map_pack());

        api_return_data(true, data_level_info_real, ack_id);

        value_fini(&json_data);
        return;
    }

    if (strcasecmp("get_current_game_info", action) == 0)
    {
        VALUE data_current_game_info_real;
        VALUE *data_current_game_info = &data_current_game_info_real;
        value_init_dict(data_current_game_info);

        value_init_int32(value_dict_add(data_current_game_info, "game_turn"), get_gameturn());

        api_return_data(true, data_current_game_info_real, ack_id);

        value_fini(&json_data);
        return;
    }

    api_err("UNKNOWN_ACTION", ack_id);
    value_fini(&json_data);
}

/**
 * Process concatenated JSON objects from buffer.
 */
void api_process_multipart_json(const char *buffer, int buf_size)
{
    int start = -1;
    int depth = 0;

    for (int i = 0; i < buf_size; ++i)
    {
        if (buffer[i] == '{')
        {
            if (depth == 0)
                start = i;
            depth++;
        }
        else if (buffer[i] == '}')
        {
            depth--;
            if (depth == 0 && start != -1)
            {
                int json_length = i - start + 1;
                char* json_string = (char*)KfxAlloc((json_length + 1) * sizeof(char));
                if (!json_string) return;
                strncpy(json_string, buffer + start, json_length);
                json_string[json_length] = '\0';

                JUSTLOG("Received message from client: %s", json_string);
                api_process_buffer(json_string, json_length);
                KfxFree(json_string);
                start = -1;
            }
        }
    }

    if (depth > 0)
        api_err("INVALID_JSON_IN_PACKET", NULL);
}

/**
 * Disconnect and clean up the active client socket.
 */
static void api_disconnect_client(void)
{
    if (api.activeSocket == KFX_INVALID_SOCKET)
        return;
    api_clear_all_subscriptions();
    kfx_closesocket(api.activeSocket);
    api.activeSocket = KFX_INVALID_SOCKET;
    JUSTLOG("API connection closed");
}

/**
 * Check the server socket for a new connection (non-blocking).
 */
static void api_accept_connection(void)
{
    struct sockaddr_in client_addr;
#ifdef _WIN32
    int addr_len = sizeof(client_addr);
#else
    socklen_t addr_len = sizeof(client_addr);
#endif
    kfx_socket_t client = accept(api.serverSocket, (struct sockaddr*)&client_addr, &addr_len);
    if (client == KFX_INVALID_SOCKET)
        return; // non-blocking: EWOULDBLOCK / WSAEWOULDBLOCK is normal

    if (api.activeSocket != KFX_INVALID_SOCKET)
    {
        // Already have a client — reject the second one
        kfx_closesocket(client);
        WARNLOG("Got another connection while API connection is still active");
        return;
    }

    // Make the new client socket non-blocking too
#ifdef _WIN32
    u_long nb = 1;
    ioctlsocket(client, FIONBIO, &nb);
#else
    {
        int flags = fcntl(client, F_GETFL, 0);
        fcntl(client, F_SETFL, flags | O_NONBLOCK);
    }
#endif

    api.activeSocket = client;
    JUSTLOG("Client connected");
}

/**
 * Update the API server and handle all pending packets.
 * Called once per game tick — uses non-blocking I/O, no select() stall.
 */
void api_update_server()
{
    if (api.serverSocket == KFX_INVALID_SOCKET)
        return;

    api_accept_connection();

    if (api.activeSocket == KFX_INVALID_SOCKET)
        return;

    char buffer[API_SERVER_BUFFER];
    memset(buffer, 0, API_SERVER_BUFFER);

    int received = (int)recv(api.activeSocket, buffer, API_SERVER_BUFFER - 1, 0);
    if (received > 0)
    {
        // Strip trailing newline for Telnet compatibility
        size_t blen = strlen(buffer);
        if (blen > 0 && buffer[blen - 1] == '\n')
            buffer[blen - 1] = '\0';

        api_process_multipart_json(buffer, (int)strlen(buffer));
    }
    else if (received == 0)
    {
        // Graceful disconnect
        api_disconnect_client();
    }
    else
    {
#ifdef _WIN32
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK)
            api_disconnect_client();
#else
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            api_disconnect_client();
#endif
    }

    api_check_var_update();
}

/**
 * Close the API server.
 */
void api_close_server()
{
    api_disconnect_client();

    JUSTLOG("API server closing");

    if (api.serverSocket != KFX_INVALID_SOCKET)
    {
        kfx_closesocket(api.serverSocket);
        api.serverSocket = KFX_INVALID_SOCKET;
    }

#ifdef _WIN32
    WSACleanup();
#endif
}
