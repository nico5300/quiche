// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_subscribe_windows.h"

#include <cstdint>
#include <optional>
#include <vector>

#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

ReducedSequenceIndex::ReducedSequenceIndex(
    Location sequence, MoqtForwardingPreference preference) {
  switch (preference) {
    case MoqtForwardingPreference::kSubgroup:
      sequence_ = Location(sequence.group, sequence.subgroup, 0);
      break;
    case MoqtForwardingPreference::kDatagram:
      sequence_ = Location(sequence.group, 0, sequence.object);
      return;
  }
}

std::optional<webtransport::StreamId> SendStreamMap::GetStreamForSequence(
    Location sequence) const {
  QUICHE_DCHECK(forwarding_preference_ == MoqtForwardingPreference::kSubgroup);
  Location index =
      ReducedSequenceIndex(sequence, forwarding_preference_).sequence();
  auto group_it = send_streams_.find(index.group);
  if (group_it == send_streams_.end()) {
    return std::nullopt;
  }
  auto subgroup_it = group_it->second.find(index.subgroup);
  if (subgroup_it == group_it->second.end()) {
    return std::nullopt;
  }
  return subgroup_it->second;
}

void SendStreamMap::AddStream(Location sequence,
                              webtransport::StreamId stream_id) {
  Location index =
      ReducedSequenceIndex(sequence, forwarding_preference_).sequence();
  auto [it, result] = send_streams_.insert({index.group, Group()});
  auto [sg, success] = it->second.try_emplace(index.subgroup, stream_id);
  QUIC_BUG_IF(quic_bug_moqt_draft_03_02, !success) << "Stream already added";
}

void SendStreamMap::RemoveStream(Location sequence,
                                 webtransport::StreamId stream_id) {
  Location index =
      ReducedSequenceIndex(sequence, forwarding_preference_).sequence();
  auto group_it = send_streams_.find(index.group);
  if (group_it == send_streams_.end()) {
    QUICHE_NOTREACHED();
    return;
  }
  auto subgroup_it = group_it->second.find(index.subgroup);
  if (subgroup_it == group_it->second.end() ||
      subgroup_it->second != stream_id) {
    QUICHE_NOTREACHED();
    return;
  }
  group_it->second.erase(subgroup_it);
}

bool SubscribeWindow::TruncateStart(Location start) {
  if (start < start_) {
    return false;
  }
  start_ = start;
  return true;
}

bool SubscribeWindow::TruncateEnd(uint64_t end_group) {
  if (end_group > end_.group) {
    return false;
  }
  end_ = Location(end_group, UINT64_MAX);
  return true;
}

bool SubscribeWindow::TruncateEnd(Location largest_id) {
  if (largest_id > end_) {
    return false;
  }
  end_ = largest_id;
  return true;
}

std::vector<webtransport::StreamId> SendStreamMap::GetAllStreams() const {
  std::vector<webtransport::StreamId> ids;
  for (const auto& [group, subgroup_map] : send_streams_) {
    for (const auto& [subgroup, stream_id] : subgroup_map) {
      ids.push_back(stream_id);
    }
  }
  return ids;
}

std::vector<webtransport::StreamId> SendStreamMap::GetStreamsForGroup(
    uint64_t group_id) const {
  std::vector<webtransport::StreamId> ids;
  auto it = send_streams_.find(group_id);
  if (it == send_streams_.end()) {
    return ids;
  }
  for (const auto& [subgroup, stream_id] : it->second) {
    ids.push_back(stream_id);
  }
  return ids;
}

}  // namespace moqt
