/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief: ITTI message handlers for telnet server
 * This file contains callback functions for processing ITTI messages
 * received by the telnet server.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "common/openairinterface5g_limits.h"
#include "common/utils/ocp_itti/intertask_interface.h"
#include "common/utils/nr/nr_common.h"
#include "openair2/RRC/NR/nr_rrc_defs.h"
#include "openair2/RRC/NR/nr_rrc_messages_types.h"
#include "openair2/LAYER2/nr_rlc/nr_rlc_oai_api.h"
#include "openair2/LAYER2/NR_MAC_gNB/mac_config.h"

#include "telnetsrv.h"
#include "telnetsrv_itti_handlers.h"

extern void client_printf(const char *message, ...);
extern void client_printf_to_client(int client_idx, const char *message, ...);

extern bool telnetsrv_o1_running;
extern telnet_client_t telnet_clients[TELNET_MAX_CLIENTS];
extern telnetsrv_params_t telnetparams;

// Forward declaration for async printf
void websrv_async_printf(const char *message, ...);

static inline bool telnetsrv_client_idx_valid(int client_idx)
{
  return (client_idx >= 0 && client_idx < TELNET_MAX_CLIENTS && telnet_clients[client_idx].active);
}

static inline bool telnetsrv_client_idx_valid_or_web_active(int client_idx)
{
  if (client_idx == WEBSRV_CLIENT_IDX) {
    return true;
  }
  return telnetsrv_client_idx_valid(client_idx);
}

#define TELNET_CLIENT_IF_VALID(client_idx) if (telnetsrv_client_idx_valid_or_web_active(client_idx))

#define TELNET_PRINT_TO_CLIENT(client_idx, fmt, ...) \
  do { \
    if (client_idx == WEBSRV_CLIENT_IDX) { \
      websrv_async_printf(fmt, ##__VA_ARGS__); \
      break; \
    } \
    if ((client_idx) >= 0 && (client_idx) < TELNET_MAX_CLIENTS && \
        telnet_clients[(client_idx)].active) { \
      client_printf_to_client((client_idx), fmt, ##__VA_ARGS__); \
    } else { \
      client_printf(fmt, ##__VA_ARGS__); \
    } \
  } while (0)

void telnetsrv_handle_rrc_get_single_ue_rnti(Rrc_get_single_ue_rnti *rrc_msg)
{
  int client_idx = rrc_msg->client_idx;

  if (!telnetsrv_client_idx_valid_or_web_active(client_idx)) {
    LOG_D(ITTI, "Received RRC_GET_SINGLE_UE_RNTI for invalid client %d, ignoring\n", client_idx);
    return;
  }

  if (rrc_msg->has_rrc) {
    if (rrc_msg->is_single) {
      rnti_t rnti = rrc_msg->rnti;
      ue_id_t ue_id = rrc_msg->id;
      int reestab = rrc_msg->ue_reestablishment_counter;
      int reconfig = rrc_msg->ue_reconfiguration_counter;

      if (telnetsrv_client_idx_valid_or_web_active(client_idx)) {
        if (telnet_clients[client_idx].pending_du_by_ue_id) {
          MessageDef *msg_p = itti_alloc_new_message(TASK_TELNET, 0, RRC_GET_DU_ID_BY_UE_ID);
          msg_p->ittiMsg.rrc_get_du_id_by_ue_id.ue_id = ue_id;
          msg_p->ittiMsg.rrc_get_du_id_by_ue_id.client_idx = client_idx;
          itti_send_msg_to_task(TASK_RRC_GNB, 0, msg_p);
        } else if (telnet_clients[client_idx].pending_f1_ho) {
          MessageDef *msg_p = itti_alloc_new_message(TASK_TELNET, 0, RRC_NR_F1_HO_TRIGGER);
          msg_p->ittiMsg.rrc_trigger_f1_ho.id = ue_id;
          msg_p->ittiMsg.rrc_trigger_f1_ho.client_idx = client_idx;
          itti_send_msg_to_task(TASK_RRC_GNB, 0, msg_p);
          TELNET_PRINT_TO_CLIENT(client_idx, "RRC F1 handover triggered for UE %u\n",
                                 ue_id);
        } else if (telnet_clients[client_idx].pending_reestab) {
          nr_rlc_test_trigger_reestablishment(ue_id);
          if (rnti != ue_id) {
            nr_rlc_test_trigger_reestablishment(rnti);
          }
          TELNET_PRINT_TO_CLIENT(client_idx,
                                 "Reset RLC counters of UE RNTI %04x to trigger reestablishment\n",
                                 rnti);
        } else if (telnet_clients[client_idx].pending_reestab_count) {
          TELNET_PRINT_TO_CLIENT(client_idx, "UE RNTI %04x reestab %d reconfig %d\n",
                                 rnti, reestab, reconfig);
        } else {
          TELNET_PRINT_TO_CLIENT(client_idx, "single UE RNTI %04x\n", rnti);
        }
      }
    } else {
      TELNET_PRINT_TO_CLIENT(client_idx, "UE count is not exactly one in RRC\n");
    }
  } else {
    TELNET_PRINT_TO_CLIENT(client_idx, "no RRC present\n");
  }

  if (client_idx >= 0 && client_idx < TELNET_MAX_CLIENTS) {
    telnet_clients[client_idx].pending_reestab = false;
    telnet_clients[client_idx].pending_reestab_count = false;
    telnet_clients[client_idx].pending_f1_ho = false;
    telnet_clients[client_idx].pending_du_by_ue_id = false;
  }
}

void telnetsrv_handle_mac_trigger_reestab_for_first_rnti(Mac_trigger_reestab_for_first_rnti *mac_msg)
{
  int client_idx = mac_msg->client_idx;

  if (!telnetsrv_client_idx_valid_or_web_active(client_idx)) {
    return;
  }

  if (mac_msg->has_rnti) {
    TELNET_PRINT_TO_CLIENT(client_idx,
                           "Reset RLC counters of UE RNTI %04x to trigger reestablishment\n",
                           mac_msg->rnti);
  } else {
    TELNET_PRINT_TO_CLIENT(client_idx,
                           "Failed to trigger reestablishment: no UE found\n");
  }

  if (client_idx >= 0 && client_idx < TELNET_MAX_CLIENTS) {
    telnet_clients[client_idx].pending_reestab = false;
  }
}

void telnetsrv_handle_rrc_get_ue_context_by_ue_id(Rrc_get_single_ue_rnti *rrc_msg)
{
  int client_idx = rrc_msg->client_idx;

  if (!telnetsrv_client_idx_valid_or_web_active(client_idx)) {
    LOG_D(ITTI, "Received RRC_GET_UE_CONTEXT_BY_UE_ID for invalid client %d, ignoring\n", client_idx);
    return;
  }

  if (rrc_msg->is_single) {
    TELNET_PRINT_TO_CLIENT(client_idx,
                           "UE RNTI %04x reestab %d reconfig %d\n",
                           rrc_msg->rnti,
                           rrc_msg->ue_reestablishment_counter,
                           rrc_msg->ue_reconfiguration_counter);
  } else {
    TELNET_PRINT_TO_CLIENT(client_idx, "could not find UE with ue_id in RRC\n");
  }

  telnet_clients[client_idx].pending_reestab_count = false;
}

void telnetsrv_handle_rrc_get_du_id_by_ue_id(Rrc_get_du_id_by_ue_id *rrc_msg)
{
  int client_idx = rrc_msg->client_idx;

  if (!telnetsrv_client_idx_valid_or_web_active(client_idx)) {
    LOG_D(ITTI, "Received RRC_GET_DU_ID_BY_UE_ID for invalid client %d, ignoring\n", client_idx);
    return;
  }

  if (!rrc_msg->no_du) {
    TELNET_PRINT_TO_CLIENT(client_idx, "gNB_DU_id %d is connected to ue_id %d\n",
                           rrc_msg->du_id, rrc_msg->ue_id);
  } else {
    TELNET_PRINT_TO_CLIENT(client_idx,
                           "No DU connected or no UE found with the requested ue_id.\n");
  }
}

void telnetsrv_handle_rrc_check_ue_context(Rrc_check_ue_context *rrc_msg)
{
  int client_idx = rrc_msg->client_idx;

  if (!telnetsrv_client_idx_valid_or_web_active(client_idx)) {
    LOG_D(ITTI, "Received RRC_CHECK_UE_CONTEXT for invalid client %d, ignoring\n", client_idx);
    return;
  }

  if (!rrc_msg->check) {
    TELNET_PRINT_TO_CLIENT(client_idx, "UE with id %d not found\n",
                           rrc_msg->id);
  }
}

void telnetsrv_handle_rrc_get_ue_context_by_rnti_any_du(Rrc_get_ue_context_by_rnti_any_du *rrc_msg)
{
  int client_idx = rrc_msg->client_idx;

  if (!telnetsrv_client_idx_valid_or_web_active(client_idx)) {
    LOG_D(ITTI, "Received RRC_GET_UE_CONTEXT_BY_RNTI_ANY_DU for invalid client %d, ignoring\n", client_idx);
    return;
  }

  if (!rrc_msg->ue_context_exists) {
    TELNET_PRINT_TO_CLIENT(client_idx, "could not find UE with RNTI %04x\n",
                           rrc_msg->rnti);
  } else {
    TELNET_PRINT_TO_CLIENT(client_idx, "UE with RNTI %04x found\n",
                           rrc_msg->rnti);
  }
}

void telnetsrv_handle_rrc_get_ngap_ue_id(Rrc_get_ngap_ue_id *rrc_msg)
{
  int client_idx = rrc_msg->client_idx;

  if (!telnetsrv_client_idx_valid_or_web_active(client_idx)) {
    LOG_D(ITTI, "Received RRC_GET_NGAP_UE_ID for invalid client %d, ignoring\n", client_idx);
    return;
  }

  TELNET_PRINT_TO_CLIENT(client_idx,
                         "NGAP UE ID: gNB_ue_ngap_id=%u, amf_ue_ngap_id=%u\n",
                         rrc_msg->gNB_ue_ngap_id,
                         rrc_msg->amf_ue_ngap_id);
}

void telnetsrv_handle_rrc_gnb_generate_rrcrelease(Rrc_gnb_generate_rrcrelease *rrc_msg)
{
  int client_idx = rrc_msg->client_idx;

  if (!telnetsrv_client_idx_valid_or_web_active(client_idx)) {
    LOG_D(ITTI, "Received RRC_GNB_GENERATE_RRCRELEASE for invalid client %d, ignoring\n", client_idx);
    return;
  }

  UNUSED(rrc_msg);
  TELNET_PRINT_TO_CLIENT(client_idx, "RRC Release triggered\n");
}

void telnetsrv_handle_rrc_gnb_generate_rrcrelease_all(Rrc_gnb_generate_rrcrelease_all *rrc_msg)
{
  int client_idx = rrc_msg->client_idx;

  if (!telnetsrv_client_idx_valid_or_web_active(client_idx)) {
    LOG_D(ITTI, "Received RRC_GNB_GENERATE_RRCRELEASE_ALL for invalid client %d, ignoring\n", client_idx);
    return;
  }

  TELNET_PRINT_TO_CLIENT(client_idx, "RRC Release triggered for all UEs (%d releases)\n",
                         rrc_msg->nb_releases);
}

void telnetsrv_handle_rrc_gnb_trigger_ue_context_release_req(Rrc_gnb_trigger_ue_context_release_req *rrc_msg)
{
  int client_idx = rrc_msg->client_idx;

  if (!telnetsrv_client_idx_valid_or_web_active(client_idx)) {
    LOG_D(ITTI, "Received RRC_GNB_TRIGGER_UE_CONTEXT_RELEASE_REQ for invalid client %d, ignoring\n", client_idx);
    return;
  }

  TELNET_PRINT_TO_CLIENT(client_idx,
                         "Sent NGAP UE Context Release Request for UE ID %u (rrc_ue_context=%s, ngap_ue_context=%s)\n",
                         rrc_msg->ue_id,
                         rrc_msg->rrc_ue_context ? "true" : "false",
                         rrc_msg->ngap_ue_context ? "true" : "false");
}

void telnetsrv_handle_mac_get_ue_rnti(Mac_get_ue_rnti *mac_msg)
{
  int client_idx = mac_msg->client_idx;

  if (!telnetsrv_client_idx_valid_or_web_active(client_idx)) {
    LOG_D(ITTI, "Received MAC_GET_UE_RNTI for invalid client %d, ignoring\n", client_idx);
    return;
  }

  if (mac_msg->has_mac) {
    rnti_t rnti = mac_msg->rnti;

    if (telnetsrv_client_idx_valid_or_web_active(client_idx)) {
      if (telnet_clients[client_idx].pending_bwp_id != -1) {
        int bwpId = telnet_clients[client_idx].pending_bwp_id;
        TELNET_PRINT_TO_CLIENT(client_idx, "bwpId %d rnti %04x\n", bwpId, rnti);
        if (!nr_trigger_bwp_switch(rnti, bwpId)) {
          TELNET_PRINT_TO_CLIENT(client_idx, "failed trigger BWP switch for UE %04x BWP ID %d\n",
                                 rnti, bwpId);
        } else {
          TELNET_PRINT_TO_CLIENT(client_idx, "triggered BWP switch to BWP ID %d for UE %04x\n",
                                 bwpId, rnti);
        }
        telnet_clients[client_idx].pending_bwp_id = -1;
      } else if (telnet_clients[client_idx].pending_bwp_info) {
        MessageDef *msg_p = itti_alloc_new_message(TASK_TELNET, 0, MAC_GET_UE_BWP_INFO);
        msg_p->ittiMsg.mac_get_ue_bwp_info.rnti = rnti;
        msg_p->ittiMsg.mac_get_ue_bwp_info.client_idx = client_idx;
        itti_send_msg_to_task(TASK_MAC_GNB, 0, msg_p);
      } else if (telnet_clients[client_idx].pending_ue_release) {
        MessageDef *msg_ul_p = itti_alloc_new_message(TASK_TELNET, 0, MAC_FORCE_UL_FAILURE);
        msg_ul_p->ittiMsg.mac_force_ul_failure.rnti = rnti;
        msg_ul_p->ittiMsg.mac_force_ul_failure.client_idx = client_idx;
        itti_send_msg_to_task(TASK_MAC_GNB, 0, msg_ul_p);

        MessageDef *msg_p = itti_alloc_new_message(TASK_TELNET, 0, MAC_FORCE_UE_RELEASE);
        msg_p->ittiMsg.mac_force_ue_release.rnti = rnti;
        msg_p->ittiMsg.mac_force_ue_release.client_idx = client_idx;
        itti_send_msg_to_task(TASK_MAC_GNB, 0, msg_p);
        TELNET_PRINT_TO_CLIENT(client_idx,
                               "Reset RLC counters and triggered UE release for UE RNTI %04x\n",
                               rnti);
      } else if (telnet_clients[client_idx].pending_ul_failure) {
        MessageDef *msg_p = itti_alloc_new_message(TASK_TELNET, 0, MAC_FORCE_UL_FAILURE);
        msg_p->ittiMsg.mac_force_ul_failure.rnti = rnti;
        msg_p->ittiMsg.mac_force_ul_failure.client_idx = client_idx;
        itti_send_msg_to_task(TASK_MAC_GNB, 0, msg_p);
      } else if (telnet_clients[client_idx].pending_reestab) {
        nr_rlc_test_trigger_reestablishment(rnti);
        TELNET_PRINT_TO_CLIENT(client_idx,
                               "Reset RLC counters of UE RNTI %04x to trigger reestablishment\n",
                               rnti);
      } else {
        TELNET_PRINT_TO_CLIENT(client_idx, "single UE RNTI %04x\n", rnti);
      }
    } else {
      TELNET_PRINT_TO_CLIENT(client_idx, "single UE RNTI %04x\n", rnti);
    }
  } else {
    TELNET_PRINT_TO_CLIENT(client_idx, "no MAC/RLC present\n");
  }

  if (client_idx >= 0 && client_idx < TELNET_MAX_CLIENTS) {
    telnet_clients[client_idx].pending_reestab = false;
    telnet_clients[client_idx].pending_ul_failure = false;
    telnet_clients[client_idx].pending_ue_release = false;
    telnet_clients[client_idx].pending_bwp_info = false;
    telnet_clients[client_idx].pending_bwp_id = -1;
  }
}

void telnetsrv_handle_mac_get_ue_rnti_by_uid(Mac_get_ue_rnti_by_uid *mac_msg)
{
  int client_idx = mac_msg->client_idx;

  if (!telnetsrv_client_idx_valid_or_web_active(client_idx)) {
    LOG_D(ITTI, "Received MAC_GET_UE_RNTI_BY_UID for invalid client %d, ignoring\n", client_idx);
    return;
  }

  if (mac_msg->has_mac) {
    TELNET_PRINT_TO_CLIENT(client_idx, "UE RNTI %04x for UID %u\n",
                           mac_msg->rnti, mac_msg->uid);
  } else {
    TELNET_PRINT_TO_CLIENT(client_idx, "no MAC/RLC present for UID %u\n",
                           mac_msg->uid);
  }
}

void telnetsrv_handle_mac_get_ue_bwp_info(Mac_get_ue_bwp_info *mac_msg)
{
  int client_idx = mac_msg->client_idx;

  if (!telnetsrv_client_idx_valid_or_web_active(client_idx)) {
    LOG_D(ITTI, "Received MAC_GET_UE_BWP_INFO for invalid client %d, ignoring\n", client_idx);
    return;
  }

  rnti_t rnti = mac_msg->rnti;
  int dl_bwp = mac_msg->dl_bwp_id;
  int ul_bwp = mac_msg->ul_bwp_id;

  if (dl_bwp >= 0 && ul_bwp >= 0) {
    const char *dl_bwp_text = dl_bwp > 0 ? "dedicated" : "initial";
    const char *ul_bwp_text = ul_bwp > 0 ? "dedicated" : "initial";
    TELNET_PRINT_TO_CLIENT(client_idx,
                           "UE %04x DL BWP ID %d (%s) UL BWP ID %d (%s)\n",
                           rnti,
                           dl_bwp,
                           dl_bwp_text,
                           ul_bwp,
                           ul_bwp_text);
  } else {
    TELNET_PRINT_TO_CLIENT(client_idx, "could not find UE with RNTI %04x\n",
                           rnti);
  }
}

void telnetsrv_handle_mac_force_ul_failure(Mac_force_ul_failure *mac_msg)
{
  int client_idx = mac_msg->client_idx;

  if (!telnetsrv_client_idx_valid_or_web_active(client_idx)) {
    LOG_D(ITTI, "Received MAC_FORCE_UL_FAILURE for invalid client %d, ignoring\n", client_idx);
    return;
  }

  UNUSED(mac_msg);
  TELNET_PRINT_TO_CLIENT(client_idx, "UL failure triggered\n");
}

void telnetsrv_handle_mac_force_ue_release(Mac_force_ue_release *mac_msg)
{
  int client_idx = mac_msg->client_idx;

  if (!telnetsrv_client_idx_valid_or_web_active(client_idx)) {
    LOG_D(ITTI, "Received MAC_FORCE_UE_RELEASE for invalid client %d, ignoring\n", client_idx);
    return;
  }

  UNUSED(mac_msg);
  TELNET_PRINT_TO_CLIENT(client_idx, "UE release triggered\n");
}

void telnetsrv_handle_mac_set_pusch_target_snr(Mac_set_pusch_target_snr *mac_msg)
{
  int client_idx = mac_msg->client_idx;

  if (!telnetsrv_client_idx_valid_or_web_active(client_idx)) {
    LOG_D(ITTI, "Received MAC_SET_PUSCH_TARGET_SNR for invalid client %d, ignoring\n", client_idx);
    return;
  }

  UNUSED(mac_msg);
  TELNET_PRINT_TO_CLIENT(client_idx, "PUSCH target SNR set\n");
}

void telnetsrv_handle_mac_set_bwconfig(Mac_set_bwconfig *mac_msg)
{
  int client_idx = mac_msg->client_idx;

  if (!telnetsrv_client_idx_valid_or_web_active(client_idx)) {
    LOG_D(ITTI, "Received MAC_SET_BWCONFIG for invalid client %d, ignoring\n", client_idx);
    return;
  }

  UNUSED(mac_msg);
  TELNET_PRINT_TO_CLIENT(client_idx, "OK\n");
}

void telnetsrv_handle_mac_stop_modem(Mac_stop_modem *mac_msg)
{
  int client_idx = mac_msg->client_idx;

  if (!telnetsrv_client_idx_valid_or_web_active(client_idx)) {
    LOG_D(ITTI, "Received MAC_STOP_MODEM for invalid client %d, ignoring\n", client_idx);
    return;
  }

  UNUSED(mac_msg);
  telnetsrv_o1_running = false;
  TELNET_PRINT_TO_CLIENT(client_idx, "OK\n");
}

void telnetsrv_handle_mac_get_o1_stats(Mac_get_o1_stats *mac_msg)
{
  int client_idx = mac_msg->client_idx;

  if (!telnetsrv_client_idx_valid_or_web_active(client_idx)) {
    LOG_D(ITTI, "Received MAC_GET_O1_STATS for invalid client %d, ignoring\n", client_idx);
    return;
  }

  Mac_get_o1_stats *current = mac_msg;

  static uint64_t last_dl_used = 0;
  static uint64_t last_dl_total = 0;
  int diff_used = current->dl_used_prb_aggregate - last_dl_used;
  int diff_total = current->dl_total_prb_aggregate - last_dl_total;
  int load = diff_total > 0 ? 100 * diff_used / diff_total : 0;
  last_dl_used = current->dl_used_prb_aggregate;
  last_dl_total = current->dl_total_prb_aggregate;

  static struct timespec tp_last = {0};
  size_t diff_msec = (current->tp_now.tv_sec - tp_last.tv_sec) * 1000 + (current->tp_now.tv_nsec - tp_last.tv_nsec) / 1000000;
  tp_last = current->tp_now;
  if (diff_msec == 0)
    diff_msec = 1; /* Avoid division by zero */

  static long last_total_dl[MAX_MOBILES_PER_GNB] = {0};
  static long last_total_ul[MAX_MOBILES_PER_GNB] = {0};

  int num_ues = current->num_ues;

  // Calculate throughput for each UE
  for (int i = 0; i < num_ues; i++) {
    if (last_total_dl[i] > current->ue_rlc_stats[i].txpdu_bytes)
      last_total_dl[i] = current->ue_rlc_stats[i].txpdu_bytes;
    if (last_total_ul[i] > current->ue_rlc_stats[i].rxpdu_bytes)
      last_total_ul[i] = current->ue_rlc_stats[i].rxpdu_bytes;

    last_total_dl[i] = current->ue_rlc_stats[i].txpdu_bytes;
    last_total_ul[i] = current->ue_rlc_stats[i].rxpdu_bytes;
  }

  TELNET_PRINT_TO_CLIENT(client_idx, "{\n");
  TELNET_PRINT_TO_CLIENT(client_idx, "  \"o1-config\": {\n");

  TELNET_PRINT_TO_CLIENT(client_idx, "    \"BWP\": {\n");
  TELNET_PRINT_TO_CLIENT(client_idx, "      \"dl\": [{\n");
  TELNET_PRINT_TO_CLIENT(client_idx, "        \"bwp3gpp:isInitialBwp\": true,\n");
  TELNET_PRINT_TO_CLIENT(client_idx, "        \"bwp3gpp:numberOfRBs\": %ld,\n", current->dl_numrbs);
  TELNET_PRINT_TO_CLIENT(client_idx, "        \"bwp3gpp:startRB\": %ld,\n", current->dl_startrb);
  TELNET_PRINT_TO_CLIENT(client_idx, "        \"bwp3gpp:subCarrierSpacing\": %ld\n", current->dl_bwpscs);
  TELNET_PRINT_TO_CLIENT(client_idx, "      }],\n");
  TELNET_PRINT_TO_CLIENT(client_idx, "      \"ul\": [{\n");
  TELNET_PRINT_TO_CLIENT(client_idx, "        \"bwp3gpp:isInitialBwp\": true,\n");
  TELNET_PRINT_TO_CLIENT(client_idx, "        \"bwp3gpp:numberOfRBs\": %ld,\n", current->ul_numrbs);
  TELNET_PRINT_TO_CLIENT(client_idx, "        \"bwp3gpp:startRB\": %ld,\n", current->ul_startrb);
  TELNET_PRINT_TO_CLIENT(client_idx, "        \"bwp3gpp:subCarrierSpacing\": %ld\n", current->ul_bwpscs);
  TELNET_PRINT_TO_CLIENT(client_idx, "      }]\n");
  TELNET_PRINT_TO_CLIENT(client_idx, "    },\n");

  TELNET_PRINT_TO_CLIENT(client_idx, "    \"NRCELLDU\": {\n");
  TELNET_PRINT_TO_CLIENT(client_idx, "      \"nrcelldu3gpp:ssbFrequency\": %ld,\n", current->ssbFrequency);
  TELNET_PRINT_TO_CLIENT(client_idx, "      \"nrcelldu3gpp:arfcnDL\": %ld,\n", current->arfcnDL);
  TELNET_PRINT_TO_CLIENT(client_idx, "      \"nrcelldu3gpp:bSChannelBwDL\": %ld,\n", current->bw_mhz);
  TELNET_PRINT_TO_CLIENT(client_idx, "      \"nrcelldu3gpp:arfcnUL\": %ld,\n", current->arfcnUL);
  TELNET_PRINT_TO_CLIENT(client_idx, "      \"nrcelldu3gpp:bSChannelBwUL\": %ld,\n", current->bw_mhz);
  TELNET_PRINT_TO_CLIENT(client_idx, "      \"nrcelldu3gpp:nRPCI\": %ld,\n", current->pci);
  TELNET_PRINT_TO_CLIENT(client_idx, "      \"nrcelldu3gpp:nRTAC\": %ld,\n", current->tac);
  TELNET_PRINT_TO_CLIENT(client_idx, "      \"nrcelldu3gpp:mcc\": \"%03d\",\n", current->mcc);
  TELNET_PRINT_TO_CLIENT(client_idx, "      \"nrcelldu3gpp:mnc\": \"%0*d\",\n", current->mnc_digit_length, current->mnc);
  TELNET_PRINT_TO_CLIENT(client_idx, "      \"nrcelldu3gpp:sd\": %d,\n", current->sd);
  TELNET_PRINT_TO_CLIENT(client_idx, "      \"nrcelldu3gpp:sst\": %d\n", current->sst);
  TELNET_PRINT_TO_CLIENT(client_idx, "    },\n");
  TELNET_PRINT_TO_CLIENT(client_idx, "    \"device\": {\n");
  TELNET_PRINT_TO_CLIENT(client_idx, "      \"gnbId\": %d,\n", current->gNB_DU_id);
  TELNET_PRINT_TO_CLIENT(client_idx, "      \"gnbName\": \"%s\",\n", current->gNB_DU_name);
  TELNET_PRINT_TO_CLIENT(client_idx, "      \"vendor\": \"OpenAirInterface\"\n");
  TELNET_PRINT_TO_CLIENT(client_idx, "    }\n");
  TELNET_PRINT_TO_CLIENT(client_idx, "  },\n");

  TELNET_PRINT_TO_CLIENT(client_idx, "  \"O1-Operational\": {\n");
  TELNET_PRINT_TO_CLIENT(client_idx, "    \"frame-type\": \"%s\",\n", current->frame_type == 1 ? "tdd" : "fdd");
  TELNET_PRINT_TO_CLIENT(client_idx, "    \"band-number\": %ld,\n", current->band);
  TELNET_PRINT_TO_CLIENT(client_idx, "    \"num-ues\": %d,\n", num_ues);

  TELNET_PRINT_TO_CLIENT(client_idx, "    \"ues\": [");
  for (int i = 0; i < num_ues; i++) {
    if (i != 0)
      TELNET_PRINT_TO_CLIENT(client_idx, ", ");
    TELNET_PRINT_TO_CLIENT(client_idx, "%d", current->ue_rlc_stats[i].rnti);
  }
  TELNET_PRINT_TO_CLIENT(client_idx, "],\n");

  TELNET_PRINT_TO_CLIENT(client_idx, "    \"load\": %d,\n", load);
  TELNET_PRINT_TO_CLIENT(client_idx, "    \"ues-thp\": [");
  for (int i = 0; i < num_ues; i++) {
    if (i != 0)
      TELNET_PRINT_TO_CLIENT(client_idx, ",\n      ");
    long dl_thr = (current->ue_rlc_stats[i].txpdu_bytes - last_total_dl[i]) * 8 / diff_msec;
    long ul_thr = (current->ue_rlc_stats[i].rxpdu_bytes - last_total_ul[i]) * 8 / diff_msec;
    TELNET_PRINT_TO_CLIENT(client_idx,
                           "{\"rnti\": %d, \"dl\": %ld, \"ul\": %ld}",
                           current->ue_rlc_stats[i].rnti, dl_thr, ul_thr);
  }
  TELNET_PRINT_TO_CLIENT(client_idx, "\n    ]\n");
  TELNET_PRINT_TO_CLIENT(client_idx, "  }\n");
  TELNET_PRINT_TO_CLIENT(client_idx, "}\n");
  TELNET_PRINT_TO_CLIENT(client_idx, "OK\n");
}
