/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*! \file telnetsrv_ci.c
 * \brief Implementation of telnet CI functions for gNB
 * \note  This file contains telnet-related functions specific to 5G gNB.
 */

#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "openair2/RRC/NR/rrc_gNB_UE_context.h"
#include "openair2/LAYER2/NR_MAC_gNB/nr_mac_gNB.h"
#include "openair2/LAYER2/nr_rlc/nr_rlc_oai_api.h"
#include "openair2/LAYER2/nr_rlc/nr_rlc_ue_manager.h"
#include "openair2/LAYER2/nr_rlc/nr_rlc_entity_am.h"
#include "openair2/LAYER2/NR_MAC_gNB/mac_proto.h"
#include "openair2/LAYER2/NR_MAC_gNB/mac_config.h"
#include "openair2/RRC/NR/rrc_gNB_mobility.h"
#include "openair3/NGAP/ngap_gNB_ue_context.h"
#include "intertask_interface.h"
#include "openair2/RRC/NR/rrc_gNB_du.h"
#define TELNETSERVERCODE
#include "telnetsrv.h"

#define ERROR_MSG_RET(mSG, aRGS...) do { prnt(mSG, ##aRGS); return -1; } while (0)

static inline void reset_current_client_pending_state(void) {
  int client_idx = telnetsrv_get_current_client_idx();
  if (client_idx >= 0 && client_idx < TELNET_MAX_CLIENTS) {
    telnet_clients[client_idx].pending_reestab = false;
    telnet_clients[client_idx].pending_reestab_count = false;
    telnet_clients[client_idx].pending_f1_ho = false;
    telnet_clients[client_idx].pending_du_by_ue_id = false;
    telnet_clients[client_idx].pending_ul_failure = false;
    telnet_clients[client_idx].pending_ue_release = false;
    telnet_clients[client_idx].pending_bwp_info = false;
    telnet_clients[client_idx].pending_bwp_id = -1;
  }
}

extern telnet_client_t telnet_clients[TELNET_MAX_CLIENTS];

int get_single_rnti(char *buf, int debug, telnet_printfunc_t prnt)
{
  UNUSED(debug);
  if (buf)
    ERROR_MSG_RET("no parameter allowed\n");

  reset_current_client_pending_state();
  MessageDef *msg_p = itti_alloc_new_message(TASK_TELNET, 0, MAC_GET_UE_RNTI);
  if (telnetsrv_get_current_client_idx() >= 0 && telnetsrv_get_current_client_idx() < TELNET_MAX_CLIENTS) {
    msg_p->ittiMsg.mac_get_ue_rnti.client_idx = telnetsrv_get_current_client_idx();
  }
  itti_send_msg_to_task(TASK_MAC_GNB, 0, msg_p);
  return 0;
}

int get_reestab_count(char *buf, int debug, telnet_printfunc_t prnt)
{
  UNUSED(debug);
  reset_current_client_pending_state();

  if (!buf) {
    if (telnetsrv_get_current_client_idx() >= 0 && telnetsrv_get_current_client_idx() < TELNET_MAX_CLIENTS) {
      telnet_clients[telnetsrv_get_current_client_idx()].pending_reestab_count = true;
    }
    MessageDef *msg_ue_rnti_p = itti_alloc_new_message(TASK_TELNET, 0, RRC_GET_SINGLE_UE_RNTI);
    if (telnetsrv_get_current_client_idx() >= 0 && telnetsrv_get_current_client_idx() < TELNET_MAX_CLIENTS) {
      msg_ue_rnti_p->ittiMsg.rrc_get_single_ue_rnti.client_idx = telnetsrv_get_current_client_idx();
    }
    itti_send_msg_to_task(TASK_RRC_GNB, 0, msg_ue_rnti_p);
  } else {
    ue_id_t ue_id = strtol(buf, NULL, 10);
    if (telnetsrv_get_current_client_idx() >= 0 && telnetsrv_get_current_client_idx() < TELNET_MAX_CLIENTS) {
      telnet_clients[telnetsrv_get_current_client_idx()].pending_reestab_count = true;
    }
    MessageDef *msg_ue_context_p = itti_alloc_new_message(TASK_TELNET, 0, RRC_GET_UE_CONTEXT_BY_UE_ID);
    msg_ue_context_p->ittiMsg.rrc_get_ue_context_by_ue_id.id = ue_id;
    if (telnetsrv_get_current_client_idx() >= 0 && telnetsrv_get_current_client_idx() < TELNET_MAX_CLIENTS) {
      msg_ue_context_p->ittiMsg.rrc_get_single_ue_rnti.client_idx = telnetsrv_get_current_client_idx();
    }
    itti_send_msg_to_task(TASK_RRC_GNB, 0, msg_ue_context_p);
  }

  return 0;
}

int trigger_reestab(char *buf, int debug, telnet_printfunc_t prnt)
{
  UNUSED(debug);
  reset_current_client_pending_state();

  if (!buf) {
    if (telnetsrv_get_current_client_idx() >= 0 && telnetsrv_get_current_client_idx() < TELNET_MAX_CLIENTS) {
      telnet_clients[telnetsrv_get_current_client_idx()].pending_reestab = true;
    }
    MessageDef *msg_p = itti_alloc_new_message(TASK_TELNET, 0, MAC_TRIGGER_REESTAB_FOR_FIRST_UE_RNTI);
    if (telnetsrv_get_current_client_idx() >= 0 && telnetsrv_get_current_client_idx() < TELNET_MAX_CLIENTS) {
      msg_p->ittiMsg.mac_trigger_reestab_for_first_rnti.client_idx = telnetsrv_get_current_client_idx();
    }
    itti_send_msg_to_task(TASK_MAC_GNB, 0, msg_p);
  } else {
    rnti_t rnti = strtol(buf, NULL, 16);
    if (rnti < 1 || rnti >= 0xfffe)
      ERROR_MSG_RET("RNTI needs to be [1,0xfffe]\n");
    nr_rlc_test_trigger_reestablishment(rnti);
    prnt("Reset RLC counters of UE RNTI %04x to trigger reestablishment\n", rnti);

  }
  return 0;
}

/** @brief Get connected DU by the UE ID */
int fetch_du_by_ue_id(char *buf, int debug, telnet_printfunc_t prnt)
{
  UNUSED(debug);
  reset_current_client_pending_state();

  if (!buf) {
    if (telnetsrv_get_current_client_idx() >= 0 && telnetsrv_get_current_client_idx() < TELNET_MAX_CLIENTS) {
      telnet_clients[telnetsrv_get_current_client_idx()].pending_du_by_ue_id = true;
    }
    MessageDef *msg_ue_rnti_p = itti_alloc_new_message(TASK_TELNET, 0, RRC_GET_SINGLE_UE_RNTI);
    if (telnetsrv_get_current_client_idx() >= 0 && telnetsrv_get_current_client_idx() < TELNET_MAX_CLIENTS) {
      msg_ue_rnti_p->ittiMsg.rrc_get_single_ue_rnti.client_idx = telnetsrv_get_current_client_idx();
    }
    itti_send_msg_to_task(TASK_RRC_GNB, 0, msg_ue_rnti_p);
  } else {
    ue_id_t ue_id = strtol(buf, NULL, 10);
    MessageDef *msg_p = itti_alloc_new_message(TASK_TELNET, 0, RRC_GET_DU_ID_BY_UE_ID);
    msg_p->ittiMsg.rrc_get_du_id_by_ue_id.ue_id = ue_id;
    if (telnetsrv_get_current_client_idx() >= 0 && telnetsrv_get_current_client_idx() < TELNET_MAX_CLIENTS) {
      msg_p->ittiMsg.rrc_get_du_id_by_ue_id.client_idx = telnetsrv_get_current_client_idx();
    }
    itti_send_msg_to_task(TASK_RRC_GNB, 0, msg_p);
  }

  return 0;
}

/**
 * @brief Trigger F1 handover for UE
 * @param buf: RRC UE ID or NULL for the first UE in list
 * @param debug: Debug flag
 * @param prnt: Print function
 * @return 0 on success, -1 on failure
 */
int rrc_gNB_trigger_f1_ho(char *buf, int debug, telnet_printfunc_t prnt)
{
  UNUSED(debug);
  reset_current_client_pending_state();

  if (buf) {
    long parsed_id = strtol(buf, NULL, 10);
    if (parsed_id < 0)
      ERROR_MSG_RET("UE ID needs to be [1,0xfffffe]\n");

    MessageDef *msg_p = itti_alloc_new_message(TASK_TELNET, 0, RRC_NR_F1_HO_TRIGGER);
    msg_p->ittiMsg.rrc_trigger_f1_ho.id = parsed_id;
    if (telnetsrv_get_current_client_idx() >= 0 && telnetsrv_get_current_client_idx() < TELNET_MAX_CLIENTS) {
      msg_p->ittiMsg.rrc_trigger_f1_ho.client_idx = telnetsrv_get_current_client_idx();
    }
    itti_send_msg_to_task(TASK_RRC_GNB, 0, msg_p);
    prnt("RRC F1 handover triggered for UE %u\n", (ue_id_t)parsed_id);
  } else {
    if (telnetsrv_get_current_client_idx() >= 0 && telnetsrv_get_current_client_idx() < TELNET_MAX_CLIENTS) {
      telnet_clients[telnetsrv_get_current_client_idx()].pending_f1_ho = true;
    }
    MessageDef *msg_p = itti_alloc_new_message(TASK_TELNET, 0, RRC_GET_SINGLE_UE_RNTI);
    if (telnetsrv_get_current_client_idx() >= 0 && telnetsrv_get_current_client_idx() < TELNET_MAX_CLIENTS) {
      msg_p->ittiMsg.rrc_get_single_ue_rnti.client_idx = telnetsrv_get_current_client_idx();
    }
    itti_send_msg_to_task(TASK_RRC_GNB, 0, msg_p);
  }
  return 0;
}

/** @brief Trigger N2 handover for UE
 *  @param buf: Neighbour PCI, SCell PCI, RRC UE ID
 *  @param debug: Debug flag
 *  @param prnt: Print function
 *  @return 0 on success, -1 on failure */
int rrc_gNB_trigger_n2_ho(char *buf, int debug, telnet_printfunc_t prnt)
{
  UNUSED(debug);
  if (!buf) {
    ERROR_MSG_RET("Please provide neighbour cell id and ue id\n");
  } else {
    // Parse neighbour cell PCI
    char *token = strtok(buf, ",");
    if (!token) {
      ERROR_MSG_RET("Invalid input. Expected format: Neighbour PCI, ueId\n");
    }
    uint32_t neighbour_pci = strtol(token, NULL, 10);

    // Parse ueId
    token = strtok(NULL, ",");
    if (!token) {
      ERROR_MSG_RET("Missing UE ID\n");
    }
    uint32_t ueId = strtol(token, NULL, 10);

    MessageDef *msg_ho_p = itti_alloc_new_message(TASK_TELNET, 0, RRC_NR_N2_HO_TRIGGER);
    msg_ho_p->ittiMsg.rrc_trigger_n2_ho.id = ueId;
    msg_ho_p->ittiMsg.rrc_trigger_n2_ho.neighbour_pci = neighbour_pci;
    if (telnetsrv_get_current_client_idx() >= 0 && telnetsrv_get_current_client_idx() < TELNET_MAX_CLIENTS) {
      msg_ho_p->ittiMsg.rrc_trigger_n2_ho.client_idx = telnetsrv_get_current_client_idx();
    }
    itti_send_msg_to_task(TASK_RRC_GNB, 0, msg_ho_p);
    // Print success message
    prnt("RRC N2 handover triggered for UE %u with neighbour cell id %u\n",
         ueId,
         neighbour_pci);
  }
  return 0;
}

int force_ul_failure(char *buf, int debug, telnet_printfunc_t prnt)
{
  UNUSED(debug);
  reset_current_client_pending_state();

  if (!buf) {
    if (telnetsrv_get_current_client_idx() >= 0 && telnetsrv_get_current_client_idx() < TELNET_MAX_CLIENTS) {
      telnet_clients[telnetsrv_get_current_client_idx()].pending_ul_failure = true;
    }
    MessageDef *msg_p = itti_alloc_new_message(TASK_TELNET, 0, MAC_GET_UE_RNTI);
    if (telnetsrv_get_current_client_idx() >= 0 && telnetsrv_get_current_client_idx() < TELNET_MAX_CLIENTS) {
      msg_p->ittiMsg.mac_get_ue_rnti.client_idx = telnetsrv_get_current_client_idx();
    }
    itti_send_msg_to_task(TASK_MAC_GNB, 0, msg_p);
    return 0;
  } else {
    rnti_t rnti = strtol(buf, NULL, 16);
    if (rnti < 1 || rnti >= 0xfffe)
      ERROR_MSG_RET("RNTI needs to be [1,0xfffe]\n");

    MessageDef *msg_p = itti_alloc_new_message(TASK_TELNET, 0, MAC_FORCE_UL_FAILURE);
    msg_p->ittiMsg.mac_force_ul_failure.rnti = rnti;
    if (telnetsrv_get_current_client_idx() >= 0 && telnetsrv_get_current_client_idx() < TELNET_MAX_CLIENTS) {
      msg_p->ittiMsg.mac_force_ul_failure.client_idx = telnetsrv_get_current_client_idx();
    }
    itti_send_msg_to_task(TASK_MAC_GNB, 0, msg_p);
    return 0;
  }
}

int force_ue_release(char *buf, int debug, telnet_printfunc_t prnt)
{
  UNUSED(debug);
  reset_current_client_pending_state();

  if (!buf) {
    if (telnetsrv_get_current_client_idx() >= 0 && telnetsrv_get_current_client_idx() < TELNET_MAX_CLIENTS) {
      telnet_clients[telnetsrv_get_current_client_idx()].pending_ue_release = true;
    }
    MessageDef *msg_p = itti_alloc_new_message(TASK_TELNET, 0, MAC_GET_UE_RNTI);
    if (telnetsrv_get_current_client_idx() >= 0 && telnetsrv_get_current_client_idx() < TELNET_MAX_CLIENTS) {
      msg_p->ittiMsg.mac_get_ue_rnti.client_idx = telnetsrv_get_current_client_idx();
    }
    itti_send_msg_to_task(TASK_MAC_GNB, 0, msg_p);
    return 0;
  } else {
    rnti_t rnti = strtol(buf, NULL, 16);
    if (rnti < 1 || rnti >= 0xfffe)
      ERROR_MSG_RET("RNTI needs to be [1,0xfffe]\n");

    MessageDef *msg_ul_p = itti_alloc_new_message(TASK_TELNET, 0, MAC_FORCE_UL_FAILURE);
    msg_ul_p->ittiMsg.mac_force_ul_failure.rnti = rnti;
    if (telnetsrv_get_current_client_idx() >= 0 && telnetsrv_get_current_client_idx() < TELNET_MAX_CLIENTS) {
      msg_ul_p->ittiMsg.mac_force_ul_failure.client_idx = telnetsrv_get_current_client_idx();
    }
    itti_send_msg_to_task(TASK_MAC_GNB, 0, msg_ul_p);

    MessageDef *msg_p = itti_alloc_new_message(TASK_TELNET, 0, MAC_FORCE_UE_RELEASE);
    msg_p->ittiMsg.mac_force_ue_release.rnti = rnti;
    if (telnetsrv_get_current_client_idx() >= 0 && telnetsrv_get_current_client_idx() < TELNET_MAX_CLIENTS) {
      msg_p->ittiMsg.mac_force_ue_release.client_idx = telnetsrv_get_current_client_idx();
    }
    itti_send_msg_to_task(TASK_MAC_GNB, 0, msg_p);
    return 0;
  }
}

static int get_current_bwp(char *buf, int debug, telnet_printfunc_t prnt)
{
  UNUSED(debug);
  reset_current_client_pending_state();

  if (!buf) {
    if (telnetsrv_get_current_client_idx() >= 0 && telnetsrv_get_current_client_idx() < TELNET_MAX_CLIENTS) {
      telnet_clients[telnetsrv_get_current_client_idx()].pending_bwp_info = true;
    }
    MessageDef *msg_p = itti_alloc_new_message(TASK_TELNET, 0, MAC_GET_UE_RNTI);
    if (telnetsrv_get_current_client_idx() >= 0 && telnetsrv_get_current_client_idx() < TELNET_MAX_CLIENTS) {
      msg_p->ittiMsg.mac_get_ue_rnti.client_idx = telnetsrv_get_current_client_idx();
    }
    itti_send_msg_to_task(TASK_MAC_GNB, 0, msg_p);
    return 0;
  } else {
    rnti_t rnti = strtol(buf, NULL, 16);
    if (rnti < 1 || rnti >= 0xfffe)
      ERROR_MSG_RET("RNTI needs to be [1,0xfffe]\n");

    MessageDef *msg_p = itti_alloc_new_message(TASK_TELNET, 0, MAC_GET_UE_BWP_INFO);
    msg_p->ittiMsg.mac_get_ue_bwp_info.rnti = rnti;
    if (telnetsrv_get_current_client_idx() >= 0 && telnetsrv_get_current_client_idx() < TELNET_MAX_CLIENTS) {
      msg_p->ittiMsg.mac_get_ue_bwp_info.client_idx = telnetsrv_get_current_client_idx();
    }
    itti_send_msg_to_task(TASK_MAC_GNB, 0, msg_p);
    return 0;
  }
}

/** @brief Trigger NGAP PDU Session Release for one or more PDU sessions associated with a UE ID/
 *  Syntax: trigger_pdu_session_release [ue_id=gNB_ue_ngap_id(int,opt)],pdusession_id(int)[,pdusession_id(int)...]
 *  - If the gNB_ue_ngap_id is omitted, it is fetched from the only UE present in the RRC layer
 *  - At least one valid PDU session ID must be provided
 * @param[in] buf   Comma-separated input string: [ue_id=gNB_ue_ngap_id(int,opt)],PDU1[,PDU2,...]
 * @param[in] debug Not used.
 * @param[in] prnt  Callback for telnet output printing.
 * @return 0 on success; negative value on error. */
static int trigger_ngap_pdu_session_release(char *buf, int debug, telnet_printfunc_t prnt)
{
  UNUSED(debug);
  if (buf == NULL) {
    ERROR_MSG_RET("Missing input. Usage: trigger_pdu_session_release [ue_id=gNB_ue_ngap_id(int,opt)],pdusession_id(int)[,pdusession_id(int)...]\n");
  }

  char *tokens[NR_MAX_NB_PDU_SESSIONS + 1];
  int count = 0;

  for (char *tok = strtok(buf, ","); tok != NULL && count < (int)sizeofArray(tokens); tok = strtok(NULL, ",")) {
    tokens[count++] = tok;
  }

  if (count < 1) {
    ERROR_MSG_RET("Invalid input. Usage: trigger_pdu_session_release [ue_id=gNB_ue_ngap_id(int,opt)],pdusession_id(int)[,pdusession_id(int)...]\n");
  }

  int gNB_ue_ngap_id = -1;
  int pdu_start_index = 0;

  if (strncmp(tokens[0], "ue_id=", 6) == 0) {
    gNB_ue_ngap_id = atoi(tokens[0] + 6);
    pdu_start_index = 1;
  } else {
    MessageDef *msg_ue_rnti_p = itti_alloc_new_message(TASK_TELNET, 0, RRC_GET_SINGLE_UE_RNTI);
    if (msg_ue_rnti_p == NULL) {
      LOG_E(NGAP, "Failed to allocate message for RRC_GET_SINGLE_UE_RNTI\n");
      ERROR_MSG_RET("Need to provide ue_id for non-blocking mode\n");
    }
    msg_ue_rnti_p->ittiMsg.rrc_get_single_ue_rnti.client_idx = telnetsrv_get_current_client_idx();
    itti_send_msg_to_task(TASK_RRC_GNB, 0, msg_ue_rnti_p);
    ERROR_MSG_RET("Need to provide ue_id for non-blocking mode\n");
  }

  if (pdu_start_index >= count) {
    ERROR_MSG_RET("No pdusession_id(int) provided\n");
  }

  if (gNB_ue_ngap_id < 0) {
    ERROR_MSG_RET("Need to provide gNB_ue_ngap_id in non-blocking mode\n");
  }

  MessageDef *message_p = itti_alloc_new_message(TASK_TELNET, 0, NGAP_PDUSESSION_RELEASE_COMMAND);
  ngap_pdusession_release_command_t *msg = &NGAP_PDUSESSION_RELEASE_COMMAND(message_p);
  memset(msg, 0, sizeof(*msg));

  msg->gNB_ue_ngap_id = gNB_ue_ngap_id;

  int nb_sessions = 0;
  for (int i = pdu_start_index; i < count; ++i) {
    int sid = atoi(tokens[i]);
    if (sid < 1 || sid > 255) {
      ERROR_MSG_RET("Invalid pdusession_id(int): %s (must be between 1 and 255)\n", tokens[i]);
    }
    msg->pdusession_ids[nb_sessions++] = sid;
  }

  msg->nb_pdusessions_torelease = nb_sessions;

  if (prnt) {
    prnt("Triggering NGAP PDU Session Release for gNB_ue_ngap_id=%d: releasing pdusession_id=", gNB_ue_ngap_id);
    for (int i = 0; i < nb_sessions; ++i) {
      prnt(" %d,", msg->pdusession_ids[i]);
    }
    prnt("\n");
  }

  itti_send_msg_to_task(TASK_RRC_GNB, 0, message_p);
  return 0;
}

static int trigger_bwp_switch(char *buf, int debug, telnet_printfunc_t prnt)
{
  UNUSED(debug);
  reset_current_client_pending_state();

  if (!buf)
    ERROR_MSG_RET("need BWP ID and RNTI\n");
  char *sbwpId = strtok(buf, " ");
  if (!sbwpId) {
    ERROR_MSG_RET("Missing BWP ID\n");
  }
  int bwpId = atoi(sbwpId);
  char *srnti = strtok(NULL, " ");

  if (!srnti || !srnti[0]) {
    if (telnetsrv_get_current_client_idx() >= 0 && telnetsrv_get_current_client_idx() < TELNET_MAX_CLIENTS) {
      telnet_clients[telnetsrv_get_current_client_idx()].pending_bwp_id = bwpId;
    }
    MessageDef *msg_p = itti_alloc_new_message(TASK_TELNET, 0, MAC_GET_UE_RNTI);
    if (telnetsrv_get_current_client_idx() >= 0 && telnetsrv_get_current_client_idx() < TELNET_MAX_CLIENTS) {
      msg_p->ittiMsg.mac_get_ue_rnti.client_idx = telnetsrv_get_current_client_idx();
    }
    itti_send_msg_to_task(TASK_MAC_GNB, 0, msg_p);
    return 0;
  } else {
    rnti_t rnti = strtol(srnti, NULL, 16);
    if (rnti < 1 || rnti >= 0xfffe)
      ERROR_MSG_RET("RNTI needs to be [1,0xfffe]\n");

    if (telnetsrv_get_current_client_idx() >= 0 && telnetsrv_get_current_client_idx() < TELNET_MAX_CLIENTS) {
      telnet_clients[telnetsrv_get_current_client_idx()].pending_bwp_id = -1;
    }

    prnt("bwpId %d rnti %04x\n", bwpId, rnti);
    if (!nr_trigger_bwp_switch(rnti, bwpId)) {
      ERROR_MSG_RET("failed trigger BWP switch for UE %04x BWP ID %d\n", rnti, bwpId);
    } else {
      prnt("triggered BWP switch to BWP ID %d for UE %04x\n", bwpId, rnti);
      return 0;
    }
  }
}

static int set_pusch_target_snr(char *buf, int debug, telnet_printfunc_t prnt)
{
  UNUSED(debug);
  if (!buf)
    ERROR_MSG_RET("need an SNR to read\n");

  char *end;
  long new_snr = strtol(buf, &end, 0);
  if (*end != 0)
    ERROR_MSG_RET("error: could not parse number in '%s'\n", buf);

  MessageDef *msg_p = itti_alloc_new_message(TASK_TELNET, 0, MAC_SET_PUSCH_TARGET_SNR);
  if (msg_p == NULL) {
    LOG_E(NR_MAC, "Failed to allocate message for MAC_SET_PUSCH_TARGET_SNR\n");
    return -1;
  }
  msg_p->ittiMsg.mac_set_pusch_target_snr.target_snrx10 = new_snr * 10;
  msg_p->ittiMsg.mac_set_pusch_target_snr.client_idx = telnetsrv_get_current_client_idx();
  itti_send_msg_to_task(TASK_MAC_GNB, 0, msg_p);
  prnt("set new PUSCH target SNR %d for all UEs\n", new_snr);

  return 0;
}

static telnetshell_cmddef_t cicmds[] = {
    {"get_single_rnti", "", get_single_rnti},
    {"force_reestab", "[rnti(hex,opt)]", trigger_reestab},
    {"get_reestab_count", "[rnti(hex,opt)]", get_reestab_count},
    {"force_ue_release", "[rnti(hex,opt)]", force_ue_release},
    {"force_ul_failure", "[rnti(hex,opt)]", force_ul_failure},
    {"trigger_f1_ho", "[rrc_ue_id(int,opt)]", rrc_gNB_trigger_f1_ho},
    {"fetch_du_by_ue_id", "[rrc_ue_id(int,opt)]", fetch_du_by_ue_id},
    {"get_current_bwp", "[rnti(hex,opt)]", get_current_bwp},
    {"trigger_bwp_switch", "newBWPId [rnti(hex,opt)]", trigger_bwp_switch},
    {"trigger_n2_ho", "[neighbour_pci(uint32_t),ueId(uint32_t)]", rrc_gNB_trigger_n2_ho},
    {"set_pusch_target_snr", "[somelongSNR(dec)]", set_pusch_target_snr},
    {"pdu_session_release", "[gNB_ue_ngap_id(int,opt)]", trigger_ngap_pdu_session_release},
    {"", "", NULL},
};

static telnetshell_vardef_t civars[] = {

  {"", 0, 0, NULL}
};

void add_ci_cmds(void) {
  add_telnetcmd("ci", civars, cicmds);
}
