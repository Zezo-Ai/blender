/* SPDX-FileCopyrightText: 2011-2022 Blender Foundation
 *
 * SPDX-License-Identifier: Apache-2.0 */

#include <iomanip>

#include "device/kernel.h"
#include "device/queue.h"

#include "util/algorithm.h"
#include "util/log.h"
#include "util/time.h"

CCL_NAMESPACE_BEGIN

DeviceQueue::DeviceQueue(Device *device) : device(device)
{
  DCHECK_NE(device, nullptr);
  is_per_kernel_performance_ = getenv("CYCLES_DEBUG_PER_KERNEL_PERFORMANCE");
}

DeviceQueue::~DeviceQueue()
{
  if (LOG_IS_ON(LOG_LEVEL_STATS)) {
    /* Print kernel execution times sorted by time. */
    vector<pair<DeviceKernelMask, double>> stats_sorted;
    for (const auto &stat : stats_kernel_time_) {
      stats_sorted.push_back(stat);
    }

    sort(stats_sorted.begin(),
         stats_sorted.end(),
         [](const pair<DeviceKernelMask, double> &a, const pair<DeviceKernelMask, double> &b) {
           return a.second > b.second;
         });

    LOG_STATS << "GPU queue stats:";
    double total_time = 0.0;
    for (const auto &[mask, time] : stats_sorted) {
      total_time += time;
      LOG_STATS << "  " << std::setfill(' ') << std::setw(10) << std::fixed << std::setprecision(5)
                << std::right << time << "s: " << device_kernel_mask_as_string(mask);
    }

    if (is_per_kernel_performance_) {
      LOG_STATS << "GPU queue total time: " << std::fixed << std::setprecision(5) << total_time;
    }
  }
}

void DeviceQueue::debug_init_execution()
{
  if (LOG_IS_ON(LOG_LEVEL_STATS)) {
    last_sync_time_ = time_dt();
  }

  last_kernels_enqueued_.reset();
}

void DeviceQueue::debug_enqueue_begin(DeviceKernel kernel, const int work_size)
{
  if (LOG_IS_ON(LOG_LEVEL_STATS)) {
    LOG_STATS << "GPU queue launch " << device_kernel_as_string(kernel) << ", work_size "
              << work_size;
  }

  last_kernels_enqueued_.set(kernel, true);
}

void DeviceQueue::debug_enqueue_end()
{
  if (LOG_IS_ON(LOG_LEVEL_STATS) && is_per_kernel_performance_) {
    synchronize();
  }
}

void DeviceQueue::debug_synchronize()
{
  if (LOG_IS_ON(LOG_LEVEL_STATS)) {
    const double new_time = time_dt();
    const double elapsed_time = new_time - last_sync_time_;
    LOG_STATS << "GPU queue synchronize, elapsed " << std::setw(10) << elapsed_time << "s";

    /* There is no sense to have an entries in the performance data
     * container without related kernel information. */
    if (last_kernels_enqueued_.any()) {
      stats_kernel_time_[last_kernels_enqueued_] += elapsed_time;
    }

    last_sync_time_ = new_time;
  }

  last_kernels_enqueued_.reset();
}

string DeviceQueue::debug_active_kernels()
{
  return device_kernel_mask_as_string(last_kernels_enqueued_);
}

CCL_NAMESPACE_END
