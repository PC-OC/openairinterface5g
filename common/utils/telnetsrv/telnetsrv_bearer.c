/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "intertask_interface.h"

#include "openair2/RRC/NR/rrc_gNB_UE_context.h"

#define TELNETSERVERCODE
#include "telnetsrv.h"

#define ERROR_MSG_RET(mSG, aRGS...) do { prnt(mSG, ##aRGS); return 1; } while (0)

int get_single_rnti(char *buf, int debug, telnet_printfunc_t prnt)
{
  UNUSED(debug);
  if (buf)
    ERROR_MSG_RET("no parameter allowed\n");

  MessageDef *msg_p = itti_alloc_new_message(TASK_TELNET, 0, RRC_GET_SINGLE_UE_RNTI);
  if (msg_p == NULL) {
    LOG_E(RRC, "Failed to allocate message for RRC_GET_SINGLE_UE_RNTI\n");
    return 1;
  }
  msg_p->ittiMsg.rrc_get_single_ue_rnti.client_idx = telnetsrv_get_current_client_idx();
  itti_send_msg_to_task(TASK_RRC_GNB, 0, msg_p);
  return 0;
}

//void rrc_gNB_trigger_new_bearer(int rnti);
int add_bearer(char *buf, int debug, telnet_printfunc_t prnt)
{
  UNUSED(debug);
  if (!buf) {
    MessageDef *msg_p = itti_alloc_new_message(TASK_TELNET, 0, RRC_GET_SINGLE_UE_RNTI);
    if (msg_p == NULL) {
      LOG_E(RRC, "Failed to allocate message for RRC_GET_SINGLE_UE_RNTI\n");
      return 1;
    }
    msg_p->ittiMsg.rrc_get_single_ue_rnti.client_idx = telnetsrv_get_current_client_idx();
    itti_send_msg_to_task(TASK_RRC_GNB, 0, msg_p);
    return 0;
  } else {
    rnti_t rnti = strtol(buf, NULL, 16);
    if (rnti < 1 || rnti >= 0xfffe)
      ERROR_MSG_RET("RNTI needs to be [1,0xfffe]\n");

    MessageDef *msg_p = itti_alloc_new_message(TASK_TELNET, 0, RRC_GET_UE_CONTEXT_BY_RNTI_ANY_DU);
    if (msg_p == NULL) {
      LOG_E(RRC, "Failed to allocate message for RRC_GET_UE_CONTEXT_BY_RNTI_ANY_DU\n");
      return 1;
    }
    msg_p->ittiMsg.rrc_get_ue_context_by_rnti_any_du.rnti = rnti;
    msg_p->ittiMsg.rrc_get_ue_context_by_rnti_any_du.client_idx = telnetsrv_get_current_client_idx();
    itti_send_msg_to_task(TASK_RRC_GNB, 0, msg_p);
  //rrc_gNB_trigger_new_bearer(rnti);
    prnt("called rrc_gNB_trigger_new_bearer(%04x)\n", rnti);
    return 0;
  }
}

//void rrc_gNB_trigger_release_bearer(int rnti);
int release_bearer(char *buf, int debug, telnet_printfunc_t prnt)
{
  UNUSED(debug);
  if (!buf) {
    MessageDef *msg_p = itti_alloc_new_message(TASK_TELNET, 0, RRC_GET_SINGLE_UE_RNTI);
    if (msg_p == NULL) {
      LOG_E(RRC, "Failed to allocate message for RRC_GET_SINGLE_UE_RNTI\n");
      return 1;
    }
    msg_p->ittiMsg.rrc_get_single_ue_rnti.client_idx = telnetsrv_get_current_client_idx();
    itti_send_msg_to_task(TASK_RRC_GNB, 0, msg_p);
    return 0;
  } else {
    rnti_t rnti = strtol(buf, NULL, 16);
    if (rnti < 1 || rnti >= 0xfffe)
      ERROR_MSG_RET("RNTI needs to be [1,0xfffe]\n");

    MessageDef *msg_p = itti_alloc_new_message(TASK_TELNET, 0, RRC_GET_UE_CONTEXT_BY_RNTI_ANY_DU);
    if (msg_p == NULL) {
      LOG_E(RRC, "Failed to allocate message for RRC_GET_UE_CONTEXT_BY_RNTI_ANY_DU\n");
      return 1;
    }
    msg_p->ittiMsg.rrc_get_ue_context_by_rnti_any_du.rnti = rnti;
    msg_p->ittiMsg.rrc_get_ue_context_by_rnti_any_du.client_idx = telnetsrv_get_current_client_idx();
    itti_send_msg_to_task(TASK_RRC_GNB, 0, msg_p);
  //rrc_gNB_trigger_release_bearer(rnti);
    prnt("called rrc_gNB_trigger_release_bearer(%04x)\n", rnti);
    return 0;
  }
}

static telnetshell_cmddef_t bearercmds[] = {
  {"get_single_rnti", "", get_single_rnti},
  {"add_bearer", "[rnti(hex,opt)]", add_bearer},
  {"release_bearer", "[rnti(hex,opt)]", release_bearer},
  {"", "", NULL},
};

static telnetshell_vardef_t bearervars[] = {

  {"", 0, 0, NULL}
};

void add_bearer_cmds(void) {
  add_telnetcmd("bearer", bearervars, bearercmds);
}
