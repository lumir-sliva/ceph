// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:nil -*-
// vim: ts=8 sw=2 sts=2 expandtab

/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2013 Inktank, Inc
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */
#ifndef CEPH_HEALTH_MONITOR_H
#define CEPH_HEALTH_MONITOR_H

#include <map>
#include <set>
#include <string>

#include "mon/PaxosService.h"

class HealthMonitor : public PaxosService
{
  version_t version = 0;
  std::map<int,health_check_map_t> quorum_checks;  // for each quorum member
  health_check_map_t leader_checks;           // leader only
  std::map<std::string,health_mute_t> mutes;
  // location level netsplit pairs to elasped time
  std::map<std::pair<std::string, std::string>, ceph::coarse_mono_clock::time_point> pending_location_netsplits;
  // individual level netsplit pairs to elasped time
  std::map<std::pair<std::string, std::string>, ceph::coarse_mono_clock::time_point> pending_mon_netsplits;
  // currently active location netsplits with their elapsed time
  std::map<std::pair<std::string, std::string>, ceph::coarse_mono_clock::time_point> current_location_netsplits;
  // currently active monitor netsplits with their elapsed time
  std::map<std::pair<std::string, std::string>, ceph::coarse_mono_clock::time_point> current_mon_netsplits;
  std::map<std::string,health_mute_t> pending_mutes;

public:
  HealthMonitor(Monitor &m, Paxos &p, const std::string& service_name);

  /**
   * @defgroup HealthMonitor_Inherited_h Inherited abstract methods
   * @{
   */
  void init() override;

  bool preprocess_query(MonOpRequestRef op) override;
  bool prepare_update(MonOpRequestRef op) override;

  void create_initial() override;
  void update_from_paxos(bool *need_bootstrap) override;
  void create_pending() override;
  void encode_pending(MonitorDBStore::TransactionRef t) override;
  version_t get_trim_to() const override;

  void encode_full(MonitorDBStore::TransactionRef t) override { }

  void tick() override;

  void gather_all_health_checks(health_check_map_t *all);
  health_status_t get_health_status(
    bool want_detail,
    ceph::Formatter *f,
    std::string *plain,
    const char *sep1 = " ",
    const char *sep2 = "; ");

  /**
   * @} // HealthMonitor_Inherited_h
   */

  /// the MON_DISK_BIG threshold, and the inputs it was derived from
  struct data_size_warn_t {
    uint64_t effective = 0;  ///< what the store size is compared against
    uint64_t raw = 0;        ///< before the filesystem cap was applied
    uint64_t base = 0;       ///< mon_data_size_warn
    uint64_t num_osds = 0;
    uint64_t num_pgs = 0;
    uint64_t fs_total = 0;   ///< 0 when the filesystem size is unknown
    double fs_ratio = 0;

    /// the cluster size terms raised the threshold
    bool is_scaled() const { return effective > base; }
    /// the filesystem cap held the threshold below the scaled value
    bool is_capped() const { return effective < raw; }
  };

  /**
   * Derive the effective MON_DISK_BIG threshold.
   *
   * Scales the base threshold by the size of the cluster, since the amount a
   * mon has to store grows with the number of OSDs and PGs it tracks.  The
   * cluster size terms are then held below @p fs_ratio of the filesystem that
   * houses the store, so that scaling the threshold up cannot push it past the
   * point where MON_DISK_LOW and MON_DISK_CRIT become the operator's first
   * warning.  The cap never lowers the threshold below @p base, so it cannot
   * make the warning fire earlier than the configured value; on a filesystem
   * small enough that @p fs_ratio of it is below @p base, the threshold is
   * simply @p base, exactly as it was before the threshold became adaptive.
   *
   * All arithmetic saturates: no combination of options can wrap.
   */
  static data_size_warn_t derive_data_size_warn(
    uint64_t base,
    uint64_t per_osd,
    uint64_t per_pg,
    uint64_t num_osds,
    uint64_t num_pgs,
    uint64_t fs_total,
    double fs_ratio);

private:
  /// derive the MON_DISK_BIG threshold from the options and this mon's OSDMap
  data_size_warn_t get_data_size_warn(uint64_t fs_total);

  bool preprocess_command(MonOpRequestRef op);

  bool prepare_command(MonOpRequestRef op);
  bool prepare_health_checks(MonOpRequestRef op);
  void check_for_colocated_monitors(health_check_map_t *checks);
  void check_for_older_version(health_check_map_t *checks);
  void check_for_mon_down(health_check_map_t *checks, std::set<std::string> &mons_down);
  void check_for_clock_skew(health_check_map_t *checks);
  void check_mon_crush_loc_stretch_mode(health_check_map_t *checks);
  void check_if_msgr2_enabled(health_check_map_t *checks);
  void check_erasure_code_profiles(health_check_map_t *checks);
  void check_netsplit(health_check_map_t *checks, std::set<std::string> &mons_down);
  bool check_leader_health();
  bool check_member_health();
  bool check_mutes();
};

#endif // CEPH_HEALTH_MONITOR_H
