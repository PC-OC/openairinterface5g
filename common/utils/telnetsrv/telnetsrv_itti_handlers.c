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

extern bool telnetsrv_pending_reestab;
extern bool telnetsrv_o1_running;
extern bool telnetsrv_pending_reestab_count;
extern bool telnetsrv_pending_f1_ho;
extern bool telnetsrv_pending_du_by_ue_id;
extern bool telnetsrv_pending_ul_failure;
extern bool telnetsrv_pending_ue_release;
extern bool telnetsrv_pending_bwp_info;
extern int telnetsrv_pending_bwp_id;

void telnetsrv_handle_rrc_get_single_ue_rnti(Rrc_get_single_ue_rnti *rrc_msg)
{
  if (rrc_msg->has_rrc) {
    if (rrc_msg->is_single) {
      rnti_t rnti = rrc_msg->rnti;
      ue_id_t ue_id = rrc_msg->id;
      int reestab = rrc_msg->ue_reestablishment_counter;
      int reconfig = rrc_msg->ue_reconfiguration_counter;

      if (telnetsrv_pending_du_by_ue_id) {
        MessageDef *msg_p = itti_alloc_new_message(TASK_TELNET, 0, RRC_GET_DU_ID_BY_UE_ID);
        msg_p->ittiMsg.rrc_get_du_id_by_ue_id.ue_id = ue_id;
        itti_send_msg_to_task(TASK_RRC_GNB, 0, msg_p);
      }
      else if (telnetsrv_pending_f1_ho) {
        MessageDef *msg_p = itti_alloc_new_message(TASK_TELNET, 0, RRC_NR_F1_HO_TRIGGER);
        msg_p->ittiMsg.rrc_trigger_f1_ho.id = ue_id;
        itti_send_msg_to_task(TASK_RRC_GNB, 0, msg_p);
        client_printf("RRC F1 handover triggered for UE %u\n", ue_id);
      }
      else if (telnetsrv_pending_reestab) {
        nr_rlc_test_trigger_reestablishment(ue_id);
        if (rnti != ue_id) {
          nr_rlc_test_trigger_reestablishment(rnti);
        }
        client_printf("Reset RLC counters of UE RNTI %04x to trigger reestablishment\n", rnti);
      }
      else if (telnetsrv_pending_reestab_count) {
        client_printf("UE RNTI %04x reestab %d reconfig %d\n", rnti, reestab, reconfig);
      }
      else {
        client_printf("single UE RNTI %04x\n", rnti);
      }
    } else {
      client_printf("UE count is not exactly one in RRC\n");
    }
  } else {
    client_printf("no RRC present\n");
  }

  telnetsrv_pending_reestab = false;
  telnetsrv_pending_reestab_count = false;
  telnetsrv_pending_f1_ho = false;
  telnetsrv_pending_du_by_ue_id = false;
}

void telnetsrv_handle_mac_trigger_reestab_for_first_rnti(Mac_trigger_reestab_for_first_rnti *mac_msg)
{
  if (mac_msg->has_rnti) {
    client_printf("Reset RLC counters of UE RNTI %04x to trigger reestablishment\n", mac_msg->rnti);
  } else {
    client_printf("Failed to trigger reestablishment: no UE found\n");
  }
  telnetsrv_pending_reestab = false;
}

void telnetsrv_handle_rrc_get_ue_context_by_ue_id(Rrc_get_single_ue_rnti *rrc_msg)
{
  if (rrc_msg->is_single) {
    client_printf("UE RNTI %04x reestab %d reconfig %d\n",
                rrc_msg->rnti,
                rrc_msg->ue_reestablishment_counter,
                rrc_msg->ue_reconfiguration_counter);
  } else {
    client_printf("could not find UE with ue_id in RRC\n");
  }
  telnetsrv_pending_reestab_count = false;
}

void telnetsrv_handle_rrc_get_du_id_by_ue_id(Rrc_get_du_id_by_ue_id *rrc_msg)
{
  if (!rrc_msg->no_du) {
    client_printf("gNB_DU_id %d is connected to ue_id %d\n",
                rrc_msg->du_id,
                rrc_msg->ue_id);
  } else {
    client_printf("No DU connected or no UE found with the requested ue_id.\n");
  }
}

void telnetsrv_handle_rrc_check_ue_context(Rrc_check_ue_context *rrc_msg)
{
  if (!rrc_msg->check) {
    client_printf("UE with id %d not found\n", rrc_msg->id);
  }
}

void telnetsrv_handle_rrc_get_ue_context_by_rnti_any_du(Rrc_get_ue_context_by_rnti_any_du *rrc_msg)
{
  if (!rrc_msg->ue_context_exists) {
    client_printf("could not find UE with RNTI %04x\n", rrc_msg->rnti);
  } else {
    client_printf("UE with RNTI %04x found\n", rrc_msg->rnti);
  }
}

void telnetsrv_handle_rrc_get_ngap_ue_id(Rrc_get_ngap_ue_id *rrc_msg)
{
  client_printf("NGAP UE ID: gNB_ue_ngap_id=%u, amf_ue_ngap_id=%u\n",
              rrc_msg->gNB_ue_ngap_id,
              rrc_msg->amf_ue_ngap_id);
}

void telnetsrv_handle_rrc_gnb_generate_rrcrelease(Rrc_gnb_generate_rrcrelease *rrc_msg)
{
  UNUSED(rrc_msg);
  client_printf("RRC Release triggered\n");
}

void telnetsrv_handle_rrc_gnb_generate_rrcrelease_all(Rrc_gnb_generate_rrcrelease_all *rrc_msg)
{
  client_printf("RRC Release triggered for all UEs (%d releases)\n",
              rrc_msg->nb_releases);
}

void telnetsrv_handle_rrc_gnb_trigger_ue_context_release_req(Rrc_gnb_trigger_ue_context_release_req *rrc_msg)
{
  client_printf("Sent NGAP UE Context Release Request for UE ID %u (rrc_ue_context=%s, ngap_ue_context=%s)\n",
              rrc_msg->ue_id,
              rrc_msg->rrc_ue_context ? "true" : "false",
              rrc_msg->ngap_ue_context ? "true" : "false");
}

void telnetsrv_handle_mac_get_ue_rnti(Mac_get_ue_rnti *mac_msg)
{
  if (mac_msg->has_mac) {
    rnti_t rnti = mac_msg->rnti;

    if (telnetsrv_pending_bwp_id != -1) {
      int bwpId = telnetsrv_pending_bwp_id;
      client_printf("bwpId %d rnti %04x\n", bwpId, rnti);
      if (!nr_trigger_bwp_switch(rnti, bwpId)) {
        client_printf("failed trigger BWP switch for UE %04x BWP ID %d\n", rnti, bwpId);
      } else {
        client_printf("triggered BWP switch to BWP ID %d for UE %04x\n", bwpId, rnti);
      }
    }
    else if (telnetsrv_pending_bwp_info) {
      MessageDef *msg_p = itti_alloc_new_message(TASK_TELNET, 0, MAC_GET_UE_BWP_INFO);
      msg_p->ittiMsg.mac_get_ue_bwp_info.rnti = rnti;
      itti_send_msg_to_task(TASK_MAC_GNB, 0, msg_p);
    }
    else if (telnetsrv_pending_ue_release) {
      MessageDef *msg_ul_p = itti_alloc_new_message(TASK_TELNET, 0, MAC_FORCE_UL_FAILURE);
      msg_ul_p->ittiMsg.mac_force_ul_failure.rnti = rnti;
      itti_send_msg_to_task(TASK_MAC_GNB, 0, msg_ul_p);

      MessageDef *msg_p = itti_alloc_new_message(TASK_TELNET, 0, MAC_FORCE_UE_RELEASE);
      msg_p->ittiMsg.mac_force_ue_release.rnti = rnti;
      itti_send_msg_to_task(TASK_MAC_GNB, 0, msg_p);
      client_printf("Reset RLC counters and triggered UE release for UE RNTI %04x\n", rnti);
    }
    else if (telnetsrv_pending_ul_failure) {
      MessageDef *msg_p = itti_alloc_new_message(TASK_TELNET, 0, MAC_FORCE_UL_FAILURE);
      msg_p->ittiMsg.mac_force_ul_failure.rnti = rnti;
      itti_send_msg_to_task(TASK_MAC_GNB, 0, msg_p);
    }
    else if (telnetsrv_pending_reestab) {
      nr_rlc_test_trigger_reestablishment(rnti);
      client_printf("Reset RLC counters of UE RNTI %04x to trigger reestablishment\n", rnti);
    }
    else {
      client_printf("single UE RNTI %04x\n", rnti);
    }
  } else {
    client_printf("no MAC/RLC present\n");
  }

  // Reset pending flags
  telnetsrv_pending_reestab = false;
  telnetsrv_pending_ul_failure = false;
  telnetsrv_pending_ue_release = false;
  telnetsrv_pending_bwp_info = false;
  telnetsrv_pending_bwp_id = -1;
}

void telnetsrv_handle_mac_get_ue_rnti_by_uid(Mac_get_ue_rnti_by_uid *mac_msg)
{
  if (mac_msg->has_mac) {
    client_printf("UE RNTI %04x for UID %u\n",
                mac_msg->rnti,
                mac_msg->uid);
  } else {
    client_printf("no MAC/RLC present for UID %u\n", mac_msg->uid);
  }
}

void telnetsrv_handle_mac_get_ue_bwp_info(Mac_get_ue_bwp_info *mac_msg)
{
  rnti_t rnti = mac_msg->rnti;
  int dl_bwp = mac_msg->dl_bwp_id;
  int ul_bwp = mac_msg->ul_bwp_id;

  if (dl_bwp >= 0 && ul_bwp >= 0) {
    const char *dl_bwp_text = dl_bwp > 0 ? "dedicated" : "initial";
    const char *ul_bwp_text = ul_bwp > 0 ? "dedicated" : "initial";
    client_printf("UE %04x DL BWP ID %d (%s) UL BWP ID %d (%s)\n",
                rnti, dl_bwp, dl_bwp_text, ul_bwp, ul_bwp_text);
  } else {
    client_printf("could not find UE with RNTI %04x\n", rnti);
  }
}

void telnetsrv_handle_mac_force_ul_failure(Mac_force_ul_failure *mac_msg)
{
  UNUSED(mac_msg);
  client_printf("UL failure triggered\n");
}

void telnetsrv_handle_mac_force_ue_release(Mac_force_ue_release *mac_msg)
{
  UNUSED(mac_msg);
  client_printf("UE release triggered\n");
}

void telnetsrv_handle_mac_set_pusch_target_snr(Mac_set_pusch_target_snr *mac_msg)
{
  UNUSED(mac_msg);
  client_printf("PUSCH target SNR set\n");
}

void telnetsrv_handle_mac_set_bwconfig(Mac_set_bwconfig *mac_msg)
{
  UNUSED(mac_msg);
  client_printf("OK\n");
}

void telnetsrv_handle_mac_stop_modem(Mac_stop_modem *mac_msg)
{
  UNUSED(mac_msg);
  telnetsrv_o1_running = false;
  client_printf("OK\n");
}

void telnetsrv_handle_mac_get_o1_stats(Mac_get_o1_stats *mac_msg)
{
  Mac_get_o1_stats *current = mac_msg;

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

  client_printf("{\n");
  client_printf("  \"o1-config\": {\n");

  client_printf("    \"BWP\": {\n");
  client_printf("      \"dl\": [{\n");
  client_printf("        \"bwp3gpp:isInitialBwp\": true,\n");
  client_printf("        \"bwp3gpp:numberOfRBs\": %ld,\n", current->dl_numrbs);
  client_printf("        \"bwp3gpp:startRB\": %ld,\n", current->dl_startrb);
  client_printf("        \"bwp3gpp:subCarrierSpacing\": %ld\n", current->dl_bwpscs);
  client_printf("      }],\n");
  client_printf("      \"ul\": [{\n");
  client_printf("        \"bwp3gpp:isInitialBwp\": true,\n");
  client_printf("        \"bwp3gpp:numberOfRBs\": %ld,\n", current->ul_numrbs);
  client_printf("        \"bwp3gpp:startRB\": %ld,\n", current->ul_startrb);
  client_printf("        \"bwp3gpp:subCarrierSpacing\": %ld\n", current->ul_bwpscs);
  client_printf("      }]\n");
  client_printf("    },\n");

  client_printf("    \"NRCELLDU\": {\n");
  client_printf("      \"nrcelldu3gpp:ssbFrequency\": %ld,\n", current->ssbFrequency);
  client_printf("      \"nrcelldu3gpp:arfcnDL\": %ld,\n", current->arfcnDL);
  client_printf("      \"nrcelldu3gpp:bSChannelBwDL\": %ld,\n", current->bw_mhz);
  client_printf("      \"nrcelldu3gpp:arfcnUL\": %ld,\n", current->arfcnUL);
  client_printf("      \"nrcelldu3gpp:bSChannelBwUL\": %ld,\n", current->bw_mhz);
  client_printf("      \"nrcelldu3gpp:nRPCI\": %ld,\n", current->pci);
  client_printf("      \"nrcelldu3gpp:nRTAC\": %ld,\n", current->tac);
  client_printf("      \"nrcelldu3gpp:mcc\": \"%03d\",\n", current->mcc);
  client_printf("      \"nrcelldu3gpp:mnc\": \"%0*d\",\n", current->mnc_digit_length, current->mnc);
  client_printf("      \"nrcelldu3gpp:sd\": %d,\n", current->sd);
  client_printf("      \"nrcelldu3gpp:sst\": %d\n", current->sst);
  client_printf("    },\n");
  client_printf("    \"device\": {\n");
  client_printf("      \"gnbId\": %d,\n", current->gNB_DU_id);
  client_printf("      \"gnbName\": \"%s\",\n", current->gNB_DU_name);
  client_printf("      \"vendor\": \"OpenAirInterface\"\n");
  client_printf("    }\n");
  client_printf("  },\n");

  client_printf("  \"O1-Operational\": {\n");
  client_printf("    \"frame-type\": \"%s\",\n", current->frame_type == 1 ? "tdd" : "fdd");
  client_printf("    \"band-number\": %ld,\n", current->band);
  client_printf("    \"num-ues\": %d,\n", num_ues);

  client_printf("    \"ues\": [");
  for (int i = 0; i < num_ues; i++) {
    if (i != 0) client_printf(", ");
    client_printf("%d", current->ue_rlc_stats[i].rnti);
  }
  client_printf("],\n");

  client_printf("    \"load\": %d,\n", load);
  client_printf("    \"ues-thp\": [");
  for (int i = 0; i < num_ues; i++) {
    if (i != 0) client_printf(",\n      ");
    long dl_thr = (current->ue_rlc_stats[i].txpdu_bytes - last_total_dl[i]) * 8 / diff_msec;
    long ul_thr = (current->ue_rlc_stats[i].rxpdu_bytes - last_total_ul[i]) * 8 / diff_msec;
    client_printf("{\"rnti\": %d, \"dl\": %ld, \"ul\": %ld}",
                  current->ue_rlc_stats[i].rnti, dl_thr, ul_thr);
  }
  client_printf("\n    ]\n");
  client_printf("  }\n");
  client_printf("}\n");
  client_printf("OK\n");
}
