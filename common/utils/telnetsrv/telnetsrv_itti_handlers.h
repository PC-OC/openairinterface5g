/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef TELNETSRV_ITTI_HANDLERS_H
#define TELNETSRV_ITTI_HANDLERS_H

#include "common/utils/ocp_itti/intertask_interface.h"
#include "openair2/RRC/NR/nr_rrc_messages_types.h"

void telnetsrv_handle_rrc_get_single_ue_rnti(Rrc_get_single_ue_rnti *rrc_msg);
void telnetsrv_handle_rrc_get_ue_context_by_ue_id(Rrc_get_single_ue_rnti *rrc_msg);
void telnetsrv_handle_rrc_get_du_id_by_ue_id(Rrc_get_du_id_by_ue_id *rrc_msg);
void telnetsrv_handle_mac_trigger_reestab_for_first_rnti(Mac_trigger_reestab_for_first_rnti *mac_msg);
void telnetsrv_handle_rrc_check_ue_context(Rrc_check_ue_context *rrc_msg);
void telnetsrv_handle_rrc_get_ue_context_by_rnti_any_du(Rrc_get_ue_context_by_rnti_any_du *rrc_msg);
void telnetsrv_handle_rrc_get_ngap_ue_id(Rrc_get_ngap_ue_id *rrc_msg);
void telnetsrv_handle_rrc_gnb_generate_rrcrelease(Rrc_gnb_generate_rrcrelease *rrc_msg);
void telnetsrv_handle_rrc_gnb_generate_rrcrelease_all(Rrc_gnb_generate_rrcrelease_all *rrc_msg);
void telnetsrv_handle_rrc_gnb_trigger_ue_context_release_req(Rrc_gnb_trigger_ue_context_release_req *rrc_msg);
void telnetsrv_handle_mac_get_ue_rnti(Mac_get_ue_rnti *mac_msg);
void telnetsrv_handle_mac_get_ue_rnti_by_uid(Mac_get_ue_rnti_by_uid *mac_msg);
void telnetsrv_handle_mac_get_ue_bwp_info(Mac_get_ue_bwp_info *mac_msg);
void telnetsrv_handle_mac_force_ul_failure(Mac_force_ul_failure *mac_msg);
void telnetsrv_handle_mac_force_ue_release(Mac_force_ue_release *mac_msg);
void telnetsrv_handle_mac_set_pusch_target_snr(Mac_set_pusch_target_snr *mac_msg);
void telnetsrv_handle_mac_set_bwconfig(Mac_set_bwconfig *mac_msg);
void telnetsrv_handle_mac_stop_modem(Mac_stop_modem *mac_msg);
void telnetsrv_handle_mac_get_o1_stats(Mac_get_o1_stats *mac_msg);

#endif // TELNETSRV_ITTI_HANDLERS_H
