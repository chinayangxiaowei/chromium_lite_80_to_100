// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/document_transition/document_transition_request.h"

#include <utility>

#include "base/test/bind.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cc {

TEST(DocumentTransitionRequestTest, PrepareRequest) {
  bool called = false;
  auto callback = base::BindLambdaForTesting([&called]() { called = true; });

  auto request = DocumentTransitionRequest::CreatePrepare(
      DocumentTransitionRequest::Effect::kRevealLeft,
      /*document_tag=*/0, DocumentTransitionRequest::TransitionConfig(),
      /*shared_element_config=*/{}, std::move(callback), false);

  EXPECT_FALSE(called);
  request->TakeFinishedCallback().Run();
  EXPECT_TRUE(called);
  EXPECT_TRUE(request->TakeFinishedCallback().is_null());

  auto directive = request->ConstructDirective({});
  EXPECT_GT(directive.sequence_id(), 0u);
  EXPECT_EQ(DocumentTransitionRequest::Effect::kRevealLeft, directive.effect());
  EXPECT_EQ(viz::CompositorFrameTransitionDirective::Type::kSave,
            directive.type());
  EXPECT_FALSE(directive.is_renderer_driven_animation());

  auto duplicate = request->ConstructDirective({});
  EXPECT_EQ(duplicate.sequence_id(), directive.sequence_id());
  EXPECT_EQ(duplicate.effect(), directive.effect());
  EXPECT_EQ(duplicate.type(), directive.type());
  EXPECT_EQ(duplicate.is_renderer_driven_animation(),
            directive.is_renderer_driven_animation());
}

TEST(DocumentTransitionRequestTest, StartRequest) {
  bool called = false;
  auto callback = base::BindLambdaForTesting([&called]() { called = true; });

  auto request = DocumentTransitionRequest::CreateStart(
      /*document_tag=*/0, /*shared_element_transition=*/0, std::move(callback));

  EXPECT_FALSE(called);
  request->TakeFinishedCallback().Run();
  EXPECT_TRUE(called);
  EXPECT_TRUE(request->TakeFinishedCallback().is_null());

  auto directive = request->ConstructDirective({});
  EXPECT_GT(directive.sequence_id(), 0u);
  EXPECT_EQ(viz::CompositorFrameTransitionDirective::Type::kAnimate,
            directive.type());
  EXPECT_FALSE(directive.is_renderer_driven_animation());
}

}  // namespace cc
