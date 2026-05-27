/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file bflib_network_stub.c
 *     No-op stubs for all networking functionality.
 * @par Purpose:
 *     Compiled instead of the real networking files when KEEPERFX_NETWORKING=OFF.
 *     Allows the game to run in single-player-only mode without any network
 *     library dependencies (SDL_net, enet, miniupnpc, libnatpmp).
 */
/******************************************************************************/
#include "pre_inc.h"

#include <stdint.h>
#include <stddef.h>
#include "bflib_basics.h"
#include "bflib_netsession.h"
#include "bflib_enet.h"
#include "bflib_coroutine.h"
#include "net_main.h"
#include "net_lobby.h"
#include "net_exchange_common.h"
#include "net_exchange_gameplay.h"
#include "net_lan.h"
#include "net_matchmaking.h"
#include "net_resync.h"
#include "net_game.h"
#include "net_input_lag.h"
#include "net_checksums.h"
#include "front_network.h"
#include "packets.h"
#include "player_data.h"

#include "post_inc.h"

/* net_game.c — excluded when KEEPERFX_NETWORKING=OFF; stub only what front_network.c does NOT define */
struct TbNetworkPlayerInfo net_player_info[MAX_NET_USERS];

TbBool network_player_active(int plyr_idx) { (void)plyr_idx; return 0; }
const char *network_player_name(int plyr_idx) { (void)plyr_idx; return ""; }
void sync_various_data(void) {}
void sync_initial_network_seed(void) {}
unsigned long get_host_player_id(void) { return 0; }
short setup_network_service(enum FrontendNetService service) { (void)service; return 0; }
int   setup_old_network_service(void) { return 0; }
TbBool init_players_network_game(void) { return 0; }
void  setup_count_players(void) {}
long  network_session_join(void) { return 0; }
void  process_quit_packet(struct PlayerInfo *player, short complete_quit) { (void)player; (void)complete_quit; }
void  process_disconnected_network_players(void) {}

/* net_main.c / net_lobby.c — LbNetwork API stubs */
void    LbNetwork_SetServerPort(int port) { (void)port; }
void    LbNetwork_InitSessionsFromCmdLine(const char *str) { (void)str; }
TbError LbNetwork_Init(uint32_t srvcindex, uint32_t maxplayrs, struct TbNetworkPlayerInfo *locplayr, struct ServiceInitData *init_data) { (void)srvcindex; (void)maxplayrs; (void)locplayr; (void)init_data; return Lb_FAIL; }
TbError LbNetwork_Join(struct TbNetworkSessionNameEntry *nsname, char *playr_name, int32_t *playr_num, void *optns) { (void)nsname; (void)playr_name; (void)playr_num; (void)optns; return Lb_FAIL; }
TbError LbNetwork_Create(char *nsname_str, char *plyr_name, uint32_t *plyr_num, void *optns) { (void)nsname_str; (void)plyr_name; (void)plyr_num; (void)optns; return Lb_FAIL; }
TbError LbNetwork_EnableNewPlayers(TbBool allow) { (void)allow; return Lb_FAIL; }
TbError LbNetwork_EnumeratePlayers(struct TbNetworkSessionNameEntry *sesn, TbNetworkCallbackFunc callback, void *user_data) { (void)sesn; (void)callback; (void)user_data; return Lb_FAIL; }
TbError LbNetwork_EnumerateSessions(TbNetworkCallbackFunc callback, void *ptr) { (void)callback; (void)ptr; return Lb_FAIL; }
TbError LbNetwork_Stop(void) { return Lb_FAIL; }
void    LbNetwork_UpdateInputLagIfHost(void) {}

/* net_lobby.c / net_exchange_gameplay.c — packet exchange stubs */
TbError LbNetwork_ExchangeLogin(char *plyr_name) { (void)plyr_name; return Lb_FAIL; }
TbError LbNetwork_ExchangeFrontend(void *send_buf, void *server_buf, size_t frame_size) { (void)send_buf; (void)server_buf; (void)frame_size; return Lb_FAIL; }
TbError LbNetwork_ExchangeGameplay(void *send_buf, void *server_buf, size_t frame_size) { (void)send_buf; (void)server_buf; (void)frame_size; return Lb_FAIL; }
void    LbNetwork_BroadcastUnpause(void) {}

/* net_main.c — netstate global */
struct NetState netstate;

/* net_lan.c — LAN session discovery stubs */
struct TbNetworkSessionNameEntry lan_sessions[LAN_SESSIONS_MAX];
int lan_session_count = 0;
void lan_refresh_sessions(void) {}
void lan_host_update(void) {}

/* net_matchmaking.c — matchmaking stubs */
struct TbNetworkSessionNameEntry matchmaking_sessions[MATCHMAKING_SESSIONS_MAX];
int matchmaking_session_count = 0;
char join_lobby_id[MATCHMAKING_ID_MAX] = { 0 };
void matchmaking_refresh_sessions(void) {}
void matchmaking_connect_async(void) {}

/* net_exchange_common.c — chat and sync stubs */
void send_network_chat_message(int player_id, const char *message) { (void)player_id; (void)message; }
struct PlayerInfo *prepare_network_chat_message(int player_id, const char *message) { (void)player_id; (void)message; return NULL; }
void wait_for_all_players(void) {}

/* net_exchange_gameplay.c — packet history stubs */
void initialize_packet_history(void) {}
void store_packet_history(PlayerNumber player, const struct Packet *packet) { (void)player; (void)packet; }
const struct Packet *get_history_packet(PlayerNumber player, GameTurn turn) { (void)player; (void)turn; return NULL; }
void process_gameplay_chat_message(int player_id, const char *message) { (void)player_id; (void)message; }

/* net_resync.cpp — called from main_game.c */
TbBool detailed_multiplayer_logging = 0;
TbBool LbNetwork_Resync(void *data_buffer, size_t buffer_length) { (void)data_buffer; (void)buffer_length; return 0; }
void LbNetwork_TimesyncBarrier(void) {}
void animate_resync_progress_bar(int current_phase, int total_phases) { (void)current_phase; (void)total_phases; }
void resync_game(void) {}

/* net_input_lag.c stubs — no networking means no input lag; return local packet directly */
void clear_input_lag_queue(void) {}
struct Packet* get_local_input_lag_packet_for_turn(GameTurn target_turn) { (void)target_turn; return get_packet_direct(my_player_number); }
void  store_local_packet_in_input_lag_queue(PlayerNumber my_packet_num) { (void)my_packet_num; }
TbBool input_lag_skips_initial_processing(void) { return 0; }
unsigned short calculate_skip_input(void) { return 0; }

/* net_checksums.c stubs */
void  update_turn_checksums(void) {}
short checksums_different(void) { return 0; }

/* bflib_enet.cpp — network stats (enet excluded, stubs return zero) */
unsigned long GetPing(int id) { (void)id; return 0; }
unsigned int  GetPacketLoss(int id) { (void)id; return 0; }
unsigned int  GetClientDataInTransit(void) { return 0; }
unsigned int  GetClientPacketsLost(void) { return 0; }
unsigned int  GetUploadRateBytesPerSecond(void) { return 0; }
unsigned int  GetDownloadRateBytesPerSecond(void) { return 0; }
void          enet_matchmaking_host_update(void) {}
uint16_t      enet_get_bound_ipv6_port(void) { return 0; }
