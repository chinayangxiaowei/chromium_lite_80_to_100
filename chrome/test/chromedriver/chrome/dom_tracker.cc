// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/chromedriver/chrome/dom_tracker.h"

#include <stddef.h>

#include <utility>

#include "base/json/json_writer.h"
#include "base/values.h"
#include "chrome/test/chromedriver/chrome/devtools_client.h"
#include "chrome/test/chromedriver/chrome/status.h"

DomTracker::DomTracker(DevToolsClient* client) {
  client->AddListener(this);
}

DomTracker::~DomTracker() {}

Status DomTracker::GetFrameIdForNode(int node_id, std::string* frame_id) {
  if (node_to_frame_map_.count(node_id) == 0) {
    return Status(kNoSuchFrame, "element is not a frame");
  }
  *frame_id = node_to_frame_map_[node_id];
  return Status(kOk);
}

Status DomTracker::OnConnected(DevToolsClient* client) {
  return RebuildMapping(client);
}

Status DomTracker::OnEvent(DevToolsClient* client,
                           const std::string& method,
                           const base::DictionaryValue& params) {
  if (method == "DOM.setChildNodes") {
    const base::Value* nodes = params.FindKey("nodes");
    if (nodes == nullptr)
      return Status(kUnknownError, "DOM.setChildNodes missing 'nodes'");

    if (!ProcessNodeList(*nodes)) {
      std::string json;
      base::JSONWriter::Write(*nodes, &json);
      return Status(kUnknownError,
                    "DOM.setChildNodes has invalid 'nodes': " + json);
    }
  } else if (method == "DOM.childNodeInserted") {
    const base::Value* node = params.FindKey("node");
    if (node == nullptr) {
      return Status(kUnknownError, "DOM.childNodeInserted missing 'node'");
    }

    if (!ProcessNode(*node)) {
      std::string json;
      base::JSONWriter::Write(*node, &json);
      return Status(kUnknownError,
                    "DOM.childNodeInserted has invalid 'node': " + json);
    }
  } else if (method == "Page.frameAttached") {
    const std::string* frame_id = params.FindStringKey("frameId");
    if (frame_id == nullptr) {
      return Status(kUnknownError,
                    "Page.frameAttached missing 'frameId' in the event");
    }

    base::DictionaryValue params;
    params.SetString("frameId", *frame_id);
    base::Value result;
    auto status =
        client->SendCommandAndGetResult("DOM.getFrameOwner", params, &result);
    if (status.IsError()) {
      if (status.code() == kNoSuchFrame) {
        // Frame was deleted before DOM.getFrameOwner arrived to the browser.
        return Status(kOk);
      }
      return status;
    }
    auto ownder_node_id = result.FindIntKey("nodeId");
    if (ownder_node_id.has_value()) {
      node_to_frame_map_.emplace(ownder_node_id.value(), *frame_id);
    } else {
      // NodeId is missing only if nodeId's have been invalidated between
      // handling of event Page.frameAttached and receiving the response to
      // DOM.getFrameOwner. In this case DOM.documentUpdated should have been
      // sent to us in between and we should have requested for DOM.getDocument
      // and received the response. This means that the mapping must have been
      // updated accordingly.
      // It is also possible that the frame in query was removed in between,
      // therefore the corresponding entry might still be missing in
      // node_to_frame_map_.
    }
  } else if (method == "DOM.documentUpdated") {
    return RebuildMapping(client);
  }
  return Status(kOk);
}

bool DomTracker::ProcessNodeList(const base::Value& nodes) {
  if (!nodes.is_list())
    return false;
  for (const base::Value& node : nodes.GetListDeprecated()) {
    if (!ProcessNode(node))
      return false;
  }
  return true;
}

bool DomTracker::ProcessNode(const base::Value& node) {
  const base::DictionaryValue* dict;
  if (!node.GetAsDictionary(&dict))
    return false;
  absl::optional<int> node_id = dict->FindIntKey("nodeId");
  if (!node_id)
    return false;
  std::string frame_id;
  if (dict->GetString("frameId", &frame_id)) {
    node_to_frame_map_.insert(std::make_pair(*node_id, frame_id));
  }

  const std::string* node_name = dict->FindStringKey("nodeName");
  if (node_name && *node_name == "FENCEDFRAME")
    ProcessFencedFrameShadowDom(node);

  if (const base::Value* children = dict->FindKey("children"))
    return ProcessNodeList(*children);
  return true;
}

// When fenced frames use their shadow-dom implementation, they have an iframe
// nested inside the shadow dom. We link the frameId of this iframe to the
// fenced frame element to allow switchToFrame to work correctly.
void DomTracker::ProcessFencedFrameShadowDom(const base::Value& node) {
  const base::Value* shadow_roots = node.FindListKey("shadowRoots");
  if (!shadow_roots)
    return;

  // Find user-agent shadow root inside fenced frame.
  const base::Value* ua_shadow_root = nullptr;
  for (const base::Value& shadow_root : shadow_roots->GetListDeprecated()) {
    const std::string* shadow_root_type =
        shadow_root.FindStringKey("shadowRootType");
    if (shadow_root_type && *shadow_root_type == "user-agent") {
      ua_shadow_root = &shadow_root;
      break;
    }
  }
  if (!ua_shadow_root ||
      ua_shadow_root->FindIntKey("childNodeCount").value_or(0) == 0)
    return;

  // Find iframe inside fenced frame's shadow dom.
  const base::Value* iframe_node = nullptr;
  const base::Value* shadow_root_children =
      ua_shadow_root->FindListKey("children");
  DCHECK(shadow_root_children);
  for (const base::Value& child : shadow_root_children->GetListDeprecated()) {
    if (*child.FindStringKey("nodeName") == "IFRAME") {
      iframe_node = &child;
      break;
    }
  }
  if (!iframe_node)
    return;

  // Associate fenced frame element with nested iframe's frame id.
  const std::string* child_frame_id = iframe_node->FindStringKey("frameId");
  if (child_frame_id) {
    node_to_frame_map_.insert(
        std::make_pair(*(node.FindIntKey("nodeId")), *child_frame_id));
  }
}

Status DomTracker::RebuildMapping(DevToolsClient* client) {
  node_to_frame_map_.clear();
  base::DictionaryValue params;
  params.SetInteger("depth", -1);
  params.SetBoolKey("pierce", true);
  base::Value result;
  // Fetch the root document and traverse it populating node_to_frame_map_.
  // The map will be updated later whenever Inspector pushes DOM node
  // information to the client.
  auto status =
      client->SendCommandAndGetResult("DOM.getDocument", params, &result);
  if (status.IsError()) {
    return status;
  }

  if (const base::Value* root = result.FindKey("root")) {
    ProcessNode(*root);
  } else {
    status =
        Status(kUnknownError, "DOM.getDocument missing 'root' in the response");
  }
  return status;
}
