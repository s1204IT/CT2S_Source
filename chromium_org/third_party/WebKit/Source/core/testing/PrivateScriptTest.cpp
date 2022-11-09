// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"
#include "core/testing/PrivateScriptTest.h"

#include "bindings/core/v8/PrivateScriptRunner.h"
#include "core/frame/LocalFrame.h"
#include <v8.h>

namespace blink {

PrivateScriptTest::PrivateScriptTest(LocalFrame* frame)
{
    v8::Handle<v8::Value> classObject = PrivateScriptRunner::installClassIfNeeded(frame, "PrivateScriptTest");
    RELEASE_ASSERT(!classObject.IsEmpty());
}

int PrivateScriptTest::addIntegerImplementedInCPPForPrivateScriptOnly(int value1, int value2)
{
    return value1 + value2;
}

String PrivateScriptTest::stringAttributeImplementedInCPPForPrivateScriptOnly()
{
    return m_stringAttributeImplementedInCPPForPrivateSriptOnly;
}

void PrivateScriptTest::setStringAttributeImplementedInCPPForPrivateScriptOnly(String value)
{
    m_stringAttributeImplementedInCPPForPrivateSriptOnly = value;
}

} // namespace blink
