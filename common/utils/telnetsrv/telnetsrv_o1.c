/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#define TELNETSERVERCODE
#include "telnetsrv_o1.h"


static int get_cu_stats(telnet_printfunc_t prnt, uint32_t node_id, char *node_name)
{
  MessageDef *msg_p = itti_alloc_new_message (TASK_RRC_GNB, 0, RRC_GET_ACTIVE_UE_LIST);
  itti_send_msg_to_task(TASK_RRC_GNB, 0, msg_p);
  itti_receive_msg(TASK_TELNET, &msg_p);
  Rrc_get_active_ue_list ue_list = msg_p->ittiMsg.rrc_active_ue_list;

  MessageDef *msg_p2 = itti_alloc_new_message (TASK_RRC_GNB, 0, RRC_GET_CUUP_CELLS_LIST);
  itti_send_msg_to_task(TASK_RRC_GNB, 0, msg_p2);
  itti_receive_msg(TASK_TELNET, &msg_p2);
  Rrc_get_cuup_cells_list cell_list = msg_p2->ittiMsg.rrc_cuup_cells_list;

  const int srb_flag = 0;
  const int rb_id = 1;
  int ue_id;
  int sum_pdcpsduvolumeul = 0;
  int sum_pdcpsduvolumedl = 0;
  int sum_pdcppduvolumeul = 0;
  int sum_pdcppduvolumedl = 0;
  for(int i=0; i<ue_list.num_ues; i++){
      ue_id = ue_list.rnti_list[i];
      nr_pdcp_statistics_t pdcp = {0};
      nr_pdcp_get_statistics(ue_id, srb_flag, rb_id, &pdcp);
      sum_pdcpsduvolumedl += pdcp.rxsdu_bytes;
      sum_pdcpsduvolumeul += pdcp.txsdu_bytes;
      sum_pdcppduvolumedl += pdcp.rxpdu_bytes;
      sum_pdcppduvolumeul += pdcp.txpdu_bytes;
  }

  itti_free(ITTI_MSG_ORIGIN_ID(msg_p), msg_p);
  itti_free(ITTI_MSG_ORIGIN_ID(msg_p2), msg_p2);
  sum_pdcpsduvolumedl = sum_pdcpsduvolumedl*8/1000000;
  sum_pdcpsduvolumeul = sum_pdcpsduvolumeul*8/1000000;
  sum_pdcppduvolumedl = sum_pdcppduvolumedl*8/1000000;
  sum_pdcppduvolumeul = sum_pdcppduvolumeul*8/1000000;
  bool first = true;

  prnt("{\n");
    prnt("  \"o1-config\": {\n");
    prnt("    \"NRCELLCU\": [\n");

    for(int i=0; i<cell_list.cell_count; i++){
        if (!first)
          prnt(",\n");
        prnt("      {\n");
        prnt("        \""CELLLOCALID"\": %d,\n", cell_list.cell_ids[i]);
        prnt("        \""CU_MCC"\": \"%03d\",\n", cell_list.mccs[i]);
        prnt("        \""CU_MNC"\": \"%0*d\",\n", cell_list.mnc_digit_lengths[i], cell_list.mncs[i]);
        prnt("        \""CU_SST"\": %d,\n", cell_list.sst);
        prnt("        \""CU_SD "\": %d\n", cell_list.sd);
        if (i < cell_list.cell_count - 1)
          prnt("      },");
        else
          prnt("      }");
        first = false;
    }
    prnt("\n    ],\n");

    /* TODO harmonize this between DU&CU to be able to have one in monolithic */
    prnt("    \"device\": {\n");
    prnt("      \"gNBId\": %d,\n", node_id);
    prnt("      \"gnbName\": \"%s\",\n", node_name);
    prnt("      \"vendor\": \"OpenAirInterface\"\n");
    prnt("    },\n");
    prnt("    \"O1-Operational\": {\n");
    prnt("      \"PdcpSduVolumeUl\": %d,\n", sum_pdcpsduvolumeul);
    prnt("      \"PdcpSduVolumeDl\": %d,\n", sum_pdcpsduvolumedl);
    prnt("      \"PdcpPduVolumeUl\": %d,\n", sum_pdcppduvolumeul);
    prnt("      \"PdcpPduVolumeDl\": %d\n", sum_pdcppduvolumedl);
    prnt("    }\n");
    prnt("  }\n");

  prnt("}\n");
  prnt("OK\n");

  return 1;
}

static inline int gnb_id_bit_length(uint32_t gnb_id) {
  if (gnb_id == 0) return 1;
  return 32 - __builtin_clz(gnb_id);
}


uint32_t extract_cell_id(uint64_t nci, const cell_config_t *cfg) {
  for (int gnb_bits = 22; gnb_bits <= 32; gnb_bits++) {
    int cell_bits = NR_NCI_BITS - gnb_bits;

    uint32_t candidate_gnb_id = (uint32_t)(nci >> cell_bits);

    if (candidate_gnb_id == cfg->gnb_id) {
      return (uint32_t)(nci & ((1ULL << cell_bits) - 1));
    }
  }

  return UINT32_MAX;
}

static int get_du_stats(char *buf, int debug, telnet_printfunc_t prnt)
{
  UNUSED(debug);
  if (buf)
    ERROR_MSG_RET("no parameter allowed\n");

  MessageDef *msg_p = itti_alloc_new_message(TASK_MAC_GNB, 0, MAC_GET_O1_STATS);
  MessageDef *resp_p;

  if (!itti_send_and_receive_msg_to_task(TASK_MAC_GNB, TASK_TELNET, msg_p, &resp_p, 1000)) {
    itti_free(ITTI_MSG_ORIGIN_ID(msg_p), msg_p);
    ERROR_MSG_RET("Timeout waiting for MAC response\n");
  }

  Mac_get_o1_stats *current = &resp_p->ittiMsg.mac_get_o1_stats;

  static uint64_t last_dl_used = 0;
  static uint64_t last_dl_total = 0;
  int diff_used = current->dl_used_prb_aggregate - last_dl_used;
  int diff_total = current->dl_total_prb_aggregate - last_dl_total;
  int load = diff_total > 0 ? 100 * diff_used / diff_total : 0;
  last_dl_used = current->dl_used_prb_aggregate;
  last_dl_total = current->dl_total_prb_aggregate;

  size_t diff_msec = (current->tp_now.tv_sec - current->tp_last.tv_sec) * 1000 +
                     (current->tp_now.tv_nsec - current->tp_last.tv_nsec) / 1000000;
  if (diff_msec == 0) diff_msec = 1; /* Avoid division by zero */

  static long last_total_dl[MAX_MOBILES_PER_GNB] = {0};
  static long last_total_ul[MAX_MOBILES_PER_GNB] = {0};

  ue_stat_t ue_stat[MAX_MOBILES_PER_GNB] = {0};

  for (int i = 0; i < current->num_ues; i++) {
    const int rnti = current->ue_rlc_stats[i].rnti;
    const long txpdu_bytes = current->ue_rlc_stats[i].txpdu_bytes;
    const long rxpdu_bytes = current->ue_rlc_stats[i].rxpdu_bytes;

    if (last_total_dl[i] > txpdu_bytes)
      last_total_dl[i] = txpdu_bytes;
    if (last_total_ul[i] > rxpdu_bytes)
      last_total_ul[i] = rxpdu_bytes;

    ue_stat[i].rnti = rnti;
    ue_stat[i].thr.dl = (txpdu_bytes - last_total_dl[i]) * 8 / diff_msec;
    ue_stat[i].thr.ul = (rxpdu_bytes - last_total_ul[i]) * 8 / diff_msec;

    last_total_dl[i] = txpdu_bytes;
    last_total_ul[i] = rxpdu_bytes;
  }

  uint32_t local_cell_id = current->cell_local_id;


  prnt("{\n");
    prnt("  \"o1-config\": {\n");

    prnt("    \"BWP\": {\n");
    prnt("      \"dl\": [{\n");
    prnt("        \"" ISINITBWP "\": true,\n");
    prnt("        \"" NUMRBS "\": %ld,\n", current->dl_numrbs);
    prnt("        \"" STARTRB "\": %ld,\n", current->dl_startrb);
    prnt("        \"" BWPSCS "\": %ld\n", current->dl_bwpscs);
    prnt("      }],\n");
    prnt("      \"ul\": [{\n");
    prnt("        \"" ISINITBWP "\": true,\n");
    prnt("        \"" NUMRBS "\": %ld,\n", current->ul_numrbs);
    prnt("        \"" STARTRB "\": %ld,\n", current->ul_startrb);
    prnt("        \"" BWPSCS "\": %ld\n", current->ul_bwpscs);
    prnt("      }]\n");
    prnt("    },\n");

    prnt("    \"NRCELLDU\": {\n");
    prnt("      \"" SSBFREQ "\": %ld,\n", current->ssbFrequency);
    prnt("      \"" ARFCNDL "\": %ld,\n", current->arfcnDL);
    prnt("      \"" BWDL "\": %ld,\n", current->bw_mhz);
    prnt("      \"" ARFCNUL "\": %ld,\n", current->arfcnUL);
    prnt("      \"" BWUL "\": %ld,\n", current->bw_mhz);
    prnt("      \"" PCI "\": %ld,\n", current->pci);
    prnt("      \"" TAC "\": %ld,\n", current->tac);
    prnt("      \"" MCC "\": \"%03d\",\n", current->mcc);
    prnt("      \"" MNC "\": \"%0*d\",\n", current->mnc_digit_length, current->mnc);
    prnt("      \"" CLI "\": \"%d\",\n", local_cell_id);
    prnt("      \"" SD  "\": %d,\n", current->sd);
    prnt("      \"" SST "\": %d,\n", current->sst);
    prnt("      \"" SSBSCS "\": %d,\n", 30);
    prnt("      \"" SSBPRD "\": %d,\n", 20);
    prnt("      \"" SSBOFF "\": %d,\n", 0);
    prnt("      \"" SSBDUR "\": %d\n", 1);
    prnt("    },\n");
    prnt("    \"device\": {\n");
    prnt("      \"gnbId\": %d,\n", current->gNB_DU_id);
    prnt("      \"gnbName\": \"%s\",\n", current->gNB_DU_name);
    prnt("      \"vendor\": \"OpenAirInterface\"\n");
    prnt("    }\n");
    prnt("  },\n");

    prnt("  \"O1-Operational\": {\n");
    prnt("    \"frame-type\": \"%s\",\n", current->frame_type == TDD ? "tdd" : "fdd");
    prnt("    \"band-number\": %ld,\n", current->band);
    prnt("    \"num-ues\": %d,\n", current->num_ues);
    prnt("    \"ues\": ["); PRINTLIST_i(current->num_ues, "%d", ue_stat[i].rnti);
    for (int dbg_i = 0; dbg_i < current->num_ues; dbg_i++) {
      if (dbg_i < current->num_ues - 1) printf(",");
    }
    prnt("],\n");
    prnt("    \"load\": %d,\n", load);
    prnt("    \"ues-thp\": [");
    for (int i = 0; i < current->num_ues; i++) {
      prnt("\n      {\"rnti\": %d, \"dl\": %ld, \"ul\": %ld}", ue_stat[i].rnti, ue_stat[i].thr.dl, ue_stat[i].thr.ul);
      if (i < current->num_ues - 1) {
        prnt(",");
      }
    }
    prnt("\n     ],\n");
    prnt("    \"RlcPacketDropRateDl\": %d\n", current->RlcPacketDropRateDl);
    prnt("  }\n");
  prnt("}\n");
  prnt("OK\n");
  itti_free(ITTI_MSG_ORIGIN_ID(resp_p), resp_p);
  return 1;
}

static int get_stats(char *buf, int debug, telnet_printfunc_t prnt)
{
  UNUSED(debug);

  if (buf)
    ERROR_MSG_RET("no parameter allowed\n");


  gNB_MAC_INST *mac = RC.nrmac ? RC.nrmac[0] : NULL;
  bool is_du = mac != NULL;
  if (is_du)
    get_du_stats(NULL, 0, prnt);
  else {
    MessageDef *msg_p = itti_alloc_new_message (TASK_RRC_GNB, 0, RRC_GET_NODE_INFO);
    itti_send_msg_to_task(TASK_RRC_GNB, 0, msg_p);
    itti_receive_msg(TASK_TELNET, &msg_p);
    Rrc_get_node_info *ni = &msg_p->ittiMsg.rrc_node_info;
    bool is_cu = NODE_IS_MONOLITHIC(ni->node_type) || NODE_IS_CU(ni->node_type);
    if (is_cu)
      get_cu_stats(prnt, ni->node_id, ni->node_name);
    itti_free(ITTI_MSG_ORIGIN_ID(msg_p), msg_p);
  }

  return 0;
}

static int read_long(const char *buf, const char *end, const char *id, long *val)
{
  const char *curr = buf;
  while (isspace(*curr) && curr < end) // skip leading spaces
    curr++;
  int len = strlen(id);
  if (curr + len >= end)
    return -1;
  if (strncmp(curr, id, len) != 0) // check buf has id
    return -1;
  curr += len;
  while (isspace(*curr) && curr < end) // skip middle spaces
    curr++;
  if (curr >= end)
    return -1;
  int nread = sscanf(curr, "%ld", val);
  if (nread != 1)
    return -1;
  while (isdigit(*curr) && curr < end) // skip all digits read above
    curr++;
  if (curr > end)
    return -1;
  return curr - buf;
}

bool running = true; // in the beginning, the softmodem is started automatically
static int set_config(char *buf, int debug, telnet_printfunc_t prnt)
{
  UNUSED(debug);
  if (!buf)
    ERROR_MSG_RET("need param: o1 config param1 val1 [param2 val2 ...]\n");
  if (running)
    ERROR_MSG_RET("cannot set parameters while L1 is running\n");
  const char *end = buf + strlen(buf);

  /* we need to update the following fields to change frequency and/or
   * bandwidth:
   * --gNBs.[0].servingCellConfigCommon.[0].absoluteFrequencySSB 620736            -> SSBFREQ
   * --gNBs.[0].servingCellConfigCommon.[0].dl_absoluteFrequencyPointA 620020      -> ARFCNDL
   * --gNBs.[0].servingCellConfigCommon.[0].dl_carrierBandwidth 51                 -> BWDL
   * --gNBs.[0].servingCellConfigCommon.[0].initialDLBWPlocationAndBandwidth 13750 -> NUMRBS + STARTRB
   * --gNBs.[0].servingCellConfigCommon.[0].ul_carrierBandwidth 51                 -> BWUL?
   * --gNBs.[0].servingCellConfigCommon.[0].initialULBWPlocationAndBandwidth 13750 -> ?
   */

  int processed = 0;
  int pos = 0;

  long ssbfreq;
  processed = read_long(buf + pos, end, SSBFREQ, &ssbfreq);
  if (processed < 0)
    ERROR_MSG_RET("could not read " SSBFREQ " at index %d\n", pos + processed);
  pos += processed;
  prnt("setting " SSBFREQ ":   %ld [len %d]\n", ssbfreq, pos);

  long arfcn;
  processed = read_long(buf + pos, end, ARFCNDL, &arfcn);
  if (processed < 0)
    ERROR_MSG_RET("could not read " ARFCNDL " at index %d\n", pos + processed);
  pos += processed;
  prnt("setting " ARFCNDL ":        %ld [len %d]\n", arfcn, pos);

  long bwdl;
  processed = read_long(buf + pos, end, BWDL, &bwdl);
  if (processed < 0)
    ERROR_MSG_RET("could not read " BWDL " at index %d\n", pos + processed);
  pos += processed;
  prnt("setting " BWDL ":  %ld [len %d]\n", bwdl, pos);

  long numrbs;
  processed = read_long(buf + pos, end, NUMRBS, &numrbs);
  if (processed < 0)
    ERROR_MSG_RET("could not read " NUMRBS " at index %d\n", pos + processed);
  pos += processed;
  prnt("setting " NUMRBS ":         %ld [len %d]\n", numrbs, pos);

  long startrb;
  processed = read_long(buf + pos, end, STARTRB, &startrb);
  if (processed < 0)
    ERROR_MSG_RET("could not read " STARTRB " at index %d\n", pos + processed);
  pos += processed;
  prnt("setting " STARTRB ":             %ld [len %d]\n", startrb, pos);

  int locationAndBandwidth = PRBalloc_to_locationandbandwidth0(numrbs, startrb, MAX_BWP_SIZE);
  prnt("inferred locationAndBandwidth:       %d\n", locationAndBandwidth);
  prnt("OK\n");
  return 0;
}

static int set_bwconfig(char *buf, int debug, telnet_printfunc_t prnt)
{
  UNUSED(debug);
  if (running)
    ERROR_MSG_RET("cannot set parameters while L1 is running\n");
  if (!buf)
    ERROR_MSG_RET("need param: o1 bwconfig <BW>\n");

  char *end = NULL;
  if (NULL != (end = strchr(buf, '\n')))
    *end = 0;
  if (NULL != (end = strchr(buf, '\r')))
    *end = 0;

  int bw_value = atoi(buf);
  if (bw_value != 20 && bw_value != 40 && bw_value != 60 && bw_value != 100)
    ERROR_MSG_RET("unhandled option %s\n", buf);

  MessageDef *msg_p = itti_alloc_new_message(TASK_MAC_GNB, 0, MAC_SET_BWCONFIG);
  msg_p->ittiMsg.mac_set_bwconfig.bw_value = bw_value;
  MessageDef *resp_p;

  if (!itti_send_and_receive_msg_to_task(TASK_MAC_GNB, TASK_TELNET, msg_p, &resp_p, 1000)) {
    ERROR_MSG_RET("Timeout waiting for MAC response\n");
  }

  itti_free(ITTI_MSG_ORIGIN_ID(resp_p), resp_p);
  prnt("OK\n");
  return 0;
}

extern int stop_L1(module_id_t gnb_id);
static int stop_modem(char *buf, int debug, telnet_printfunc_t prnt)
{
  UNUSED(debug);
  UNUSED(buf);
  if (!running)
    ERROR_MSG_RET("cannot stop, nr-softmodem not running\n");

  MessageDef *msg_p = itti_alloc_new_message(TASK_MAC_GNB, 0, MAC_STOP_MODEM);
  MessageDef *resp_p;

  if (!itti_send_and_receive_msg_to_task(TASK_MAC_GNB, TASK_TELNET, msg_p, &resp_p, 1000)) {
    ERROR_MSG_RET("Timeout waiting for MAC response\n");
  }

  itti_free(ITTI_MSG_ORIGIN_ID(resp_p), resp_p);
  running = false;
  prnt("OK\n");
  return 0;
}

extern int start_L1L2(module_id_t gnb_id, nr_cell_sched_t *cell);
static int start_modem(char *buf, int debug, telnet_printfunc_t prnt)
{
  UNUSED(debug);
  UNUSED(buf);
  if (running)
    ERROR_MSG_RET("cannot start, nr-softmodem already running\n");
  start_L1L2(0, &RC.nrmac[0]->cells[0]);
  running = true;
  prnt("OK\n");
  return 0;
}

extern void du_clear_all_ue_states();
static int remove_mac_ues(char *buf, int debug, telnet_printfunc_t prnt)
{
  UNUSED(debug);
  UNUSED(buf);
  du_clear_all_ue_states();
  prnt("OK\n");
  return 0;
}

static telnetshell_cmddef_t o1cmds[] = {
  {"stats", "", get_stats},
  {"config", "[]", set_config},
  {"bwconfig", "", set_bwconfig},
  {"stop_modem", "", stop_modem},
  {"start_modem", "", start_modem},
  {"remove_mac_ues", "", remove_mac_ues},
  {"", "", NULL},
};

static telnetshell_vardef_t o1vars[] = {
  {"", 0, 0, NULL}
};

void add_o1_cmds(void) {
  add_telnetcmd("o1", o1vars, o1cmds);
}
