/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "./rrc_telnet_tasks.h"
#include <NR_DL-DCCH-Message.h>
#include "NR_RRCReconfiguration.h"
#include "openair2/LAYER2/NR_MAC_COMMON/nr_mac.h"
#include "openair2/F1AP/f1ap_ids.h"
#include "openair2/F1AP/f1ap_common.h"
#include "oai_asn1.h"
#include "nr_rrc_proto.h"
#include "rrc_gNB_NGAP.h"
#include "rrc_gNB_du.h"
#include "openair2/LAYER2/nr_pdcp/nr_pdcp_oai_api.h"
#include "openair2/LAYER2/nr_pdcp/nr_pdcp_ue_manager.h"
#include "openair2/LAYER2/nr_pdcp/nr_pdcp_entity.h"

typedef struct deliver_dl_rrc_message_data_s {
  const gNB_RRC_INST *rrc;
  f1ap_dl_rrc_message_t *dl_rrc;
  sctp_assoc_t assoc_id;
} deliver_dl_rrc_message_data_t;

static void rrc_deliver_dl_rrc_message(void *deliver_pdu_data, ue_id_t ue_id, int srb_id, char *buf, int size, int sdu_id)
{
  UNUSED(ue_id);
  UNUSED(sdu_id);
  DevAssert(deliver_pdu_data != NULL);
  deliver_dl_rrc_message_data_t *data = (deliver_dl_rrc_message_data_t *)deliver_pdu_data;
  data->dl_rrc->rrc_container = (uint8_t *)buf;
  data->dl_rrc->rrc_container_length = size;
  DevAssert(data->dl_rrc->srb_id == srb_id);
  data->rrc->mac_rrc.dl_rrc_message_transfer(data->assoc_id, data->dl_rrc);
}

static void rrc_get_single_ue_rnti_helper(MessageDef **msg_p, instance_t instance)
{
  if(!RC.nrrrc){
    (*msg_p)->ittiMsg.rrc_get_single_ue_rnti.has_rrc = false;
    return;
  }
  if (RC.nrrrc[instance] != NULL) {
    rrc_gNB_ue_context_t *ue = NULL;
    int count = 0;

    RB_FOREACH (ue, rrc_nr_ue_tree_s, &RC.nrrrc[instance]->rrc_ue_head) {
      count++;

      if (count == 1) {
        (*msg_p)->ittiMsg.rrc_get_single_ue_rnti.rnti = ue->ue_context.rnti;
        (*msg_p)->ittiMsg.rrc_get_single_ue_rnti.id = ue->ue_context.rrc_ue_id;
        (*msg_p)->ittiMsg.rrc_get_single_ue_rnti.ue_reestablishment_counter = ue->ue_context.ue_reestablishment_counter;
        (*msg_p)->ittiMsg.rrc_get_single_ue_rnti.ue_reconfiguration_counter = ue->ue_context.ue_reconfiguration_counter;
        (*msg_p)->ittiMsg.rrc_get_single_ue_rnti.is_single = true;
        (*msg_p)->ittiMsg.rrc_get_single_ue_rnti.has_rrc = true;
      }

      if (count >= 2) {
        (*msg_p)->ittiMsg.rrc_get_single_ue_rnti.is_single = false;
        break;
      }
    }
  }
}

void rrc_get_single_ue_rnti(MessageDef *msg_p, instance_t instance)
{
  MessageDef *resp_p = itti_alloc_new_message(TASK_RRC_GNB, 0, RRC_GET_SINGLE_UE_RNTI);
  rrc_get_single_ue_rnti_helper(&resp_p, instance);
  itti_send_msg_to_task(TASK_TELNET, 0, resp_p);
}

void rrc_force_reconfiguration_failure(MessageDef *msg_p, instance_t instance) {
  rnti_t rnti = msg_p->ittiMsg.rrc_force_reconfiguration_failure.rnti;
  rrc_gNB_ue_context_t *ue_context = rrc_gNB_get_ue_context_by_rnti_any_du(RC.nrrrc[instance], rnti);
  if (!ue_context) {
    LOG_E(NR_RRC, "UE with RNTI %04x not found\n", rnti);
    return;
  }
  gNB_RRC_UE_t *ue_p = &ue_context->ue_context;

  if (!ue_p->Srb[SRB1].Active) {
    LOG_E(NR_RRC, "UE with RNTI %04x: SRB1 not active, cannot send RRCReconfiguration\n", rnti);
    return;
  }

  f1_ue_data_t ue_data = cu_get_f1_ue_data(ue_p->rrc_ue_id);
  if (ue_data.du_assoc_id == 0) {
    LOG_E(NR_RRC, "UE with RNTI %04x: F1 UE data invalid (assoc_id=0), cannot send RRCReconfiguration\n", rnti);
    return;
  }

  if (!ue_p->as_security_active) {
    LOG_E(NR_RRC, "UE with RNTI %04x: AS security not active, cannot send protected RRC message\n", rnti);
    return;
  }

  uint8_t srb_id = DL_SCH_LCID_DCCH;

  NR_DL_DCCH_Message_t dl_dcch_msg = {0};
  dl_dcch_msg.message.present = NR_DL_DCCH_MessageType_PR_c1;

  struct NR_DL_DCCH_MessageType__c1 *c1 = calloc(1, sizeof(*c1));
  dl_dcch_msg.message.choice.c1 = c1;
  c1->present = NR_DL_DCCH_MessageType__c1_PR_rrcReconfiguration;

  NR_RRCReconfiguration_t *rrcReconf = calloc(1, sizeof(*rrcReconf));
  c1->choice.rrcReconfiguration = rrcReconf;
  rrcReconf->rrc_TransactionIdentifier = rrc_gNB_get_next_transaction_identifier(RC.nrrrc[instance]->module_id);
  rrcReconf->criticalExtensions.present = NR_RRCReconfiguration__criticalExtensions_PR_rrcReconfiguration;

  NR_RRCReconfiguration_IEs_t *ie = calloc(1, sizeof(*ie));
  rrcReconf->criticalExtensions.choice.rrcReconfiguration = ie;
  ie->radioBearerConfig = NULL;
  ie->measConfig = NULL;

  ie->secondaryCellGroup = calloc(1, sizeof(*ie->secondaryCellGroup));
  if (!ie->secondaryCellGroup) {
    LOG_E(NR_RRC, "Failed to allocate secondaryCellGroup\n");
    goto cleanup;
  }
  OCTET_STRING_fromBuf(ie->secondaryCellGroup, "INVALID_DATA", 12);

  byte_array_t msg = {0};
  ssize_t encoded = uper_encode_to_new_buffer(
      &asn_DEF_NR_DL_DCCH_Message, NULL, &dl_dcch_msg, (void **)&msg.buf
  );
  if (encoded <= 0) {
    LOG_E(NR_RRC, "Failed to encode RRCReconfiguration\n");
    goto cleanup;
  }
  msg.len = encoded;

  const uint32_t msg_id = NR_DL_DCCH_MessageType__c1_PR_rrcReconfiguration;
  LOG_I(NR_RRC, "UE with RNTI %04x: Sending corrupted RRCReconfiguration on SRB %d to trigger re-establishment\n", rnti, srb_id == DL_SCH_LCID_DCCH1 ? 2 : 1);
  nr_rrc_transfer_protected_rrc_message(
      RC.nrrrc[instance], ue_p, srb_id, msg_id, msg.buf, msg.len
  );

cleanup:
  if (msg.buf) free(msg.buf);
  if (ie && ie->secondaryCellGroup) {
    if (ie->secondaryCellGroup->buf) free(ie->secondaryCellGroup->buf);
    free(ie->secondaryCellGroup);
  }
  if (rrcReconf) free(rrcReconf);
  if (c1) free(c1);
  return;
}

void rrc_force_integrity_check_failure(MessageDef *msg_p, instance_t instance) {
  rnti_t rnti = msg_p->ittiMsg.rrc_force_integrity_check_failure.rnti;
  rrc_gNB_ue_context_t *ue_context = rrc_gNB_get_ue_context_by_rnti_any_du(RC.nrrrc[instance], rnti);
  if (!ue_context) {
    LOG_E(NR_RRC, "UE with RNTI %04x not found\n", rnti);
    return;
  }
  gNB_RRC_UE_t *ue_p = &ue_context->ue_context;

  if (!ue_p->Srb[SRB1].Active) {
    LOG_E(NR_RRC, "UE with RNTI %04x: SRB1 not active, cannot force integrity check failure\n", rnti);
    return;
  }

  if (!ue_p->as_security_active) {
    LOG_E(NR_RRC, "UE with RNTI %04x: AS security not active, cannot force integrity check failure\n", rnti);
    return;
  }

  nr_pdcp_ue_manager_t *pdcp_manager = nr_pdcp_sdap_get_ue_manager();
  if (!pdcp_manager) {
    LOG_E(NR_RRC, "PDCP manager not available\n");
    return;
  }

  nr_pdcp_manager_lock(pdcp_manager);

  NR_DL_DCCH_Message_t dl_dcch_msg = {0};
  struct NR_DL_DCCH_MessageType__c1 *c1 = NULL;
  NR_RRCReconfiguration_t *rrcReconf = NULL;
  NR_RRCReconfiguration_IEs_t *ie = NULL;
  byte_array_t msg = {0};
  nr_pdcp_entity_t *srb1_entity = NULL;

  nr_pdcp_ue_t *pdcp_ue = nr_pdcp_manager_get_ue(pdcp_manager, ue_p->rrc_ue_id);
  if (!pdcp_ue) {
    LOG_E(NR_RRC, "PDCP UE context not found for rrc_ue_id %u\n", ue_p->rrc_ue_id);
    goto cleanup_pdcp;
  }

  srb1_entity = pdcp_ue->srb[SRB1 - 1];
  if (!srb1_entity) {
    LOG_E(NR_RRC, "SRB1 PDCP entity not found for UE %u\n", ue_p->rrc_ue_id);
    goto cleanup_pdcp;
  }

  dl_dcch_msg.message.present = NR_DL_DCCH_MessageType_PR_c1;

  c1 = calloc(1, sizeof(*c1));
  dl_dcch_msg.message.choice.c1 = c1;
  c1->present = NR_DL_DCCH_MessageType__c1_PR_rrcReconfiguration;

  rrcReconf = calloc(1, sizeof(*rrcReconf));
  c1->choice.rrcReconfiguration = rrcReconf;
  rrcReconf->rrc_TransactionIdentifier = rrc_gNB_get_next_transaction_identifier(RC.nrrrc[instance]->module_id);
  rrcReconf->criticalExtensions.present = NR_RRCReconfiguration__criticalExtensions_PR_rrcReconfiguration;

  ie = calloc(1, sizeof(*ie));
  rrcReconf->criticalExtensions.choice.rrcReconfiguration = ie;
  ie->radioBearerConfig = NULL;
  ie->measConfig = NULL;

  ie->secondaryCellGroup = calloc(1, sizeof(*ie->secondaryCellGroup));
  if (!ie->secondaryCellGroup) {
    LOG_E(NR_RRC, "Failed to allocate secondaryCellGroup\n");
    goto cleanup_pdcp;
  }
  OCTET_STRING_fromBuf(ie->secondaryCellGroup, "VALID_DATA", 11);

  ssize_t encoded = uper_encode_to_new_buffer(
      &asn_DEF_NR_DL_DCCH_Message, NULL, &dl_dcch_msg, (void **)&msg.buf
  );
  if (encoded <= 0) {
    LOG_E(NR_RRC, "Failed to encode RRCReconfiguration\n");
    goto cleanup_pdcp;
  }
  msg.len = encoded;

  char pdu_buffer[2048];
  int pdu_size = srb1_entity->process_sdu(
      srb1_entity,
      (char *)msg.buf,
      msg.len,
      rrc_gNB_mui++,
      pdu_buffer,
      sizeof(pdu_buffer)
  );

  if (pdu_size <= 0) {
    LOG_E(NR_RRC, "Failed to process SDU through PDCP\n");
    goto cleanup_pdcp;
  }

  uint8_t *mac_i = (uint8_t *)pdu_buffer + pdu_size - PDCP_INTEGRITY_SIZE;
  uint32_t mac_i_value = *(uint32_t *)mac_i;
  LOG_I(NR_RRC, "UE with RNTI %04x: Original MAC-I: 0x%08x", rnti, mac_i_value);

  mac_i_value ^= 0x01;
  *(uint32_t *)mac_i = mac_i_value;
  LOG_I(NR_RRC, "UE with RNTI %04x: Corrupted MAC-I: 0x%08x (flipped LSB)\n", rnti, mac_i_value);

  f1_ue_data_t ue_data = cu_get_f1_ue_data(ue_p->rrc_ue_id);
  if (ue_data.du_assoc_id == 0) {
    LOG_E(NR_RRC, "cannot send data: invalid assoc_id 0, DU offline\n");
    goto cleanup_pdcp;
  }
  f1ap_dl_rrc_message_t dl_rrc = {.gNB_CU_ue_id = ue_p->rrc_ue_id, .gNB_DU_ue_id = ue_data.secondary_ue, .srb_id = SRB1};
  deliver_dl_rrc_message_data_t data = {.rrc = RC.nrrrc[instance], .dl_rrc = &dl_rrc, .assoc_id = ue_data.du_assoc_id};
  rrc_deliver_dl_rrc_message(&data, ue_p->rrc_ue_id, SRB1, pdu_buffer, pdu_size, 0);
  goto cleanup_pdcp;

cleanup_pdcp:
  if (msg.buf) free(msg.buf);
  if (ie && ie->secondaryCellGroup) {
    if (ie->secondaryCellGroup->buf) free(ie->secondaryCellGroup->buf);
    free(ie->secondaryCellGroup);
  }
  if (rrcReconf) free(rrcReconf);
  if (c1) free(c1);
  nr_pdcp_manager_unlock(pdcp_manager);
  return;
}

void rrc_get_ue_context_by_rnti_any_du(MessageDef *msg_p, instance_t instance)
{
  MessageDef *resp_p = itti_alloc_new_message(TASK_RRC_GNB, 0, RRC_GET_UE_CONTEXT_BY_RNTI_ANY_DU);
  resp_p->ittiMsg.rrc_get_ue_context_by_rnti_any_du.rnti = msg_p->ittiMsg.rrc_get_ue_context_by_rnti_any_du.rnti;
  resp_p->ittiMsg.rrc_get_ue_context_by_rnti_any_du.ue_context_exists =
      (rrc_gNB_get_ue_context_by_rnti_any_du(RC.nrrrc[instance], msg_p->ittiMsg.rrc_get_ue_context_by_rnti_any_du.rnti) != NULL);
  itti_send_msg_to_task(TASK_TELNET, 0, resp_p);
}

void rrc_check_ue_context(MessageDef *msg_p, instance_t instance)
{
  MessageDef *resp_p = itti_alloc_new_message(TASK_RRC_GNB, 0, RRC_CHECK_UE_CONTEXT);
  resp_p->ittiMsg.rrc_check_ue_context.id = msg_p->ittiMsg.rrc_check_ue_context.id;
  if (RC.nrrrc[instance] != NULL) {
    rrc_gNB_ue_context_t *ue = rrc_gNB_get_ue_context(RC.nrrrc[instance], msg_p->ittiMsg.rrc_check_ue_context.id);
    if (!ue) {
      resp_p->ittiMsg.rrc_check_ue_context.check = false;
    } else {
      resp_p->ittiMsg.rrc_check_ue_context.check = true;
    }
  }
  itti_send_msg_to_task(TASK_TELNET, 0, resp_p);
}

static void rrc_get_ue_context_by_ue_id_helper(MessageDef **msg_p, ue_id_t ue_id, instance_t instance)
{
  if (ue_id == -1) {
    rrc_get_single_ue_rnti_helper(msg_p, instance);
    (*msg_p)->ittiMsg.rrc_get_ue_context_by_ue_id.id = (*msg_p)->ittiMsg.rrc_get_single_ue_rnti.id;
  } else {
    (*msg_p)->ittiMsg.rrc_get_ue_context_by_ue_id.id = ue_id;
  }
  if ((*msg_p)->ittiMsg.rrc_get_ue_context_by_ue_id.id != -1) {
    rrc_gNB_ue_context_t *ue = NULL;
    ue = rrc_gNB_get_ue_context(RC.nrrrc[instance], (*msg_p)->ittiMsg.rrc_get_ue_context_by_ue_id.id);
    if (ue) {
      (*msg_p)->ittiMsg.rrc_get_ue_context_by_ue_id.rnti = ue->ue_context.rnti;
      (*msg_p)->ittiMsg.rrc_get_ue_context_by_ue_id.ue_reestablishment_counter = ue->ue_context.ue_reestablishment_counter;
      (*msg_p)->ittiMsg.rrc_get_ue_context_by_ue_id.ue_reconfiguration_counter = ue->ue_context.ue_reconfiguration_counter;
      (*msg_p)->ittiMsg.rrc_get_ue_context_by_ue_id.rrc_ue_id = ue->ue_context.rrc_ue_id;
      (*msg_p)->ittiMsg.rrc_get_ue_context_by_ue_id.is_single = true;
    } else {
      LOG_E(RRC, "Could not find UE context associated with UE ID %lu\n", ue_id);
    }
  }
}

void rrc_get_ue_context_by_ue_id(MessageDef *msg_p, instance_t instance)
{
  MessageDef *resp_p = itti_alloc_new_message(TASK_RRC_GNB, 0, RRC_GET_UE_CONTEXT_BY_UE_ID);
  rrc_get_ue_context_by_ue_id_helper(&resp_p, msg_p->ittiMsg.rrc_get_ue_context_by_ue_id.id, instance);
  itti_send_msg_to_task(TASK_TELNET, 0, resp_p);
}

static void rrc_get_du_id_by_ue_id_helper(MessageDef **resp_p, int ue_id, instance_t instance)
{
  if (ue_id != -1) {
    (*resp_p)->ittiMsg.rrc_get_du_id_by_ue_id.ue_id = ue_id;
  } else {
    rrc_get_single_ue_rnti_helper(resp_p, instance);
    if ((*resp_p)->ittiMsg.rrc_get_single_ue_rnti.id > 0) {
      (*resp_p)->ittiMsg.rrc_get_du_id_by_ue_id.ue_id = (*resp_p)->ittiMsg.rrc_get_single_ue_rnti.id;
    }
  }
  if ((*resp_p)->ittiMsg.rrc_get_du_id_by_ue_id.ue_id != -1) {
    nr_rrc_du_container_t *du = get_du_for_ue(RC.nrrrc[instance], (*resp_p)->ittiMsg.rrc_get_du_id_by_ue_id.ue_id);
    if (du != NULL && du->gNB_DU_id != 0) {
      (*resp_p)->ittiMsg.rrc_get_du_id_by_ue_id.du_id = du->gNB_DU_id;
      (*resp_p)->ittiMsg.rrc_get_du_id_by_ue_id.no_du = false;
    } else {
      (*resp_p)->ittiMsg.rrc_get_du_id_by_ue_id.no_du = true;
    }
  }
}

void rrc_get_du_id_by_ue_id(MessageDef *msg_p, instance_t instance)
{
  MessageDef *resp_p = itti_alloc_new_message(TASK_RRC_GNB, 0, RRC_GET_DU_ID_BY_UE_ID);
  rrc_get_du_id_by_ue_id_helper(&resp_p, msg_p->ittiMsg.rrc_get_du_id_by_ue_id.ue_id, instance);
  itti_send_msg_to_task(TASK_TELNET, 0, resp_p);
}

extern void nr_F1_HO_trigger_telnet(gNB_RRC_INST *rrc, uint32_t rrc_ue_id);
extern void nr_N2_HO_trigger_telnet(gNB_RRC_INST *rrc, uint32_t neighbour_pci, uint32_t rrc_ue_id);

void rrc_trigger_f1_ho(MessageDef *msg_p, instance_t instance)
{
  nr_F1_HO_trigger_telnet(RC.nrrrc[instance], msg_p->ittiMsg.rrc_trigger_f1_ho.id);
}

void rrc_trigger_n2_ho(MessageDef *msg_p, instance_t instance)
{
  nr_N2_HO_trigger_telnet(RC.nrrrc[instance], msg_p->ittiMsg.rrc_trigger_n2_ho.neighbour_pci, msg_p->ittiMsg.rrc_trigger_n2_ho.id);
}

void rrc_get_ngap_ue_id(MessageDef *msg_p, instance_t instance)
{
  if (msg_p->ittiMsg.rrc_get_ngap_ue_id.gNB_ue_ngap_id == -1) {
    MessageDef *resp_p = itti_alloc_new_message(TASK_RRC_GNB, 0, RRC_GET_SINGLE_UE_RNTI);
    rrc_get_single_ue_rnti_helper(&resp_p, instance);
    msg_p->ittiMsg.rrc_get_ngap_ue_id.gNB_ue_ngap_id = resp_p->ittiMsg.rrc_get_single_ue_rnti.id;
    free(resp_p);
  }
  MessageDef *resp_p2 = itti_alloc_new_message(TASK_RRC_GNB, 0, RRC_GET_NGAP_UE_ID);
  resp_p2->ittiMsg.rrc_get_ngap_ue_id.gNB_ue_ngap_id = msg_p->ittiMsg.rrc_get_ngap_ue_id.gNB_ue_ngap_id;
  ngap_gNB_ue_context_t *ngap_ue_context = ngap_get_ue_context(msg_p->ittiMsg.rrc_get_ngap_ue_id.gNB_ue_ngap_id);
  if (ngap_ue_context) {
    resp_p2->ittiMsg.rrc_get_ngap_ue_id.amf_ue_ngap_id = ngap_ue_context->amf_ue_ngap_id;
    resp_p2->ittiMsg.rrc_get_ngap_ue_id.gNB_ue_ngap_id = ngap_ue_context->gNB_ue_ngap_id;
  } else {
    resp_p2->ittiMsg.rrc_get_ngap_ue_id.amf_ue_ngap_id = 0;
  }
  itti_send_msg_to_task(TASK_TELNET, 0, resp_p2);
}

void rrc_gnb_generate_rrcrelease(MessageDef *msg_p, instance_t instance)
{
  if (msg_p->ittiMsg.rrc_gnb_generate_rrcrelease.ue_id == -1) {
    MessageDef *resp_p = itti_alloc_new_message(TASK_RRC_GNB, 0, RRC_GET_SINGLE_UE_RNTI);
    rrc_get_single_ue_rnti_helper(&resp_p, instance);
    msg_p->ittiMsg.rrc_gnb_generate_rrcrelease.ue_id = resp_p->ittiMsg.rrc_get_single_ue_rnti.id;
    free(resp_p);
  }
  rrc_gNB_ue_context_t *ue = rrc_gNB_get_ue_context(RC.nrrrc[instance], msg_p->ittiMsg.rrc_gnb_generate_rrcrelease.ue_id);
  if (ue != NULL) {
    gNB_RRC_UE_t *UE = &ue->ue_context;
    rrc_gNB_generate_RRCRelease(RC.nrrrc[instance], UE);
  } else {
    LOG_E(RRC, "UE context not found for ue_id %lu\n", msg_p->ittiMsg.rrc_gnb_generate_rrcrelease.ue_id);
  }
}

void rrc_gnb_generate_rrcrelease_all(MessageDef *msg_p, instance_t instance)
{
  MessageDef * resp_p = itti_alloc_new_message(TASK_RRC_GNB, 0, RRC_GNB_GENERATE_RRCRELEASE_ALL);
  rrc_gNB_ue_context_t *ue_context_p = NULL;
  int i=0;
  RB_FOREACH (ue_context_p, rrc_nr_ue_tree_s, &RC.nrrrc[instance]->rrc_ue_head) {
    gNB_RRC_UE_t *UE = &ue_context_p->ue_context;
    rrc_gNB_generate_RRCRelease(RC.nrrrc[instance], UE);
    resp_p->ittiMsg.rrc_gnb_generate_rrcrelease_all.nb_releases++;
    resp_p->ittiMsg.rrc_gnb_generate_rrcrelease_all.rrc_gnb_generate_rrcreleases[i].ue_id = ue_context_p->ue_context.rrc_ue_id;
  }
  itti_send_msg_to_task(TASK_TELNET, 0, resp_p);
}

void rrc_gnb_trigger_ue_context_release_req(MessageDef *msg_p, instance_t instance)
{
  MessageDef *resp_p = itti_alloc_new_message(TASK_RRC_GNB, 0, RRC_GNB_TRIGGER_UE_CONTEXT_RELEASE_REQ);
  gNB_RRC_INST *rrc = RC.nrrrc[0];
  resp_p->ittiMsg.rrc_gnb_trigger_ue_context_release_req.ue_id = msg_p->ittiMsg.rrc_gnb_trigger_ue_context_release_req.ue_id;
  rrc_gNB_ue_context_t *ue_context_p = rrc_gNB_get_ue_context(rrc, msg_p->ittiMsg.rrc_gnb_trigger_ue_context_release_req.ue_id);
  if (!ue_context_p) {
    itti_send_msg_to_task(TASK_TELNET, 0, resp_p);
  } else if (!ngap_get_ue_context(msg_p->ittiMsg.rrc_gnb_trigger_ue_context_release_req.ue_id)) {
    resp_p->ittiMsg.rrc_gnb_trigger_ue_context_release_req.rrc_ue_context = true;
    itti_send_msg_to_task(TASK_TELNET, 0, resp_p);
  } else {
    resp_p->ittiMsg.rrc_gnb_trigger_ue_context_release_req.rrc_ue_context = true;
    resp_p->ittiMsg.rrc_gnb_trigger_ue_context_release_req.ngap_ue_context = true;
    ngap_cause_t cause = {
        .type = NGAP_CAUSE_RADIO_NETWORK,
        .value = NGAP_CAUSE_RADIO_NETWORK_USER_INACTIVITY,
    };
    rrc_gNB_send_NGAP_UE_CONTEXT_RELEASE_REQ(0, ue_context_p, cause);
    itti_send_msg_to_task(TASK_TELNET, 0, resp_p);
  }
}
