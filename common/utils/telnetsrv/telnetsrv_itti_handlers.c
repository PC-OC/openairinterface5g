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

void telnetsrv_handle_rrc_get_single_ue_rnti(Rrc_get_single_ue_rnti *rrc_msg)
{
  int client_idx = rrc_msg->client_idx;
  
  if (client_idx < 0 || client_idx >= TELNET_MAX_CLIENTS || !telnet_clients[client_idx].active) {
    // Invalid client, use default output
    client_idx = -1;
  }

  if (rrc_msg->has_rrc) {
    if (rrc_msg->is_single) {
      rnti_t rnti = rrc_msg->rnti;
      ue_id_t ue_id = rrc_msg->id;
      int reestab = rrc_msg->ue_reestablishment_counter;
      int reconfig = rrc_msg->ue_reconfiguration_counter;

      if (client_idx >= 0 && telnet_clients[client_idx].pending_du_by_ue_id) {
        MessageDef *msg_p = itti_alloc_new_message(TASK_TELNET, 0, RRC_GET_DU_ID_BY_UE_ID);
        msg_p->ittiMsg.rrc_get_du_id_by_ue_id.ue_id = ue_id;
        msg_p->ittiMsg.rrc_get_du_id_by_ue_id.client_idx = client_idx;
        itti_send_msg_to_task(TASK_RRC_GNB, 0, msg_p);
      }
      else if (client_idx >= 0 && telnet_clients[client_idx].pending_f1_ho) {
        MessageDef *msg_p = itti_alloc_new_message(TASK_TELNET, 0, RRC_NR_F1_HO_TRIGGER);
        msg_p->ittiMsg.rrc_trigger_f1_ho.id = ue_id;
        msg_p->ittiMsg.rrc_trigger_f1_ho.client_idx = client_idx;
        itti_send_msg_to_task(TASK_RRC_GNB, 0, msg_p);
        if (client_idx >= 0) {
          client_printf_to_client(client_idx, "RRC F1 handover triggered for UE %u\n", ue_id);
        } else {
          client_printf("RRC F1 handover triggered for UE %u\n", ue_id);
        }
      }
      else if (client_idx >= 0 && telnet_clients[client_idx].pending_reestab) {
        nr_rlc_test_trigger_reestablishment(ue_id);
        if (rnti != ue_id) {
          nr_rlc_test_trigger_reestablishment(rnti);
        }
        if (client_idx >= 0) {
          client_printf_to_client(client_idx, "Reset RLC counters of UE RNTI %04x to trigger reestablishment\n", rnti);
        } else {
          client_printf("Reset RLC counters of UE RNTI %04x to trigger reestablishment\n", rnti);
        }
      }
      else if (client_idx >= 0 && telnet_clients[client_idx].pending_reestab_count) {
        if (client_idx >= 0) {
          client_printf_to_client(client_idx, "UE RNTI %04x reestab %d reconfig %d\n", rnti, reestab, reconfig);
        } else {
          client_printf("UE RNTI %04x reestab %d reconfig %d\n", rnti, reestab, reconfig);
        }
      }
      else {
        if (client_idx >= 0) {
          client_printf_to_client(client_idx, "single UE RNTI %04x\n", rnti);
        } else {
          client_printf("single UE RNTI %04x\n", rnti);
        }
      }
    } else {
      if (client_idx >= 0) {
        client_printf_to_client(client_idx, "UE count is not exactly one in RRC\n");
      } else {
        client_printf("UE count is not exactly one in RRC\n");
      }
    }
  } else {
    if (client_idx >= 0) {
      client_printf_to_client(client_idx, "no RRC present\n");
    } else {
      client_printf("no RRC present\n");
    }
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
  
  if (client_idx >= 0 && client_idx < TELNET_MAX_CLIENTS && telnet_clients[client_idx].active) {
    if (mac_msg->has_rnti) {
      client_printf_to_client(client_idx, "Reset RLC counters of UE RNTI %04x to trigger reestablishment\n", mac_msg->rnti);
    } else {
      client_printf_to_client(client_idx, "Failed to trigger reestablishment: no UE found\n");
    }
    telnet_clients[client_idx].pending_reestab = false;
  } else {
    if (mac_msg->has_rnti) {
      client_printf("Reset RLC counters of UE RNTI %04x to trigger reestablishment\n", mac_msg->rnti);
    } else {
      client_printf("Failed to trigger reestablishment: no UE found\n");
    }
  }
}

void telnetsrv_handle_rrc_get_ue_context_by_ue_id(Rrc_get_single_ue_rnti *rrc_msg)
{
  int client_idx = rrc_msg->client_idx;
  
  if (rrc_msg->is_single) {
    if (client_idx >= 0 && client_idx < TELNET_MAX_CLIENTS && telnet_clients[client_idx].active) {
      client_printf_to_client(client_idx, "UE RNTI %04x reestab %d reconfig %d\n",
                  rrc_msg->rnti,
                  rrc_msg->ue_reestablishment_counter,
                  rrc_msg->ue_reconfiguration_counter);
    } else {
      client_printf("UE RNTI %04x reestab %d reconfig %d\n",
                  rrc_msg->rnti,
                  rrc_msg->ue_reestablishment_counter,
                  rrc_msg->ue_reconfiguration_counter);
    }
  } else {
    if (client_idx >= 0 && client_idx < TELNET_MAX_CLIENTS && telnet_clients[client_idx].active) {
      client_printf_to_client(client_idx, "could not find UE with ue_id in RRC\n");
    } else {
      client_printf("could not find UE with ue_id in RRC\n");
    }
  }
  if (client_idx >= 0 && client_idx < TELNET_MAX_CLIENTS) {
    telnet_clients[client_idx].pending_reestab_count = false;
  }
}

void telnetsrv_handle_rrc_get_du_id_by_ue_id(Rrc_get_du_id_by_ue_id *rrc_msg)
{
  int client_idx = rrc_msg->client_idx;
  
  if (!rrc_msg->no_du) {
    if (client_idx >= 0 && client_idx < TELNET_MAX_CLIENTS && telnet_clients[client_idx].active) {
      client_printf_to_client(client_idx, "gNB_DU_id %d is connected to ue_id %d\n",
                  rrc_msg->du_id,
                  rrc_msg->ue_id);
    } else {
      client_printf("gNB_DU_id %d is connected to ue_id %d\n",
                  rrc_msg->du_id,
                  rrc_msg->ue_id);
    }
  } else {
    if (client_idx >= 0 && client_idx < TELNET_MAX_CLIENTS && telnet_clients[client_idx].active) {
      client_printf_to_client(client_idx, "No DU connected or no UE found with the requested ue_id.\n");
    } else {
      client_printf("No DU connected or no UE found with the requested ue_id.\n");
    }
  }
}

void telnetsrv_handle_rrc_check_ue_context(Rrc_check_ue_context *rrc_msg)
{
  int client_idx = rrc_msg->client_idx;
  
  if (!rrc_msg->check) {
    if (client_idx >= 0 && client_idx < TELNET_MAX_CLIENTS && telnet_clients[client_idx].active) {
      client_printf_to_client(client_idx, "UE with id %d not found\n", rrc_msg->id);
    } else {
      client_printf("UE with id %d not found\n", rrc_msg->id);
    }
  }
}

void telnetsrv_handle_rrc_get_ue_context_by_rnti_any_du(Rrc_get_ue_context_by_rnti_any_du *rrc_msg)
{
  int client_idx = rrc_msg->client_idx;
  
  if (!rrc_msg->ue_context_exists) {
    if (client_idx >= 0 && client_idx < TELNET_MAX_CLIENTS && telnet_clients[client_idx].active) {
      client_printf_to_client(client_idx, "could not find UE with RNTI %04x\n", rrc_msg->rnti);
    } else {
      client_printf("could not find UE with RNTI %04x\n", rrc_msg->rnti);
    }
  } else {
    if (client_idx >= 0 && client_idx < TELNET_MAX_CLIENTS && telnet_clients[client_idx].active) {
      client_printf_to_client(client_idx, "UE with RNTI %04x found\n", rrc_msg->rnti);
    } else {
      client_printf("UE with RNTI %04x found\n", rrc_msg->rnti);
    }
  }
}

void telnetsrv_handle_rrc_get_ngap_ue_id(Rrc_get_ngap_ue_id *rrc_msg)
{
  int client_idx = rrc_msg->client_idx;
  
  if (client_idx >= 0 && client_idx < TELNET_MAX_CLIENTS && telnet_clients[client_idx].active) {
    client_printf_to_client(client_idx, "NGAP UE ID: gNB_ue_ngap_id=%u, amf_ue_ngap_id=%u\n",
                rrc_msg->gNB_ue_ngap_id,
                rrc_msg->amf_ue_ngap_id);
  } else {
    client_printf("NGAP UE ID: gNB_ue_ngap_id=%u, amf_ue_ngap_id=%u\n",
                rrc_msg->gNB_ue_ngap_id,
                rrc_msg->amf_ue_ngap_id);
  }
}

void telnetsrv_handle_rrc_gnb_generate_rrcrelease(Rrc_gnb_generate_rrcrelease *rrc_msg)
{
  int client_idx = rrc_msg->client_idx;
  
  UNUSED(rrc_msg);
  if (client_idx >= 0 && client_idx < TELNET_MAX_CLIENTS && telnet_clients[client_idx].active) {
    client_printf_to_client(client_idx, "RRC Release triggered\n");
  } else {
    client_printf("RRC Release triggered\n");
  }
}

void telnetsrv_handle_rrc_gnb_generate_rrcrelease_all(Rrc_gnb_generate_rrcrelease_all *rrc_msg)
{
  int client_idx = rrc_msg->client_idx;
  
  if (client_idx >= 0 && client_idx < TELNET_MAX_CLIENTS && telnet_clients[client_idx].active) {
    client_printf_to_client(client_idx, "RRC Release triggered for all UEs (%d releases)\n",
                rrc_msg->nb_releases);
  } else {
    client_printf("RRC Release triggered for all UEs (%d releases)\n",
                rrc_msg->nb_releases);
  }
}

void telnetsrv_handle_rrc_gnb_trigger_ue_context_release_req(Rrc_gnb_trigger_ue_context_release_req *rrc_msg)
{
  int client_idx = rrc_msg->client_idx;
  
  if (client_idx >= 0 && client_idx < TELNET_MAX_CLIENTS && telnet_clients[client_idx].active) {
    client_printf_to_client(client_idx, "Sent NGAP UE Context Release Request for UE ID %u (rrc_ue_context=%s, ngap_ue_context=%s)\n",
                rrc_msg->ue_id,
                rrc_msg->rrc_ue_context ? "true" : "false",
                rrc_msg->ngap_ue_context ? "true" : "false");
  } else {
    client_printf("Sent NGAP UE Context Release Request for UE ID %u (rrc_ue_context=%s, ngap_ue_context=%s)\n",
                rrc_msg->ue_id,
                rrc_msg->rrc_ue_context ? "true" : "false",
                rrc_msg->ngap_ue_context ? "true" : "false");
  }
}

void telnetsrv_handle_mac_get_ue_rnti(Mac_get_ue_rnti *mac_msg)
{
  int client_idx = mac_msg->client_idx;
  
  if (client_idx < 0 || client_idx >= TELNET_MAX_CLIENTS || !telnet_clients[client_idx].active) {
    // Invalid client, use default output
    if (mac_msg->has_mac) {
      client_printf("single UE RNTI %04x\n", mac_msg->rnti);
    } else {
      client_printf("no MAC/RLC present\n");
    }
    return;
  }

  if (mac_msg->has_mac) {
    rnti_t rnti = mac_msg->rnti;

    if (telnet_clients[client_idx].pending_bwp_id != -1) {
      int bwpId = telnet_clients[client_idx].pending_bwp_id;
      client_printf_to_client(client_idx, "bwpId %d rnti %04x\n", bwpId, rnti);
      if (!nr_trigger_bwp_switch(rnti, bwpId)) {
        client_printf_to_client(client_idx, "failed trigger BWP switch for UE %04x BWP ID %d\n", rnti, bwpId);
      } else {
        client_printf_to_client(client_idx, "triggered BWP switch to BWP ID %d for UE %04x\n", bwpId, rnti);
      }
      telnet_clients[client_idx].pending_bwp_id = -1;
    }
    else if (telnet_clients[client_idx].pending_bwp_info) {
      MessageDef *msg_p = itti_alloc_new_message(TASK_TELNET, 0, MAC_GET_UE_BWP_INFO);
      msg_p->ittiMsg.mac_get_ue_bwp_info.rnti = rnti;
      msg_p->ittiMsg.mac_get_ue_bwp_info.client_idx = client_idx;
      itti_send_msg_to_task(TASK_MAC_GNB, 0, msg_p);
    }
    else if (telnet_clients[client_idx].pending_ue_release) {
      MessageDef *msg_ul_p = itti_alloc_new_message(TASK_TELNET, 0, MAC_FORCE_UL_FAILURE);
      msg_ul_p->ittiMsg.mac_force_ul_failure.rnti = rnti;
      msg_ul_p->ittiMsg.mac_force_ul_failure.client_idx = client_idx;
      itti_send_msg_to_task(TASK_MAC_GNB, 0, msg_ul_p);

      MessageDef *msg_p = itti_alloc_new_message(TASK_TELNET, 0, MAC_FORCE_UE_RELEASE);
      msg_p->ittiMsg.mac_force_ue_release.rnti = rnti;
      msg_p->ittiMsg.mac_force_ue_release.client_idx = client_idx;
      itti_send_msg_to_task(TASK_MAC_GNB, 0, msg_p);
      client_printf_to_client(client_idx, "Reset RLC counters and triggered UE release for UE RNTI %04x\n", rnti);
    }
    else if (telnet_clients[client_idx].pending_ul_failure) {
      MessageDef *msg_p = itti_alloc_new_message(TASK_TELNET, 0, MAC_FORCE_UL_FAILURE);
      msg_p->ittiMsg.mac_force_ul_failure.rnti = rnti;
      msg_p->ittiMsg.mac_force_ul_failure.client_idx = client_idx;
      itti_send_msg_to_task(TASK_MAC_GNB, 0, msg_p);
    }
    else if (telnet_clients[client_idx].pending_reestab) {
      nr_rlc_test_trigger_reestablishment(rnti);
      client_printf_to_client(client_idx, "Reset RLC counters of UE RNTI %04x to trigger reestablishment\n", rnti);
    }
    else {
      client_printf_to_client(client_idx, "single UE RNTI %04x\n", rnti);
    }
  } else {
    client_printf_to_client(client_idx, "no MAC/RLC present\n");
  }

  // Reset pending flags
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
  
  if (mac_msg->has_mac) {
    if (client_idx >= 0 && client_idx < TELNET_MAX_CLIENTS && telnet_clients[client_idx].active) {
      client_printf_to_client(client_idx, "UE RNTI %04x for UID %u\n",
                  mac_msg->rnti,
                  mac_msg->uid);
    } else {
      client_printf("UE RNTI %04x for UID %u\n",
                  mac_msg->rnti,
                  mac_msg->uid);
    }
  } else {
    if (client_idx >= 0 && client_idx < TELNET_MAX_CLIENTS && telnet_clients[client_idx].active) {
      client_printf_to_client(client_idx, "no MAC/RLC present for UID %u\n", mac_msg->uid);
    } else {
      client_printf("no MAC/RLC present for UID %u\n", mac_msg->uid);
    }
  }
}

void telnetsrv_handle_mac_get_ue_bwp_info(Mac_get_ue_bwp_info *mac_msg)
{
  int client_idx = mac_msg->client_idx;
  rnti_t rnti = mac_msg->rnti;
  int dl_bwp = mac_msg->dl_bwp_id;
  int ul_bwp = mac_msg->ul_bwp_id;

  if (dl_bwp >= 0 && ul_bwp >= 0) {
    const char *dl_bwp_text = dl_bwp > 0 ? "dedicated" : "initial";
    const char *ul_bwp_text = ul_bwp > 0 ? "dedicated" : "initial";
    if (client_idx >= 0 && client_idx < TELNET_MAX_CLIENTS && telnet_clients[client_idx].active) {
      client_printf_to_client(client_idx, "UE %04x DL BWP ID %d (%s) UL BWP ID %d (%s)\n",
                  rnti, dl_bwp, dl_bwp_text, ul_bwp, ul_bwp_text);
    } else {
      client_printf("UE %04x DL BWP ID %d (%s) UL BWP ID %d (%s)\n",
                  rnti, dl_bwp, dl_bwp_text, ul_bwp, ul_bwp_text);
    }
  } else {
    if (client_idx >= 0 && client_idx < TELNET_MAX_CLIENTS && telnet_clients[client_idx].active) {
      client_printf_to_client(client_idx, "could not find UE with RNTI %04x\n", rnti);
    } else {
      client_printf("could not find UE with RNTI %04x\n", rnti);
    }
  }
}

void telnetsrv_handle_mac_force_ul_failure(Mac_force_ul_failure *mac_msg)
{
  int client_idx = mac_msg->client_idx;
  
  UNUSED(mac_msg);
  if (client_idx >= 0 && client_idx < TELNET_MAX_CLIENTS && telnet_clients[client_idx].active) {
    client_printf_to_client(client_idx, "UL failure triggered\n");
  } else {
    client_printf("UL failure triggered\n");
  }
}

void telnetsrv_handle_mac_force_ue_release(Mac_force_ue_release *mac_msg)
{
  int client_idx = mac_msg->client_idx;
  
  UNUSED(mac_msg);
  if (client_idx >= 0 && client_idx < TELNET_MAX_CLIENTS && telnet_clients[client_idx].active) {
    client_printf_to_client(client_idx, "UE release triggered\n");
  } else {
    client_printf("UE release triggered\n");
  }
}

void telnetsrv_handle_mac_set_pusch_target_snr(Mac_set_pusch_target_snr *mac_msg)
{
  int client_idx = mac_msg->client_idx;
  
  UNUSED(mac_msg);
  if (client_idx >= 0 && client_idx < TELNET_MAX_CLIENTS && telnet_clients[client_idx].active) {
    client_printf_to_client(client_idx, "PUSCH target SNR set\n");
  } else {
    client_printf("PUSCH target SNR set\n");
  }
}

void telnetsrv_handle_mac_set_bwconfig(Mac_set_bwconfig *mac_msg)
{
  int client_idx = mac_msg->client_idx;
  
  UNUSED(mac_msg);
  if (client_idx >= 0 && client_idx < TELNET_MAX_CLIENTS && telnet_clients[client_idx].active) {
    client_printf_to_client(client_idx, "OK\n");
  } else {
    client_printf("OK\n");
  }
}

void telnetsrv_handle_mac_stop_modem(Mac_stop_modem *mac_msg)
{
  int client_idx = mac_msg->client_idx;
  
  UNUSED(mac_msg);
  telnetsrv_o1_running = false;
  if (client_idx >= 0 && client_idx < TELNET_MAX_CLIENTS && telnet_clients[client_idx].active) {
    client_printf_to_client(client_idx, "OK\n");
  } else {
    client_printf("OK\n");
  }
}

void telnetsrv_handle_mac_get_o1_stats(Mac_get_o1_stats *mac_msg)
{
  int client_idx = mac_msg->client_idx;
  Mac_get_o1_stats *current = mac_msg;
  
  // Helper macro to print to correct client
  #define PRINT_TO_CLIENT(fmt, ...) \
    do { \
      if (client_idx >= 0 && client_idx < TELNET_MAX_CLIENTS && telnet_clients[client_idx].active) { \
        client_printf_to_client(client_idx, fmt, ##__VA_ARGS__); \
      } else { \
        client_printf(fmt, ##__VA_ARGS__); \
      } \
    } while (0)

  static uint64_t last_dl_used = 0;
  static uint64_t last_dl_total = 0;
  int diff_used = current->dl_used_prb_aggregate - last_dl_used;
  int diff_total = current->dl_total_prb_aggregate - last_dl_total;
  int load = diff_total > 0 ? 100 * diff_used / diff_total : 0;
  last_dl_used = current->dl_used_prb_aggregate;
  last_dl_total = current->dl_total_prb_aggregate;

  static struct timespec tp_last = {0};
  size_t diff_msec = (current->tp_now.tv_sec - tp_last.tv_sec) * 1000 +
                     (current->tp_now.tv_nsec - tp_last.tv_nsec) / 1000000;
  tp_last = current->tp_now;
  if (diff_msec == 0) diff_msec = 1; /* Avoid division by zero */

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

  PRINT_TO_CLIENT("{\n");
  PRINT_TO_CLIENT("  \"o1-config\": {\n");

  PRINT_TO_CLIENT("    \"BWP\": {\n");
  PRINT_TO_CLIENT("      \"dl\": [{\n");
  PRINT_TO_CLIENT("        \"bwp3gpp:isInitialBwp\": true,\n");
  PRINT_TO_CLIENT("        \"bwp3gpp:numberOfRBs\": %ld,\n", current->dl_numrbs);
  PRINT_TO_CLIENT("        \"bwp3gpp:startRB\": %ld,\n", current->dl_startrb);
  PRINT_TO_CLIENT("        \"bwp3gpp:subCarrierSpacing\": %ld\n", current->dl_bwpscs);
  PRINT_TO_CLIENT("      }],\n");
  PRINT_TO_CLIENT("      \"ul\": [{\n");
  PRINT_TO_CLIENT("        \"bwp3gpp:isInitialBwp\": true,\n");
  PRINT_TO_CLIENT("        \"bwp3gpp:numberOfRBs\": %ld,\n", current->ul_numrbs);
  PRINT_TO_CLIENT("        \"bwp3gpp:startRB\": %ld,\n", current->ul_startrb);
  PRINT_TO_CLIENT("        \"bwp3gpp:subCarrierSpacing\": %ld\n", current->ul_bwpscs);
  PRINT_TO_CLIENT("      }]\n");
  PRINT_TO_CLIENT("    },\n");

  PRINT_TO_CLIENT("    \"NRCELLDU\": {\n");
  PRINT_TO_CLIENT("      \"nrcelldu3gpp:ssbFrequency\": %ld,\n", current->ssbFrequency);
  PRINT_TO_CLIENT("      \"nrcelldu3gpp:arfcnDL\": %ld,\n", current->arfcnDL);
  PRINT_TO_CLIENT("      \"nrcelldu3gpp:bSChannelBwDL\": %ld,\n", current->bw_mhz);
  PRINT_TO_CLIENT("      \"nrcelldu3gpp:arfcnUL\": %ld,\n", current->arfcnUL);
  PRINT_TO_CLIENT("      \"nrcelldu3gpp:bSChannelBwUL\": %ld,\n", current->bw_mhz);
  PRINT_TO_CLIENT("      \"nrcelldu3gpp:nRPCI\": %ld,\n", current->pci);
  PRINT_TO_CLIENT("      \"nrcelldu3gpp:nRTAC\": %ld,\n", current->tac);
  PRINT_TO_CLIENT("      \"nrcelldu3gpp:mcc\": \"%03d\",\n", current->mcc);
  PRINT_TO_CLIENT("      \"nrcelldu3gpp:mnc\": \"%0*d\",\n", current->mnc_digit_length, current->mnc);
  PRINT_TO_CLIENT("      \"nrcelldu3gpp:sd\": %d,\n", current->sd);
  PRINT_TO_CLIENT("      \"nrcelldu3gpp:sst\": %d\n", current->sst);
  PRINT_TO_CLIENT("    },\n");
  PRINT_TO_CLIENT("    \"device\": {\n");
  PRINT_TO_CLIENT("      \"gnbId\": %d,\n", current->gNB_DU_id);
  PRINT_TO_CLIENT("      \"gnbName\": \"%s\",\n", current->gNB_DU_name);
  PRINT_TO_CLIENT("      \"vendor\": \"OpenAirInterface\"\n");
  PRINT_TO_CLIENT("    }\n");
  PRINT_TO_CLIENT("  },\n");

  PRINT_TO_CLIENT("  \"O1-Operational\": {\n");
  PRINT_TO_CLIENT("    \"frame-type\": \"%s\",\n", current->frame_type == 1 ? "tdd" : "fdd");
  PRINT_TO_CLIENT("    \"band-number\": %ld,\n", current->band);
  PRINT_TO_CLIENT("    \"num-ues\": %d,\n", num_ues);

  PRINT_TO_CLIENT("    \"ues\": [");
  for (int i = 0; i < num_ues; i++) {
    if (i != 0) PRINT_TO_CLIENT(", ");
    PRINT_TO_CLIENT("%d", current->ue_rlc_stats[i].rnti);
  }
  PRINT_TO_CLIENT("],\n");

  PRINT_TO_CLIENT("    \"load\": %d,\n", load);
  PRINT_TO_CLIENT("    \"ues-thp\": [");
  for (int i = 0; i < num_ues; i++) {
    if (i != 0) PRINT_TO_CLIENT(",\n      ");
    long dl_thr = (current->ue_rlc_stats[i].txpdu_bytes - last_total_dl[i]) * 8 / diff_msec;
    long ul_thr = (current->ue_rlc_stats[i].rxpdu_bytes - last_total_ul[i]) * 8 / diff_msec;
    PRINT_TO_CLIENT("{\"rnti\": %d, \"dl\": %ld, \"ul\": %ld}",
                  current->ue_rlc_stats[i].rnti, dl_thr, ul_thr);
  }
  PRINT_TO_CLIENT("\n    ]\n");
  PRINT_TO_CLIENT("  }\n");
  PRINT_TO_CLIENT("}\n");
  PRINT_TO_CLIENT("OK\n");
  #undef PRINT_TO_CLIENT
}
