// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:nil -*-
// vim: ts=8 sw=2 sts=2 expandtab

#include <limits>

#include "gtest/gtest.h"

#include "mon/HealthMonitor.h"

namespace {

constexpr uint64_t K = 1024;
constexpr uint64_t M = 1024 * K;
constexpr uint64_t G = 1024 * M;

// stand-ins for mon_data_size_warn{,_per_osd,_per_pg,_max_fs_ratio}.  PER_PG is
// deliberately non-zero here, unlike the shipped default, so that the per-PG
// term is exercised.
constexpr uint64_t BASE = 15 * G;
constexpr uint64_t PER_OSD = 10 * M;
constexpr uint64_t PER_PG = 50 * K;
constexpr double RATIO = 0.5;

HealthMonitor::data_size_warn_t derive(uint64_t per_osd, uint64_t per_pg,
                                      uint64_t num_osds, uint64_t num_pgs,
                                      uint64_t fs_total, double fs_ratio,
                                      uint64_t base = BASE)
{
  return HealthMonitor::derive_data_size_warn(base, per_osd, per_pg,
                                              num_osds, num_pgs,
                                              fs_total, fs_ratio);
}

} // anonymous namespace

// both scaling terms off: the threshold is the configured size, as it was
// before the threshold became adaptive
TEST(HealthMonitorDataSizeWarn, ScalingDisabled)
{
  auto d = derive(0, 0, 2060, 34273, 800 * G, RATIO);
  EXPECT_EQ(BASE, d.effective);
  EXPECT_EQ(BASE, d.raw);
  EXPECT_FALSE(d.is_scaled());
  EXPECT_FALSE(d.is_capped());
}

// a mon that has no osdmap yet reports no OSDs and no PGs, which must leave the
// threshold at the base rather than scaling it to nothing
TEST(HealthMonitorDataSizeWarn, UnknownClusterSize)
{
  auto d = derive(PER_OSD, PER_PG, 0, 0, 800 * G, RATIO);
  EXPECT_EQ(BASE, d.effective);
  EXPECT_FALSE(d.is_scaled());
}

TEST(HealthMonitorDataSizeWarn, ScalesWithOsdsAndPgs)
{
  auto d = derive(PER_OSD, PER_PG, 2060, 34273, 800 * G, RATIO);
  const uint64_t expected = BASE + 2060 * PER_OSD + 34273 * PER_PG;
  EXPECT_EQ(expected, d.raw);
  EXPECT_EQ(expected, d.effective);
  EXPECT_TRUE(d.is_scaled());
  EXPECT_FALSE(d.is_capped());
  EXPECT_EQ(2060u, d.num_osds);
  EXPECT_EQ(34273u, d.num_pgs);
}

// each term works on its own
TEST(HealthMonitorDataSizeWarn, TermsAreIndependent)
{
  auto osds_only = derive(PER_OSD, 0, 100, 34273, 0, 0);
  EXPECT_EQ(BASE + 100 * PER_OSD, osds_only.effective);

  auto pgs_only = derive(0, PER_PG, 100, 4096, 0, 0);
  EXPECT_EQ(BASE + 4096 * PER_PG, pgs_only.effective);
}

// a big cluster on a small mon disk: the cap keeps the threshold under the
// filesystem, so MON_DISK_BIG still fires before MON_DISK_LOW/CRIT do
TEST(HealthMonitorDataSizeWarn, FilesystemCapBinds)
{
  const uint64_t fs_total = 47 * G;
  auto d = derive(PER_OSD, PER_PG, 2060, 34273, fs_total, RATIO);
  const uint64_t cap = static_cast<uint64_t>(RATIO * fs_total);
  EXPECT_EQ(cap, d.effective);
  EXPECT_GT(d.raw, d.effective);
  EXPECT_TRUE(d.is_scaled());
  EXPECT_TRUE(d.is_capped());
}

// the cap must never warn earlier than the operator configured
TEST(HealthMonitorDataSizeWarn, CapNeverGoesBelowBase)
{
  auto d = derive(PER_OSD, PER_PG, 2060, 34273, 20 * G, RATIO);
  ASSERT_LT(static_cast<uint64_t>(RATIO * 20 * G), BASE);
  EXPECT_EQ(BASE, d.effective);
  EXPECT_FALSE(d.is_scaled());
}

TEST(HealthMonitorDataSizeWarn, CapDisabled)
{
  auto d = derive(PER_OSD, PER_PG, 2060, 34273, 20 * G, 0.0);
  EXPECT_EQ(d.raw, d.effective);
  EXPECT_TRUE(d.is_scaled());
  EXPECT_FALSE(d.is_capped());
}

// get_fs_stats() failing leaves the filesystem size at 0, which must not be
// mistaken for a 0-byte cap
TEST(HealthMonitorDataSizeWarn, UnknownFilesystemSize)
{
  auto d = derive(PER_OSD, PER_PG, 2060, 34273, 0, RATIO);
  EXPECT_EQ(d.raw, d.effective);
  EXPECT_TRUE(d.is_scaled());
}

// no configuration may wrap the threshold around
TEST(HealthMonitorDataSizeWarn, SaturatesInsteadOfWrapping)
{
  constexpr uint64_t max = std::numeric_limits<uint64_t>::max();

  auto per_osd = derive(max, 0, 2, 0, 0, 0);
  EXPECT_EQ(max, per_osd.raw);
  EXPECT_EQ(max, per_osd.effective);

  auto per_pg = derive(0, max, 0, 2, 0, 0);
  EXPECT_EQ(max, per_pg.raw);

  auto base = derive(PER_OSD, PER_PG, 2060, 34273, 0, 0, max);
  EXPECT_EQ(max, base.raw);

  // saturated and then capped is still a sane threshold
  auto capped = derive(max, max, max, max, 100 * G, RATIO);
  EXPECT_EQ(static_cast<uint64_t>(RATIO * 100 * G), capped.effective);
}
